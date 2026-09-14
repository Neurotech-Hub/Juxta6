/*
 * Nordic CS / RAS implementation behind juxta_range.h.
 * Aligned with NCS channel_sounding ras_initiator / ras_reflector samples
 * (NCS ≥ 3.3.4): mode 2 + sub-mode 1, continuous procedures, DE sliding
 * window of 9 with per-method medians, and CS config remove on teardown.
 *
 * Do not copy nRF54L15 DK CS antenna overlays onto Tag (those GPIOs are TWI).
 */

#include "juxta_range.h"

#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/cs.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

#include <bluetooth/cs_de.h>
#include <bluetooth/gatt_dm.h>
#include <bluetooth/services/ras.h>

LOG_MODULE_REGISTER(juxta_range, LOG_LEVEL_INF);

#define CS_CONFIG_ID 0
/* Nordic sample default: PBR (mode 2) + RTT sub-mode 1. */
#define CS_CONFIG_MODE BT_CONN_LE_CS_MAIN_MODE_2_SUB_MODE_1
#define NUM_MODE_0_STEPS 3
#define PROCEDURE_COUNTER_NONE (-1)
#define DE_SLIDING_WINDOW_SIZE 9
/* Enough wall time for ≥9 procedures at Nordic-like intervals. */
#define RANGE_TIMEOUT_MS 60000
#define REFLECTOR_ARM_MS 30000
#define REFLECTOR_HOLD_MS 55000
#define CHANNEL_INDEX_OFFSET 2
#define TONE_QI_OK_TONE_COUNT_THRESHOLD 15
#define MAX_AP CONFIG_BT_RAS_MAX_ANTENNA_PATHS

#define LOCAL_PROCEDURE_MEM                                                                        \
	((BT_RAS_MAX_STEPS_PER_PROCEDURE * sizeof(struct bt_le_cs_subevent_step)) +                \
	 (BT_RAS_MAX_STEPS_PER_PROCEDURE * BT_RAS_MAX_STEP_DATA_LEN))

static K_SEM_DEFINE(sem_security, 0, 1);
static K_SEM_DEFINE(sem_mtu, 0, 1);
static K_SEM_DEFINE(sem_discovery, 0, 1);
static K_SEM_DEFINE(sem_ras_features, 0, 1);
static K_SEM_DEFINE(sem_caps, 0, 1);
static K_SEM_DEFINE(sem_config, 0, 1);
static K_SEM_DEFINE(sem_cs_sec, 0, 1);
static K_SEM_DEFINE(sem_proc_en, 0, 1);
static K_SEM_DEFINE(sem_local_steps, 1, 1);
static K_SEM_DEFINE(sem_estimate, 0, 1);

static K_MUTEX_DEFINE(distance_estimate_buffer_mutex);

static struct bt_conn *active_conn;
static struct bt_conn_le_cs_config cs_config;
static int discovery_err;
static int last_config_status;
static uint32_t ras_feature_bits;

struct distance_estimate_buffer {
	cs_de_dist_estimates_t estimates[DE_SLIDING_WINDOW_SIZE];
	uint8_t num_valid;
	uint8_t index;
};

static struct distance_estimate_buffer distance_estimate_buffers[MAX_AP];

static float last_distance_m;
static float last_phase_slope_m;
static float last_ifft_m;
static float last_rtt_m;
static uint8_t last_samples;
static uint16_t last_quality;
static int last_estimate_status;
static atomic_t estimate_done;

static int32_t most_recent_local_ranging_counter = PROCEDURE_COUNTER_NONE;
static int32_t dropped_ranging_counter = PROCEDURE_COUNTER_NONE;
static uint16_t m_n_iqs[MAX_AP][CS_DE_NUM_CHANNELS];

NET_BUF_SIMPLE_DEFINE_STATIC(latest_local_steps, LOCAL_PROCEDURE_MEM);
NET_BUF_SIMPLE_DEFINE_STATIC(latest_peer_steps, BT_RAS_PROCEDURE_MEM);

static void result_clear(struct juxta_range_result *out)
{
	out->distance_m = NAN;
	out->phase_slope_m = NAN;
	out->ifft_m = NAN;
	out->rtt_m = NAN;
	out->samples = 0;
	out->quality = JUXTA_RANGE_QUALITY_INVALID;
	out->status = -EIO;
}

static void distance_buffers_reset(void)
{
	(void)k_mutex_lock(&distance_estimate_buffer_mutex, K_FOREVER);
	memset(distance_estimate_buffers, 0, sizeof(distance_estimate_buffers));
	(void)k_mutex_unlock(&distance_estimate_buffer_mutex);
}

static void store_distance_estimates_in_buffer(cs_de_dist_estimates_t *p_estimates,
					       struct distance_estimate_buffer *buffer)
{
	(void)k_mutex_lock(&distance_estimate_buffer_mutex, K_FOREVER);

	memcpy(&buffer->estimates[buffer->index], p_estimates, sizeof(*p_estimates));
	buffer->index = (uint8_t)((buffer->index + 1U) % DE_SLIDING_WINDOW_SIZE);
	if (buffer->num_valid < DE_SLIDING_WINDOW_SIZE) {
		buffer->num_valid++;
	}

	(void)k_mutex_unlock(&distance_estimate_buffer_mutex);
}

