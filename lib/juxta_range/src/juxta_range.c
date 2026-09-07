/*
 * Nordic CS / RAS implementation behind juxta_range.h.
 * Adapted from NCS channel_sounding_ras_{initiator,reflector} samples
 * without DK LED helpers or reboot-on-disconnect.
 */

#include "juxta_range.h"

#include <errno.h>
#include <math.h>
#include <string.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/cs.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>

#include <bluetooth/cs_de.h>
#include <bluetooth/gatt_dm.h>
#include <bluetooth/services/ras.h>

LOG_MODULE_REGISTER(juxta_range, LOG_LEVEL_INF);

#define CS_CONFIG_ID 0
#define NUM_MODE_0_STEPS 3
#define PROCEDURE_COUNTER_NONE (-1)
#define RANGE_TIMEOUT_MS 30000
#define REFLECTOR_WAIT_MS 20000

#define LOCAL_PROCEDURE_MEM                                                                        \
	((BT_RAS_MAX_STEPS_PER_PROCEDURE * sizeof(struct bt_le_cs_subevent_step)) +                \
	 (BT_RAS_MAX_STEPS_PER_PROCEDURE * BT_RAS_MAX_STEP_DATA_LEN))

static K_SEM_DEFINE(sem_security, 0, 1);
static K_SEM_DEFINE(sem_mtu, 0, 1);
static K_SEM_DEFINE(sem_discovery, 0, 1);
static K_SEM_DEFINE(sem_caps, 0, 1);
static K_SEM_DEFINE(sem_config, 0, 1);
static K_SEM_DEFINE(sem_cs_sec, 0, 1);
static K_SEM_DEFINE(sem_proc_en, 0, 1);
static K_SEM_DEFINE(sem_local_steps, 1, 1);
static K_SEM_DEFINE(sem_estimate, 0, 1);

static struct bt_conn *active_conn;
static int discovery_err;
static float last_distance_m;
static uint16_t last_quality;
static int last_estimate_status;

static int32_t most_recent_local_ranging_counter = PROCEDURE_COUNTER_NONE;
static int32_t dropped_ranging_counter = PROCEDURE_COUNTER_NONE;

NET_BUF_SIMPLE_DEFINE_STATIC(latest_local_steps, LOCAL_PROCEDURE_MEM);
NET_BUF_SIMPLE_DEFINE_STATIC(latest_peer_steps, BT_RAS_PROCEDURE_MEM);

static void result_clear(struct juxta_range_result *out)
{
	out->distance_m = NAN;
	out->quality = JUXTA_RANGE_QUALITY_INVALID;
	out->status = -EIO;
}