static int float_cmp(const void *a, const void *b)
{
	float fa = *(const float *)a;
	float fb = *(const float *)b;

	return (fa > fb) - (fa < fb);
}

static float median_inplace(int count, float *values)
{
	if (count == 0) {
		return NAN;
	}

	qsort(values, (size_t)count, sizeof(float), float_cmp);

	if ((count % 2) == 0) {
		return (values[count / 2] + values[count / 2 - 1]) / 2.0f;
	}

	return values[count / 2];
}

/* Nordic ras_initiator get_distance(): median per method across the window. */
static cs_de_dist_estimates_t get_distance(uint8_t ap)
{
	cs_de_dist_estimates_t averaged_result = {};
	uint8_t num_ifft = 0;
	uint8_t num_phase_slope = 0;
	uint8_t num_rtt = 0;

	static float temp_ifft[DE_SLIDING_WINDOW_SIZE];
	static float temp_phase_slope[DE_SLIDING_WINDOW_SIZE];
	static float temp_rtt[DE_SLIDING_WINDOW_SIZE];

	struct distance_estimate_buffer *buffer = &distance_estimate_buffers[ap];

	(void)k_mutex_lock(&distance_estimate_buffer_mutex, K_FOREVER);

	for (uint8_t i = 0; i < buffer->num_valid; i++) {
		if (isfinite(buffer->estimates[i].ifft)) {
			temp_ifft[num_ifft++] = buffer->estimates[i].ifft;
		}
		if (isfinite(buffer->estimates[i].phase_slope)) {
			temp_phase_slope[num_phase_slope++] = buffer->estimates[i].phase_slope;
		}
		if (isfinite(buffer->estimates[i].rtt)) {
			temp_rtt[num_rtt++] = buffer->estimates[i].rtt;
		}
	}

	(void)k_mutex_unlock(&distance_estimate_buffer_mutex);

	averaged_result.ifft = median_inplace(num_ifft, temp_ifft);
	averaged_result.phase_slope = median_inplace(num_phase_slope, temp_phase_slope);
	averaged_result.rtt = median_inplace(num_rtt, temp_rtt);

	/* Match cs_de set_best_estimate priority: ifft → phase_slope → rtt. */
	if (isfinite(averaged_result.ifft)) {
		averaged_result.best = averaged_result.ifft;
	} else if (isfinite(averaged_result.phase_slope)) {
		averaged_result.best = averaged_result.phase_slope;
	} else if (isfinite(averaged_result.rtt)) {
		averaged_result.best = averaged_result.rtt;
	} else {
		averaged_result.best = NAN;
	}

	return averaged_result;
}

static uint8_t distance_buffer_num_valid(uint8_t ap)
{
	uint8_t n;

	(void)k_mutex_lock(&distance_estimate_buffer_mutex, K_FOREVER);
	n = distance_estimate_buffers[ap].num_valid;
	(void)k_mutex_unlock(&distance_estimate_buffer_mutex);
	return n;
}

static void publish_median_estimate(void)
{
	cs_de_dist_estimates_t est = get_distance(0);
	uint8_t samples = distance_buffer_num_valid(0);

	last_samples = samples;
	last_ifft_m = est.ifft;
	last_phase_slope_m = est.phase_slope;
	last_rtt_m = est.rtt;
	last_distance_m = est.best;

	if (!isfinite(last_distance_m)) {
		last_estimate_status = -EIO;
		last_quality = JUXTA_RANGE_QUALITY_INVALID;
		return;
	}

	last_estimate_status = 0;
	last_quality = JUXTA_RANGE_QUALITY_OK;
	LOG_INF("median samples=%u best=%.3f ifft=%.3f phase_slope=%.3f rtt=%.3f", samples,
		(double)last_distance_m, (double)last_ifft_m, (double)last_phase_slope_m,
		(double)last_rtt_m);
}

static bool m_is_tone_quality_ok(uint16_t num_iqs[CS_DE_NUM_CHANNELS], uint8_t channel_map[10])
{
	uint8_t ok_tones_count = 0;

	for (uint8_t i = 0; i < CS_DE_NUM_CHANNELS; ++i) {
		if (BT_LE_CS_CHANNEL_BIT_GET(channel_map, i + CHANNEL_INDEX_OFFSET) &&
		    num_iqs[i] >= 1) {
			ok_tones_count += 1;
		}
	}
	return (ok_tones_count >= TONE_QI_OK_TONE_COUNT_THRESHOLD);
}

static void cumulate_mean(float *avg, float new_value, uint16_t *N)
{
	float a = 1.0f / (*N);
	float b = 1.0f - a;

	*avg = a * new_value + b * (*avg);
}

static void extract_pcts(cs_de_report_t *p_report, uint8_t channel_index,
			 uint8_t antenna_permutation_index,
			 struct bt_hci_le_cs_step_data_tone_info *local_tone_info,
			 struct bt_hci_le_cs_step_data_tone_info *remote_tone_info)
{
	for (uint8_t tone_index = 0; tone_index < p_report->n_ap; tone_index++) {
		int antenna_path =
			bt_le_cs_get_antenna_path(p_report->n_ap, antenna_permutation_index,
						  tone_index);
		if (antenna_path < 0) {
			LOG_WRN("Invalid antenna path");
			return;
		}

		if (local_tone_info[tone_index].quality_indicator !=
			    BT_HCI_LE_CS_TONE_QUALITY_HIGH ||
		    remote_tone_info[tone_index].quality_indicator !=
			    BT_HCI_LE_CS_TONE_QUALITY_HIGH) {
			return;
		}

		struct bt_le_cs_iq_sample local_iq =
			bt_le_cs_parse_pct(local_tone_info[tone_index].phase_correction_term);
		struct bt_le_cs_iq_sample remote_iq =
			bt_le_cs_parse_pct(remote_tone_info[tone_index].phase_correction_term);

		m_n_iqs[antenna_path][channel_index]++;

		if (m_n_iqs[antenna_path][channel_index] == 1) {
			p_report->iq_tones[antenna_path].i_local[channel_index] = local_iq.i;
			p_report->iq_tones[antenna_path].q_local[channel_index] = local_iq.q;
			p_report->iq_tones[antenna_path].i_remote[channel_index] = remote_iq.i;
			p_report->iq_tones[antenna_path].q_remote[channel_index] = remote_iq.q;
		} else {
			cumulate_mean(&p_report->iq_tones[antenna_path].i_local[channel_index],
				      local_iq.i, &m_n_iqs[antenna_path][channel_index]);
			cumulate_mean(&p_report->iq_tones[antenna_path].q_local[channel_index],
				      local_iq.q, &m_n_iqs[antenna_path][channel_index]);
			cumulate_mean(&p_report->iq_tones[antenna_path].i_remote[channel_index],
				      remote_iq.i, &m_n_iqs[antenna_path][channel_index]);
			cumulate_mean(&p_report->iq_tones[antenna_path].q_remote[channel_index],
				      remote_iq.q, &m_n_iqs[antenna_path][channel_index]);
		}
	}
}

static void extract_rtt_timings(cs_de_report_t *p_report,
				struct bt_hci_le_cs_step_data_mode_1 *local_rtt_data,
				struct bt_hci_le_cs_step_data_mode_1 *peer_rtt_data)
{
	if (local_rtt_data->packet_quality_aa_check !=
		    BT_HCI_LE_CS_PACKET_QUALITY_AA_CHECK_SUCCESSFUL ||
	    local_rtt_data->packet_rssi == BT_HCI_LE_CS_PACKET_RSSI_NOT_AVAILABLE ||
	    local_rtt_data->tod_toa_reflector == BT_HCI_LE_CS_TIME_DIFFERENCE_NOT_AVAILABLE ||
	    peer_rtt_data->packet_quality_aa_check !=
		    BT_HCI_LE_CS_PACKET_QUALITY_AA_CHECK_SUCCESSFUL ||
	    peer_rtt_data->packet_rssi == BT_HCI_LE_CS_PACKET_RSSI_NOT_AVAILABLE ||
	    peer_rtt_data->tod_toa_reflector == BT_HCI_LE_CS_TIME_DIFFERENCE_NOT_AVAILABLE) {
		return;
	}

	if (p_report->role == BT_CONN_LE_CS_ROLE_INITIATOR) {
		p_report->rtt_accumulated_half_ns +=
			local_rtt_data->toa_tod_initiator - peer_rtt_data->tod_toa_reflector;
	} else {
		p_report->rtt_accumulated_half_ns +=
			peer_rtt_data->toa_tod_initiator - local_rtt_data->tod_toa_reflector;
	}

	p_report->rtt_count++;
}

static bool process_ranging_header(struct ras_ranging_header *ranging_header, void *user_data)
{
	cs_de_report_t *p_report = (cs_de_report_t *)user_data;

	p_report->n_ap = MAX(1, ((ranging_header->antenna_paths_mask & BIT(0)) +
				 ((ranging_header->antenna_paths_mask & BIT(1)) >> 1) +
				 ((ranging_header->antenna_paths_mask & BIT(2)) >> 2) +
				 ((ranging_header->antenna_paths_mask & BIT(3)) >> 3)));
	return true;
}

static bool process_step_data(struct bt_le_cs_subevent_step *local_step,
			      struct bt_le_cs_subevent_step *peer_step, void *user_data)
{
	cs_de_report_t *p_report = (cs_de_report_t *)user_data;

	if (local_step->mode == BT_HCI_OP_LE_CS_MAIN_MODE_2) {
		struct bt_hci_le_cs_step_data_mode_2 *local_step_data =
			(struct bt_hci_le_cs_step_data_mode_2 *)local_step->data;
		struct bt_hci_le_cs_step_data_mode_2 *peer_step_data =
			(struct bt_hci_le_cs_step_data_mode_2 *)peer_step->data;

		extract_pcts(p_report, local_step->channel - CHANNEL_INDEX_OFFSET,
			     local_step_data->antenna_permutation_index, local_step_data->tone_info,
			     peer_step_data->tone_info);
	} else if (local_step->mode == BT_HCI_OP_LE_CS_MAIN_MODE_1) {
		struct bt_hci_le_cs_step_data_mode_1 *local_step_data =
			(struct bt_hci_le_cs_step_data_mode_1 *)local_step->data;
		struct bt_hci_le_cs_step_data_mode_1 *peer_step_data =
			(struct bt_hci_le_cs_step_data_mode_1 *)peer_step->data;

		extract_rtt_timings(p_report, local_step_data, peer_step_data);
	} else if (local_step->mode == BT_HCI_OP_LE_CS_MAIN_MODE_3) {
		struct bt_hci_le_cs_step_data_mode_3 *local_step_data =
			(struct bt_hci_le_cs_step_data_mode_3 *)local_step->data;
		struct bt_hci_le_cs_step_data_mode_3 *peer_step_data =
			(struct bt_hci_le_cs_step_data_mode_3 *)peer_step->data;

		extract_pcts(p_report, local_step->channel - CHANNEL_INDEX_OFFSET,
			     local_step_data->antenna_permutation_index, local_step_data->tone_info,
			     peer_step_data->tone_info);
		extract_rtt_timings(p_report,
				    (struct bt_hci_le_cs_step_data_mode_1 *)local_step_data,
				    (struct bt_hci_le_cs_step_data_mode_1 *)peer_step_data);
	}