static void ranging_data_get_complete_cb(struct bt_conn *conn, uint16_t ranging_counter, int err)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(ranging_counter);

	if (err) {
		LOG_ERR("Get ranging data failed (%d)", err);
		last_estimate_status = err;
		last_quality = JUXTA_RANGE_QUALITY_INVALID;
		last_distance_m = NAN;
		k_sem_give(&sem_estimate);
		return;
	}

	static cs_de_report_t cs_de_report;

	cs_de_populate_report(&latest_local_steps, &latest_peer_steps, BT_CONN_LE_CS_ROLE_INITIATOR,
			      &cs_de_report);
	net_buf_simple_reset(&latest_local_steps);
	net_buf_simple_reset(&latest_peer_steps);
	k_sem_give(&sem_local_steps);

	cs_de_quality_t quality = cs_de_calc(&cs_de_report);

	if (quality != CS_DE_QUALITY_OK) {
		LOG_WRN("CS DE quality not OK");
		last_estimate_status = -EIO;
		last_quality = JUXTA_RANGE_QUALITY_INVALID;
		last_distance_m = NAN;
		k_sem_give(&sem_estimate);
		return;
	}

	if (cs_de_report.n_ap < 1U ||
	    cs_de_report.tone_quality[0] != CS_DE_TONE_QUALITY_OK) {
		LOG_WRN("CS DE tone quality bad");
		last_estimate_status = -EIO;
		last_quality = JUXTA_RANGE_QUALITY_INVALID;
		last_distance_m = NAN;
		k_sem_give(&sem_estimate);
		return;
	}

	cs_de_dist_estimates_t *est = &cs_de_report.distance_estimates[0];

	if (isfinite(est->phase_slope)) {
		last_distance_m = est->phase_slope;
	} else if (isfinite(est->ifft)) {
		last_distance_m = est->ifft;
	} else if (isfinite(est->rtt)) {
		last_distance_m = est->rtt;
	} else {
		last_estimate_status = -EIO;
		last_quality = JUXTA_RANGE_QUALITY_INVALID;
		last_distance_m = NAN;
		k_sem_give(&sem_estimate);
		return;
	}

	last_quality = JUXTA_RANGE_QUALITY_OK;
	last_estimate_status = 0;
	LOG_INF("distance_m=%.3f (phase_slope=%.3f ifft=%.3f rtt=%.3f)", (double)last_distance_m,
		(double)est->phase_slope, (double)est->ifft, (double)est->rtt);
	k_sem_give(&sem_estimate);
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

	if (result->header.procedure_done_status == BT_CONN_LE_CS_PROCEDURE_ABORTED) {
		LOG_WRN("CS procedure aborted");
		net_buf_simple_reset(&latest_local_steps);
		k_sem_give(&sem_local_steps);
	}
}

static void ranging_data_ready_cb(struct bt_conn *conn, uint16_t ranging_counter)
{
	if (ranging_counter != most_recent_local_ranging_counter) {
		return;
	}

	int err = bt_ras_rreq_cp_get_ranging_data(conn, &latest_peer_steps, ranging_counter,
						  ranging_data_get_complete_cb);

	if (err) {
		LOG_ERR("bt_ras_rreq_cp_get_ranging_data failed (%d)", err);
		net_buf_simple_reset(&latest_local_steps);
		net_buf_simple_reset(&latest_peer_steps);
		k_sem_give(&sem_local_steps);
		last_estimate_status = err;
		last_quality = JUXTA_RANGE_QUALITY_INVALID;
		last_distance_m = NAN;
		k_sem_give(&sem_estimate);
	}
}

static void ranging_data_overwritten_cb(struct bt_conn *conn, uint16_t ranging_counter)
{
	ARG_UNUSED(conn);
	LOG_INF("Ranging data overwritten %u", ranging_counter);
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
	ARG_UNUSED(config);

	if (status == BT_HCI_ERR_SUCCESS) {
		k_sem_give(&sem_config);
	} else {
		LOG_WRN("CS config failed 0x%02x", status);
	}
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
	} else if (status != BT_HCI_ERR_SUCCESS) {
		LOG_WRN("CS procedures enable failed 0x%02x", status);
	}
}