	return true;
}

static void try_complete_estimate(void)
{
	if (distance_buffer_num_valid(0) < DE_SLIDING_WINDOW_SIZE) {
		return;
	}

	if (!atomic_cas(&estimate_done, 0, 1)) {
		return;
	}

	publish_median_estimate();
	k_sem_give(&sem_estimate);
}

static void ranging_data_get_complete_cb(struct bt_conn *conn, uint16_t ranging_counter, int err)
{
	ARG_UNUSED(conn);

	if (err) {
		LOG_ERR("Ranging data receive failed counter=%u (%d)", ranging_counter, err);
		net_buf_simple_reset(&latest_local_steps);
		if (!(ras_feature_bits & RAS_FEAT_REALTIME_RD)) {
			net_buf_simple_reset(&latest_peer_steps);
		}
		k_sem_give(&sem_local_steps);
		return;
	}

	if (ranging_counter != most_recent_local_ranging_counter) {
		LOG_INF("Ranging data dropped (peer=%u local=%d)", ranging_counter,
			most_recent_local_ranging_counter);
		net_buf_simple_reset(&latest_local_steps);
		k_sem_give(&sem_local_steps);
		return;
	}

	static cs_de_report_t cs_de_report;

	if (latest_local_steps.len == 0) {
		LOG_WRN("All subevents aborted for ranging counter %u", ranging_counter);
		net_buf_simple_reset(&latest_local_steps);
		k_sem_give(&sem_local_steps);
		if (!(ras_feature_bits & RAS_FEAT_REALTIME_RD)) {
			net_buf_simple_reset(&latest_peer_steps);
		}
		return;
	}

	memset(&cs_de_report, 0, sizeof(cs_de_report));
	memset(m_n_iqs, 0, sizeof(m_n_iqs));
	cs_de_report.role = BT_CONN_LE_CS_ROLE_INITIATOR;

	bt_ras_rreq_rd_subevent_data_parse(&latest_peer_steps, &latest_local_steps, cs_config.role,
					   process_ranging_header, NULL, process_step_data,
					   &cs_de_report);

	for (uint8_t ap = 0; ap < cs_de_report.n_ap; ap++) {
		cs_de_report.distance_estimates[ap].ifft = NAN;
		cs_de_report.distance_estimates[ap].phase_slope = NAN;
		cs_de_report.distance_estimates[ap].rtt = NAN;
		cs_de_report.distance_estimates[ap].best = NAN;

		if (m_is_tone_quality_ok(m_n_iqs[ap], cs_config.channel_map)) {
			cs_de_report.tone_quality[ap] = CS_DE_TONE_QUALITY_OK;
		} else {
			cs_de_report.tone_quality[ap] = CS_DE_TONE_QUALITY_BAD;
		}
	}

	net_buf_simple_reset(&latest_local_steps);
	if (!(ras_feature_bits & RAS_FEAT_REALTIME_RD)) {
		net_buf_simple_reset(&latest_peer_steps);
	}
	k_sem_give(&sem_local_steps);

	cs_de_quality_t quality = cs_de_calc(&cs_de_report);

	if (quality != CS_DE_QUALITY_OK) {
		LOG_DBG("CS DE quality not OK (skip sample)");
		return;
	}

	for (uint8_t ap = 0; ap < cs_de_report.n_ap; ap++) {
		cs_de_dist_estimates_t *est = &cs_de_report.distance_estimates[ap];

		if (cs_de_report.tone_quality[ap] == CS_DE_TONE_QUALITY_OK || isfinite(est->rtt)) {
			LOG_INF("sample[%u] ifft=%.3f phase_slope=%.3f rtt=%.3f best=%.3f", ap,
				(double)est->ifft, (double)est->phase_slope, (double)est->rtt,
				(double)est->best);
			store_distance_estimates_in_buffer(est, &distance_estimate_buffers[ap]);
		}
	}

	try_complete_estimate();
}

static void subevent_result_cb(struct bt_conn *conn, struct bt_conn_le_cs_subevent_result *result)
{
	ARG_UNUSED(conn);

	if (dropped_ranging_counter == result->header.procedure_counter) {
		return;
	}

	if (most_recent_local_ranging_counter !=
	    bt_ras_rreq_get_ranging_counter(result->header.procedure_counter)) {
		int sem_state = k_sem_take(&sem_local_steps, K_NO_WAIT);

		if (sem_state < 0) {
			dropped_ranging_counter = result->header.procedure_counter;
			LOG_DBG("Dropped subevent results (waiting for peer ranging data)");
			return;
		}

		most_recent_local_ranging_counter =
			bt_ras_rreq_get_ranging_counter(result->header.procedure_counter);
	}

	if (result->header.subevent_done_status == BT_CONN_LE_CS_SUBEVENT_ABORTED) {
		/* unused */
	} else if (result->step_data_buf) {
		if (result->step_data_buf->len <= net_buf_simple_tailroom(&latest_local_steps)) {
			uint16_t len = result->step_data_buf->len;
			uint8_t *step_data = net_buf_simple_pull_mem(result->step_data_buf, len);

			net_buf_simple_add_mem(&latest_local_steps, step_data, len);
		} else {
			LOG_ERR("Not enough memory for step data");
			net_buf_simple_reset(&latest_local_steps);
			dropped_ranging_counter = result->header.procedure_counter;
			return;
		}
	}

	dropped_ranging_counter = PROCEDURE_COUNTER_NONE;

	if (result->header.procedure_done_status == BT_CONN_LE_CS_PROCEDURE_COMPLETE) {
		most_recent_local_ranging_counter =
			bt_ras_rreq_get_ranging_counter(result->header.procedure_counter);
	} else if (result->header.procedure_done_status == BT_CONN_LE_CS_PROCEDURE_ABORTED) {
		LOG_WRN("CS procedure %u aborted", result->header.procedure_counter);
		net_buf_simple_reset(&latest_local_steps);
		k_sem_give(&sem_local_steps);
	}
}

static void ranging_data_ready_cb(struct bt_conn *conn, uint16_t ranging_counter)
{
	if (ranging_counter != most_recent_local_ranging_counter) {
		LOG_DBG("rd_ready %u ignored (local=%d)", ranging_counter,
			most_recent_local_ranging_counter);
		return;
	}

	int err = bt_ras_rreq_cp_get_ranging_data(conn, &latest_peer_steps, ranging_counter,
						  ranging_data_get_complete_cb);

	if (err) {
		LOG_ERR("bt_ras_rreq_cp_get_ranging_data failed (%d)", err);
		net_buf_simple_reset(&latest_local_steps);
		net_buf_simple_reset(&latest_peer_steps);
		k_sem_give(&sem_local_steps);
	}
}

static void ranging_data_overwritten_cb(struct bt_conn *conn, uint16_t ranging_counter)
{
	ARG_UNUSED(conn);
	LOG_WRN("Ranging data overwritten %u", ranging_counter);
}

static void ras_features_read_cb(struct bt_conn *conn, uint32_t feature_bits, int err)
{
	ARG_UNUSED(conn);

	if (err) {
		LOG_ERR("RAS features read failed (%d)", err);
		ras_feature_bits = 0;
	} else {
		ras_feature_bits = feature_bits;
		LOG_INF("RAS features 0x%08x%s", feature_bits,
			(feature_bits & RAS_FEAT_REALTIME_RD) ? " (realtime RD)" : "");
	}
	k_sem_give(&sem_ras_features);
}

static void mtu_exchange_cb(struct bt_conn *conn, uint8_t err,
			    struct bt_gatt_exchange_params *params)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(params);

	if (err) {
		LOG_ERR("MTU exchange failed (err %u)", err);
		discovery_err = -EIO;
	}
	k_sem_give(&sem_mtu);
}

static void discovery_completed_cb(struct bt_gatt_dm *dm, void *context)
{
	ARG_UNUSED(context);

	struct bt_conn *conn = bt_gatt_dm_conn_get(dm);
	int err = bt_ras_rreq_alloc_and_assign_handles(dm, conn);

	if (err) {
		LOG_ERR("RAS RREQ alloc failed (%d)", err);
		discovery_err = err;
	} else {
		discovery_err = 0;
	}

	(void)bt_gatt_dm_data_release(dm);
	k_sem_give(&sem_discovery);
}

static void discovery_service_not_found_cb(struct bt_conn *conn, void *context)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(context);

	LOG_ERR("RAS service not found");
	discovery_err = -ENOENT;
	k_sem_give(&sem_discovery);
}

static void discovery_error_found_cb(struct bt_conn *conn, int err, void *context)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(context);

	LOG_ERR("Discovery failed (%d)", err);
	discovery_err = err;
	k_sem_give(&sem_discovery);
}

static struct bt_gatt_dm_cb discovery_cb = {
	.completed = discovery_completed_cb,
	.service_not_found = discovery_service_not_found_cb,
	.error_found = discovery_error_found_cb,
};

static void security_changed(struct bt_conn *conn, bt_security_t level, enum bt_security_err err)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(level);

	if (err) {
		LOG_ERR("Security failed (%d)", err);
	}
	k_sem_give(&sem_security);
}

static void remote_capabilities_cb(struct bt_conn *conn, uint8_t status,
				   struct bt_conn_le_cs_capabilities *params)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(params);

	if (status == BT_HCI_ERR_SUCCESS) {
		k_sem_give(&sem_caps);
	} else {
		LOG_WRN("CS capability exchange failed 0x%02x", status);
	}
}

static void config_create_cb(struct bt_conn *conn, uint8_t status,
			     struct bt_conn_le_cs_config *config)
{
	ARG_UNUSED(conn);