BT_CONN_CB_DEFINE(juxta_range_conn_cb) = {
	.security_changed = security_changed,
	.le_cs_read_remote_capabilities_complete = remote_capabilities_cb,
	.le_cs_config_complete = config_create_cb,
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
	last_estimate_status = -EAGAIN;
	last_quality = JUXTA_RANGE_QUALITY_INVALID;
	last_distance_m = NAN;

	k_sem_reset(&sem_security);
	k_sem_reset(&sem_mtu);
	k_sem_reset(&sem_discovery);
	k_sem_reset(&sem_caps);
	k_sem_reset(&sem_config);
	k_sem_reset(&sem_cs_sec);
	k_sem_reset(&sem_proc_en);
	k_sem_reset(&sem_estimate);

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
		.enable_initiator_role = true,
		.enable_reflector_role = false,
		.cs_sync_antenna_selection = BT_LE_CS_ANTENNA_SELECTION_OPT_REPETITIVE,
		.max_tx_power = BT_HCI_OP_LE_CS_MAX_MAX_TX_POWER,
	};

	err = bt_le_cs_set_default_settings(conn, &default_settings);
	if (err) {
		LOG_ERR("CS default settings (%d)", err);
		out->status = err;
		return err;
	}

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

	struct bt_le_cs_create_config_params config_params = {
		.id = CS_CONFIG_ID,
		.main_mode_type = BT_CONN_LE_CS_MAIN_MODE_2,
		.sub_mode_type = BT_CONN_LE_CS_SUB_MODE_1,
		.min_main_mode_steps = 2,
		.max_main_mode_steps = 5,
		.main_mode_repetition = 0,
		.mode_0_steps = NUM_MODE_0_STEPS,
		.role = BT_CONN_LE_CS_ROLE_INITIATOR,
		.rtt_type = BT_CONN_LE_CS_RTT_TYPE_AA_ONLY,
		.cs_sync_phy = BT_CONN_LE_CS_SYNC_1M_PHY,
		.channel_map_repetition = 3,
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
		return err;
	}
	err = wait_sem(&sem_config, "cs config");
	if (err) {
		out->status = err;
		return err;
	}

	err = bt_le_cs_security_enable(conn);
	if (err) {
		LOG_ERR("CS security enable (%d)", err);
		out->status = err;
		return err;
	}
	err = wait_sem(&sem_cs_sec, "cs security");
	if (err) {
		out->status = err;
		return err;
	}

	const struct bt_le_cs_set_procedure_parameters_param procedure_params = {
		.config_id = CS_CONFIG_ID,
		.max_procedure_len = 1000,
		.min_procedure_interval = 10,
		.max_procedure_interval = 10,
		.max_procedure_count = 1,
		.min_subevent_len = 60000,
		.max_subevent_len = 60000,
		.tone_antenna_config_selection = BT_LE_CS_TONE_ANTENNA_CONFIGURATION_A1_B1,
		.phy = BT_LE_CS_PROCEDURE_PHY_1M,
		.tx_power_delta = 0x80,
		.preferred_peer_antenna = BT_LE_CS_PROCEDURE_PREFERRED_PEER_ANTENNA_1,
		.snr_control_initiator = BT_LE_CS_SNR_CONTROL_NOT_USED,
		.snr_control_reflector = BT_LE_CS_SNR_CONTROL_NOT_USED,
	};

	err = bt_le_cs_set_procedure_parameters(conn, &procedure_params);
	if (err) {
		LOG_ERR("CS procedure parameters (%d)", err);
		out->status = err;
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
		return err;
	}

	err = wait_sem(&sem_estimate, "distance estimate");
	out->distance_m = last_distance_m;
	out->quality = last_quality;
	out->status = (err != 0) ? err : last_estimate_status;

	(void)bt_ras_rreq_free(conn);
	active_conn = NULL;
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
		.enable_initiator_role = false,
		.enable_reflector_role = true,
		.cs_sync_antenna_selection = BT_LE_CS_ANTENNA_SELECTION_OPT_REPETITIVE,
		.max_tx_power = BT_HCI_OP_LE_CS_MAX_MAX_TX_POWER,
	};

	err = bt_le_cs_set_default_settings(conn, &default_settings);
	if (err) {
		LOG_ERR("CS reflector default settings (%d)", err);
		out->status = err;
		return err;
	}

	LOG_INF("Reflector armed — waiting for CS procedures (up to %u ms)", REFLECTOR_WAIT_MS);
	err = k_sem_take(&sem_proc_en, K_MSEC(REFLECTOR_WAIT_MS));
	if (err != 0) {
		LOG_WRN("Reflector: no procedure-enable observed (%d)", err);
		out->status = -ETIMEDOUT;
		out->quality = JUXTA_RANGE_QUALITY_INVALID;
		out->distance_m = NAN;
		return -ETIMEDOUT;
	}

	/* Allow the initiator procedure to finish; distance is computed on initiator. */
	k_sleep(K_MSEC(5000));

	out->distance_m = NAN;
	out->quality = JUXTA_RANGE_QUALITY_REFLECTOR;
	out->status = 0;
	LOG_INF("Reflector participation complete (no local distance)");
	return 0;
}