	if (status == BT_HCI_ERR_SUCCESS) {
		cs_config = *config;
		last_config_status = 0;
		k_sem_give(&sem_config);
	} else {
		/* 0x20 = BT_HCI_ERR_UNSUPP_LL_PARAM_VAL */
		LOG_WRN("CS config failed 0x%02x", status);
		last_config_status = -EIO;
		k_sem_give(&sem_config);
	}
}

static void config_removed_cb(struct bt_conn *conn, uint8_t config_id)
{
	ARG_UNUSED(conn);
	LOG_DBG("CS config %u removed", config_id);
}

static void security_enable_cb(struct bt_conn *conn, uint8_t status)
{
	ARG_UNUSED(conn);

	if (status == BT_HCI_ERR_SUCCESS) {
		k_sem_give(&sem_cs_sec);
	} else {
		LOG_WRN("CS security enable failed 0x%02x", status);
	}
}

static void procedure_enable_cb(struct bt_conn *conn, uint8_t status,
				struct bt_conn_le_cs_procedure_enable_complete *params)
{
	ARG_UNUSED(conn);

	if (status == BT_HCI_ERR_SUCCESS && params->state == 1U) {
		LOG_INF("CS procedures enabled");
		k_sem_give(&sem_proc_en);
	} else if (status == BT_HCI_ERR_SUCCESS && params->state == 0U) {
		LOG_INF("CS procedures disabled");
	} else if (status != BT_HCI_ERR_SUCCESS) {
		LOG_WRN("CS procedures enable failed 0x%02x", status);
	}
}

BT_CONN_CB_DEFINE(juxta_range_conn_cb) = {
	.security_changed = security_changed,
	.le_cs_read_remote_capabilities_complete = remote_capabilities_cb,
	.le_cs_config_complete = config_create_cb,
	.le_cs_config_removed = config_removed_cb,
	.le_cs_security_enable_complete = security_enable_cb,
	.le_cs_procedure_enable_complete = procedure_enable_cb,
	.le_cs_subevent_data_available = subevent_result_cb,
};

static int wait_sem(struct k_sem *sem, const char *what)
{
	int err = k_sem_take(sem, K_MSEC(RANGE_TIMEOUT_MS));

	if (err != 0) {
		LOG_ERR("Timeout waiting for %s", what);
		return -ETIMEDOUT;
	}
	return 0;
}

static void cs_disable_and_remove(struct bt_conn *conn)
{
	struct bt_le_cs_procedure_enable_param disable_params = {
		.config_id = CS_CONFIG_ID,
		.enable = 0,
	};
	int err;

	err = bt_le_cs_procedure_enable(conn, &disable_params);
	if (err && err != -EINVAL) {
		LOG_DBG("CS procedure disable (%d)", err);
	}

	err = bt_le_cs_remove_config(conn, CS_CONFIG_ID);
	if (err && err != -EINVAL) {
		LOG_DBG("CS remove config (%d)", err);
	}
}

static void fill_result(struct juxta_range_result *out)
{
	out->distance_m = last_distance_m;
	out->phase_slope_m = last_phase_slope_m;
	out->ifft_m = last_ifft_m;
	out->rtt_m = last_rtt_m;
	out->samples = last_samples;
	out->quality = last_quality;
	out->status = last_estimate_status;
}

int juxta_range_as_initiator(struct bt_conn *conn, struct juxta_range_result *out)
{
	int err;
	static struct bt_gatt_exchange_params mtu_params;

	if (conn == NULL || out == NULL) {
		return -EINVAL;
	}

	result_clear(out);
	active_conn = conn;
	most_recent_local_ranging_counter = PROCEDURE_COUNTER_NONE;
	dropped_ranging_counter = PROCEDURE_COUNTER_NONE;
	net_buf_simple_reset(&latest_local_steps);
	net_buf_simple_reset(&latest_peer_steps);
	distance_buffers_reset();
	atomic_set(&estimate_done, 0);
	last_estimate_status = -EAGAIN;
	last_quality = JUXTA_RANGE_QUALITY_INVALID;
	last_distance_m = NAN;
	last_phase_slope_m = NAN;
	last_ifft_m = NAN;
	last_rtt_m = NAN;
	last_samples = 0;

	k_sem_reset(&sem_security);
	k_sem_reset(&sem_mtu);
	k_sem_reset(&sem_discovery);
	k_sem_reset(&sem_ras_features);
	k_sem_reset(&sem_caps);
	k_sem_reset(&sem_config);
	k_sem_reset(&sem_cs_sec);
	k_sem_reset(&sem_proc_en);
	k_sem_reset(&sem_estimate);
	last_config_status = -EAGAIN;
	ras_feature_bits = 0;

	err = bt_conn_set_security(conn, BT_SECURITY_L2);
	if (err) {
		LOG_ERR("bt_conn_set_security (%d)", err);
		out->status = err;
		return err;
	}
	err = wait_sem(&sem_security, "security");
	if (err) {
		out->status = err;
		return err;
	}

	mtu_params.func = mtu_exchange_cb;
	err = bt_gatt_exchange_mtu(conn, &mtu_params);
	if (err) {
		LOG_ERR("MTU exchange start (%d)", err);
		out->status = err;
		return err;
	}
	err = wait_sem(&sem_mtu, "mtu");
	if (err) {
		out->status = err;
		return err;
	}

	discovery_err = -EAGAIN;
	err = bt_gatt_dm_start(conn, BT_UUID_RANGING_SERVICE, &discovery_cb, NULL);
	if (err) {
		LOG_ERR("GATT DM start (%d)", err);
		out->status = err;
		return err;
	}
	err = wait_sem(&sem_discovery, "ras discovery");
	if (err) {
		out->status = err;
		return err;
	}
	if (discovery_err != 0) {
		out->status = discovery_err;
		return discovery_err;
	}

	const struct bt_le_cs_set_default_settings_param default_settings = {
		/* Dual-role HIL image: keep both roles available on the Tag. */
		.enable_initiator_role = true,
		.enable_reflector_role = true,
		/* Tag has a single RF path (ANT1 hog); avoid REPETITIVE. */
		.cs_sync_antenna_selection = BT_LE_CS_ANTENNA_SELECTION_OPT_ONE,
		.max_tx_power = BT_HCI_OP_LE_CS_MAX_MAX_TX_POWER,
	};

	err = bt_le_cs_set_default_settings(conn, &default_settings);
	if (err) {
		LOG_ERR("CS default settings (%d)", err);
		out->status = err;
		return err;
	}

	ras_feature_bits = 0;
	k_sem_reset(&sem_ras_features);
	err = bt_ras_rreq_read_features(conn, ras_features_read_cb);
	if (err) {
		LOG_ERR("RAS features read (%d)", err);
		out->status = err;
		return err;
	}
	err = wait_sem(&sem_ras_features, "ras features");
	if (err) {
		out->status = err;
		return err;
	}

	const bool realtime_rd = (ras_feature_bits & RAS_FEAT_REALTIME_RD) != 0U;

	if (realtime_rd) {
		err = bt_ras_rreq_realtime_rd_subscribe(conn, &latest_peer_steps,
							ranging_data_get_complete_cb);
		if (err) {
			LOG_ERR("realtime RD subscribe (%d)", err);
			out->status = err;
			return err;
		}
	} else {
		err = bt_ras_rreq_rd_overwritten_subscribe(conn, ranging_data_overwritten_cb);
		if (err) {
			LOG_ERR("rd_overwritten subscribe (%d)", err);
			out->status = err;
			return err;
		}
		err = bt_ras_rreq_rd_ready_subscribe(conn, ranging_data_ready_cb);
		if (err) {
			LOG_ERR("rd_ready subscribe (%d)", err);
			out->status = err;
			return err;
		}
		err = bt_ras_rreq_on_demand_rd_subscribe(conn);
		if (err) {
			LOG_ERR("on_demand_rd subscribe (%d)", err);
			out->status = err;
			return err;
		}
		err = bt_ras_rreq_cp_subscribe(conn);
		if (err) {
			LOG_ERR("cp subscribe (%d)", err);
			out->status = err;
			return err;
		}
	}

	err = bt_le_cs_read_remote_supported_capabilities(conn);
	if (err) {
		LOG_ERR("CS capabilities (%d)", err);
		out->status = err;
		return err;
	}
	err = wait_sem(&sem_caps, "cs capabilities");
	if (err) {
		out->status = err;
		return err;
	}

	/* Drop any leftover config ID before create (fixes alternating 0x20). */
	(void)bt_le_cs_remove_config(conn, CS_CONFIG_ID);
	k_sem_reset(&sem_config);
	last_config_status = -EAGAIN;

	struct bt_le_cs_create_config_params config_params = {
		.id = CS_CONFIG_ID,
		.mode = CS_CONFIG_MODE,
		.min_main_mode_steps = 2,
		.max_main_mode_steps = 5,
		.main_mode_repetition = 0,
		.mode_0_steps = NUM_MODE_0_STEPS,
		.role = BT_CONN_LE_CS_ROLE_INITIATOR,
		.rtt_type = BT_CONN_LE_CS_RTT_TYPE_AA_ONLY,
		.cs_sync_phy = BT_CONN_LE_CS_SYNC_1M_PHY,
		.channel_map_repetition = 1,
		.channel_selection_type = BT_CONN_LE_CS_CHSEL_TYPE_3B,
		.ch3c_shape = BT_CONN_LE_CS_CH3C_SHAPE_HAT,
		.ch3c_jump = 2,
	};

	bt_le_cs_set_valid_chmap_bits(config_params.channel_map);
	err = bt_le_cs_create_config(conn, &config_params,
				     BT_LE_CS_CREATE_CONFIG_CONTEXT_LOCAL_AND_REMOTE);
	if (err) {
		LOG_ERR("CS create config (%d)", err);
		out->status = err;
		cs_disable_and_remove(conn);
		(void)bt_ras_rreq_free(conn);
		active_conn = NULL;
		return err;
	}
	err = wait_sem(&sem_config, "cs config");
	if (err) {
		out->status = err;
		cs_disable_and_remove(conn);
		(void)bt_ras_rreq_free(conn);
		active_conn = NULL;
		return err;
	}
	if (last_config_status != 0) {
		out->status = last_config_status;
		cs_disable_and_remove(conn);
		(void)bt_ras_rreq_free(conn);
		active_conn = NULL;
		return last_config_status;
	}

	err = bt_le_cs_security_enable(conn);
	if (err) {
		LOG_ERR("CS security enable (%d)", err);
		out->status = err;
		cs_disable_and_remove(conn);
		(void)bt_ras_rreq_free(conn);
		active_conn = NULL;
		return err;
	}
	err = wait_sem(&sem_cs_sec, "cs security");
	if (err) {
		out->status = err;
		cs_disable_and_remove(conn);
		(void)bt_ras_rreq_free(conn);
		active_conn = NULL;
		return err;
	}

	/* Nordic ras_initiator: continuous procedures; realtime RD uses tighter interval. */
	const uint16_t procedure_interval = realtime_rd ? 5U : 20U;
	const struct bt_le_cs_set_procedure_parameters_param procedure_params = {
		.config_id = CS_CONFIG_ID,
		.max_procedure_len = 1000,
		.min_procedure_interval = procedure_interval,
		.max_procedure_interval = procedure_interval,
		.max_procedure_count = 0,
		.min_subevent_len = 16000,
		.max_subevent_len = 16000,
		.tone_antenna_config_selection = BT_LE_CS_TONE_ANTENNA_CONFIGURATION_A1_B1,
		.phy = BT_LE_CS_PROCEDURE_PHY_2M,
		.tx_power_delta = 0x80,
		.preferred_peer_antenna = BT_LE_CS_PROCEDURE_PREFERRED_PEER_ANTENNA_1,
		.snr_control_initiator = BT_LE_CS_SNR_CONTROL_NOT_USED,
		.snr_control_reflector = BT_LE_CS_SNR_CONTROL_NOT_USED,
	};

	err = bt_le_cs_set_procedure_parameters(conn, &procedure_params);
	if (err) {
		LOG_ERR("CS procedure parameters (%d)", err);
		out->status = err;
		cs_disable_and_remove(conn);
		(void)bt_ras_rreq_free(conn);
		active_conn = NULL;
		return err;
	}

	struct bt_le_cs_procedure_enable_param enable_params = {
		.config_id = CS_CONFIG_ID,
		.enable = 1,
	};

	err = bt_le_cs_procedure_enable(conn, &enable_params);
	if (err) {
		LOG_ERR("CS procedure enable (%d)", err);
		out->status = err;
		cs_disable_and_remove(conn);
		(void)bt_ras_rreq_free(conn);
		active_conn = NULL;
		return err;
	}

	err = wait_sem(&sem_estimate, "distance estimate window");
	if (err != 0) {
		/* Timed out: publish whatever samples we have (partial window). */
		if (distance_buffer_num_valid(0) > 0U && atomic_cas(&estimate_done, 0, 1)) {
			publish_median_estimate();
			LOG_WRN("Estimate window incomplete (samples=%u/%u)", last_samples,
				DE_SLIDING_WINDOW_SIZE);
		} else if (atomic_get(&estimate_done) == 0) {
			last_estimate_status = -ETIMEDOUT;
			last_quality = JUXTA_RANGE_QUALITY_INVALID;
			last_distance_m = NAN;
		}
	}

	cs_disable_and_remove(conn);
	(void)bt_ras_rreq_free(conn);
	active_conn = NULL;

	fill_result(out);
	return (out->status == 0) ? 0 : out->status;
}

int juxta_range_as_reflector(struct bt_conn *conn, struct juxta_range_result *out)
{
	int err;

	if (conn == NULL || out == NULL) {
		return -EINVAL;
	}

	result_clear(out);
	k_sem_reset(&sem_proc_en);

	const struct bt_le_cs_set_default_settings_param default_settings = {
		.enable_initiator_role = true,
		.enable_reflector_role = true,
		.cs_sync_antenna_selection = BT_LE_CS_ANTENNA_SELECTION_OPT_ONE,
		.max_tx_power = BT_HCI_OP_LE_CS_MAX_MAX_TX_POWER,
	};

	err = bt_le_cs_set_default_settings(conn, &default_settings);
	if (err) {
		LOG_ERR("CS reflector default settings (%d)", err);
		out->status = err;
		return err;
	}

	LOG_INF("Reflector armed — waiting for CS procedures (up to %u ms)", REFLECTOR_ARM_MS);
	err = k_sem_take(&sem_proc_en, K_MSEC(REFLECTOR_ARM_MS));
	if (err != 0) {
		LOG_WRN("Reflector: no procedure-enable observed (%d)", err);
		out->status = -ETIMEDOUT;
		out->quality = JUXTA_RANGE_QUALITY_INVALID;
		out->distance_m = NAN;
		return -ETIMEDOUT;
	}

	/* Hold for initiator's 9-sample window, or until the peer disconnects. */
	LOG_INF("Reflector holding for initiator window (up to %u ms)", REFLECTOR_HOLD_MS);
	{
		int64_t deadline = k_uptime_get() + REFLECTOR_HOLD_MS;
		bool still_connected = true;

		while (k_uptime_get() < deadline) {
			struct bt_conn_info info;

			if (bt_conn_get_info(conn, &info) != 0 ||
			    info.state != BT_CONN_STATE_CONNECTED) {
				still_connected = false;
				break;
			}
			k_sleep(K_MSEC(200));
		}

		if (still_connected) {
			(void)bt_le_cs_remove_config(conn, CS_CONFIG_ID);
		}
	}

	out->distance_m = NAN;
	out->phase_slope_m = NAN;
	out->ifft_m = NAN;
	out->rtt_m = NAN;
	out->samples = 0;
	out->quality = JUXTA_RANGE_QUALITY_REFLECTOR;
	out->status = 0;
	LOG_INF("Reflector participation complete (no local distance)");
	return 0;
}
