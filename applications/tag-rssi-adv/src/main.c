/*
 * Connectionless dual-antenna advertising RSSI HIL.
 *
 * One image, two roles (BTN1 release + 40 ms debounce toggles):
 *   - advertiser (LED1 red): non-connectable identity adv with sequenced mfg data
 *   - scanner (LED1 blue idle / green when peer heard): same dual-antenna burst
 *     as juxta6-0-prod (500 ms ANT1 + 500 ms ANT2); RTT logs one strongest RSSI
 *     per peer per burst (no HCI duplicate filter)
 *
 * Advertiser stays on ANT1. TX: CONFIG_BT_CTLR_TX_PWR_ANTENNA=+8 dBm.
 * Manufacturer company ID 0xFFFF is Bluetooth SIG development/test only.
 *
 * No Channel Sounding, RAS, cs_de, or nrf_dm.
 */

#include <stdio.h>
#include <string.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net_buf.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

#include "juxta_id.h"

LOG_MODULE_REGISTER(tag_rssi_adv, LOG_LEVEL_INF);

#define SEQ_UPDATE_MS 150U
/* Match juxta6-0-prod production scan burst (two antenna dwells). */
#define PEER_SLOT_COUNT 8U
#define ANT_DWELL_MS 500U
#define SCAN_BURST_MS (ANT_DWELL_MS * 2U)
#define SCAN_PERIOD_MS SCAN_BURST_MS
/* Role toggle: act on release, then settle before committing. */
#define BTN_DEBOUNCE_MS 40U

BUILD_ASSERT(ANT_DWELL_MS * 2U == SCAN_BURST_MS, "scan burst must be two antenna dwells");

/* Development/test Company ID (not a Juxta product CID). */
#define MFG_COMPANY_ID 0xFFFFU
#define MFG_MAGIC0 'J'
#define MFG_MAGIC1 'X'
#define MFG_TX_ANT_ADVERTISER 1U

#define MFG_LEN 7U

#if !DT_NODE_EXISTS(DT_NODELABEL(led1_red)) || !DT_NODE_EXISTS(DT_NODELABEL(led1_blue)) ||       \
	!DT_NODE_EXISTS(DT_NODELABEL(led1_green))
#error "Board must define led1_red, led1_green, and led1_blue"
#endif

#if !DT_NODE_EXISTS(DT_ALIAS(sw0))
#error "Board must define alias sw0"
#endif

#if !DT_NODE_EXISTS(DT_NODELABEL(sky13348))
#error "Board must define sky13348 (SKY13348 antenna switch)"
#endif

static const struct gpio_dt_spec led_r = GPIO_DT_SPEC_GET(DT_NODELABEL(led1_red), gpios);
static const struct gpio_dt_spec led_g = GPIO_DT_SPEC_GET(DT_NODELABEL(led1_green), gpios);
static const struct gpio_dt_spec led_b = GPIO_DT_SPEC_GET(DT_NODELABEL(led1_blue), gpios);
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
static const struct gpio_dt_spec ant_v1 = GPIO_DT_SPEC_GET(DT_NODELABEL(sky13348), v1_gpios);
static const struct gpio_dt_spec ant_v2 = GPIO_DT_SPEC_GET(DT_NODELABEL(sky13348), v2_gpios);

static struct gpio_callback button_cb_data;

static char local_name[JUXTA_ID_LEN];

enum role {
	ROLE_ADVERTISER = 0,
	ROLE_SCANNER = 1,
};

struct peer_slot {
	char name[JUXTA_ID_LEN];
	int8_t best_rssi;
	bool active;
};

static atomic_t role = ATOMIC_INIT(ROLE_ADVERTISER);
static atomic_t rx_ant = ATOMIC_INIT(1); /* 1 = ANT1, 2 = ANT2 */
static atomic_t busy_switch;

static struct peer_slot peers[PEER_SLOT_COUNT];
static struct k_mutex peers_lock;

static uint16_t adv_seq;
static uint8_t mfg_payload[MFG_LEN];

static struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_NO_BREDR),
	BT_DATA(BT_DATA_NAME_COMPLETE, local_name, 0),
	BT_DATA(BT_DATA_MANUFACTURER_DATA, mfg_payload, sizeof(mfg_payload)),
};

static struct k_work role_work;
static struct k_work_delayable seq_work;
static struct k_work_delayable scan_cycle_work;
static struct k_work_delayable scan_burst_end_work;
static struct k_work_delayable scan_ant_switch_work;
static struct k_work_delayable btn_debounce_work;

static void leds_off(void)
{
	(void)gpio_pin_set_dt(&led_r, 0);
	(void)gpio_pin_set_dt(&led_g, 0);
	(void)gpio_pin_set_dt(&led_b, 0);
}

static void led_show_role(void)
{
	leds_off();
	if ((int)atomic_get(&role) == ROLE_ADVERTISER) {
		(void)gpio_pin_set_dt(&led_r, 1);
	} else {
		/* Scanner idle / no peer yet — blue until a burst reports a hit. */
		(void)gpio_pin_set_dt(&led_b, 1);
	}
}

/** Sticky scan result: green = peer heard this burst, blue = none. */
static void led_show_scan_result(bool peer_found)
{
	leds_off();
	if (peer_found) {
		(void)gpio_pin_set_dt(&led_g, 1);
	} else {
		(void)gpio_pin_set_dt(&led_b, 1);
	}
}

/**
 * Select RF path. Never leave V1 and V2 both inactive (undefined IL).
 * Break-before-make: drive the new high first, then clear the other.
 */
static int antenna_select(uint8_t ant)
{
	int err;

	if (ant == 1U) {
		err = gpio_pin_set_dt(&ant_v1, 1);
		if (err) {
			return err;
		}
		err = gpio_pin_set_dt(&ant_v2, 0);
		if (err) {
			return err;
		}
		atomic_set(&rx_ant, 1);
	} else if (ant == 2U) {
		err = gpio_pin_set_dt(&ant_v2, 1);
		if (err) {
			return err;
		}
		err = gpio_pin_set_dt(&ant_v1, 0);
		if (err) {
			return err;
		}
		atomic_set(&rx_ant, 2);
	} else {
		return -EINVAL;
	}

	return 0;
}

static int antenna_init(void)
{
	int err;

	if (!gpio_is_ready_dt(&ant_v1) || !gpio_is_ready_dt(&ant_v2)) {
		LOG_ERR("Antenna GPIO not ready (is the hog-delete overlay applied?)");
		return -ENODEV;
	}

	err = gpio_pin_configure_dt(&ant_v1, GPIO_OUTPUT_ACTIVE);
	if (err) {
		return err;
	}
	err = gpio_pin_configure_dt(&ant_v2, GPIO_OUTPUT_INACTIVE);
	if (err) {
		return err;
	}

	return antenna_select(1);
}

static void mfg_pack(uint16_t seq, uint8_t tx_ant)
{
	sys_put_le16(MFG_COMPANY_ID, &mfg_payload[0]);
	mfg_payload[2] = MFG_MAGIC0;
	mfg_payload[3] = MFG_MAGIC1;
	sys_put_le16(seq, &mfg_payload[4]);
	mfg_payload[6] = tx_ant;
}

static void extract_name(struct net_buf_simple *ad_buf, char *dev_name, size_t name_sz)
{
	struct net_buf_simple_state state;

	memset(dev_name, 0, name_sz);
	if (ad_buf == NULL || ad_buf->len == 0U || name_sz == 0U) {
		return;
	}

	net_buf_simple_save(ad_buf, &state);
	while (ad_buf->len > 1U) {
		uint8_t flen = net_buf_simple_pull_u8(ad_buf);

		if (flen == 0U || flen > ad_buf->len) {
			break;
		}

		uint8_t ftype = net_buf_simple_pull_u8(ad_buf);

		flen--;
		if (flen > ad_buf->len) {
			break;
		}
		if ((ftype == BT_DATA_NAME_COMPLETE || ftype == BT_DATA_NAME_SHORTENED) &&
		    flen < name_sz) {
			memcpy(dev_name, ad_buf->data, flen);
			dev_name[flen] = '\0';
		}
		net_buf_simple_pull(ad_buf, flen);
	}
	net_buf_simple_restore(ad_buf, &state);
}

/** Keep the strongest RSSI seen for each peer name across both antennas (prod). */
static void peer_note_max(const char *name, int8_t rssi)
{
	int free_idx = -1;

	(void)k_mutex_lock(&peers_lock, K_FOREVER);
	for (int i = 0; i < (int)PEER_SLOT_COUNT; i++) {
		if (peers[i].active && strcmp(peers[i].name, name) == 0) {
			if (rssi > peers[i].best_rssi) {
				peers[i].best_rssi = rssi;
			}
			(void)k_mutex_unlock(&peers_lock);
			return;
		}
		if (!peers[i].active && free_idx < 0) {
			free_idx = i;
		}
	}
	if (free_idx >= 0) {
		peers[free_idx].active = true;
		(void)snprintf(peers[free_idx].name, sizeof(peers[free_idx].name), "%s", name);
		peers[free_idx].best_rssi = rssi;
	}
	(void)k_mutex_unlock(&peers_lock);
}

static void peers_clear(void)
{
	(void)k_mutex_lock(&peers_lock, K_FOREVER);
	memset(peers, 0, sizeof(peers));
	(void)k_mutex_unlock(&peers_lock);
}

/** One RTT line per peer with max RSSI; returns true if any peer was present. */
static bool peers_flush_rtt(void)
{
	bool any = false;

	(void)k_mutex_lock(&peers_lock, K_FOREVER);
	for (int i = 0; i < (int)PEER_SLOT_COUNT; i++) {
		if (peers[i].active) {
			any = true;
			LOG_INF("rssi_pkt id=%s rssi=%d uptime_ms=%u", peers[i].name,
				(int)peers[i].best_rssi, k_uptime_get_32());
		}
	}
	(void)k_mutex_unlock(&peers_lock);
	return any;
}

static void advertising_stop(void)
{
	(void)k_work_cancel_delayable(&seq_work);
	(void)bt_le_adv_stop();
}

static void scan_works_cancel(void)
{
	(void)k_work_cancel_delayable(&scan_cycle_work);
	(void)k_work_cancel_delayable(&scan_burst_end_work);
	(void)k_work_cancel_delayable(&scan_ant_switch_work);
}

static void scan_stop(void)
{
	scan_works_cancel();
	(void)bt_le_scan_stop();
}

static void seq_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	if ((int)atomic_get(&role) != ROLE_ADVERTISER) {
		return;
	}

	adv_seq++;
	mfg_pack(adv_seq, MFG_TX_ANT_ADVERTISER);
	ad[1].data_len = (uint8_t)strlen(local_name);

	int err = bt_le_adv_update_data(ad, ARRAY_SIZE(ad), NULL, 0);

	if (err && err != -EAGAIN) {
		LOG_WRN("adv update failed (%d)", err);
	}

	(void)k_work_schedule(&seq_work, K_MSEC(SEQ_UPDATE_MS));
}

static int advertising_start(void)
{
	/* Fast advertising (30–60 ms) for denser packets at the range edge. */
	static const struct bt_le_adv_param adv_param = {
		.id = BT_ID_DEFAULT,
		.options = BT_LE_ADV_OPT_USE_IDENTITY,
		.interval_min = BT_GAP_ADV_FAST_INT_MIN_1,
		.interval_max = BT_GAP_ADV_FAST_INT_MAX_1,
	};
	int err;

	err = antenna_select(1);
	if (err) {
		return err;
	}

	adv_seq = 0;
	mfg_pack(adv_seq, MFG_TX_ANT_ADVERTISER);
	ad[1].data_len = (uint8_t)strlen(local_name);

	err = bt_le_adv_start(&adv_param, ad, ARRAY_SIZE(ad), NULL, 0);
	if (err && err != -EALREADY) {
		LOG_ERR("Advertising failed (%d)", err);
		return err;
	}

	LOG_INF("role=advertiser id=%s ant=1 tx=+8dBm (red)", local_name);
	(void)k_work_schedule(&seq_work, K_MSEC(SEQ_UPDATE_MS));
	return 0;
}

static void scan_ant_switch_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	if ((int)atomic_get(&role) != ROLE_SCANNER) {
		return;
	}

	int err = antenna_select(2);

	if (err) {
		LOG_WRN("antenna_select(2) failed (%d)", err);
	}
}

static void scan_burst_end_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	if ((int)atomic_get(&role) != ROLE_SCANNER) {
		return;
	}

	(void)k_work_cancel_delayable(&scan_ant_switch_work);
	(void)bt_le_scan_stop();
	(void)antenna_select(1);

	bool found = peers_flush_rtt();

	led_show_scan_result(found);
	if (!found) {
		LOG_INF("scan_burst done found=0");
	}
}

static void scan_cycle_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	if ((int)atomic_get(&role) != ROLE_SCANNER) {
		return;
	}

	/* No FILTER_DUPLICATE: need both antenna sightings to pick max RSSI. */
	struct bt_le_scan_param scan_param = {
		.type = BT_LE_SCAN_TYPE_PASSIVE,
		.options = BT_LE_SCAN_OPT_NONE,
		.interval = BT_GAP_SCAN_FAST_INTERVAL,
		.window = BT_GAP_SCAN_FAST_WINDOW,
	};
	int err;

	peers_clear();

	err = antenna_select(1);
	if (err) {
		LOG_WRN("antenna_select(1) failed (%d)", err);
		(void)k_work_schedule(&scan_cycle_work, K_MSEC(SCAN_PERIOD_MS));
		return;
	}

	err = bt_le_scan_start(&scan_param, NULL);
	if (err && err != -EALREADY) {
		LOG_WRN("scan start failed (%d)", err);
		(void)k_work_schedule(&scan_cycle_work, K_MSEC(SCAN_PERIOD_MS));
		return;
	}

	(void)k_work_schedule(&scan_ant_switch_work, K_MSEC(ANT_DWELL_MS));
	(void)k_work_schedule(&scan_burst_end_work, K_MSEC(SCAN_BURST_MS));
	(void)k_work_schedule(&scan_cycle_work, K_MSEC(SCAN_PERIOD_MS));
}

static void scan_recv(const struct bt_le_scan_recv_info *info, struct net_buf_simple *buf)
{
	char name[JUXTA_ID_LEN];

	if ((int)atomic_get(&role) != ROLE_SCANNER) {
		return;
	}

	extract_name(buf, name, sizeof(name));
	if (!juxta_id_is_peer_name(name, local_name)) {
		return;
	}

	peer_note_max(name, info->rssi);
}

static struct bt_le_scan_cb scan_callbacks = {
	.recv = scan_recv,
};

static int scanning_start(void)
{
	LOG_INF("role=scanner local=%s dwell=%ums×2 (blue idle / green on peer; max RSSI/RTT)",
		local_name, ANT_DWELL_MS);
	led_show_role();
	(void)k_work_schedule(&scan_cycle_work, K_NO_WAIT);
	return 0;
}

static void role_apply(void)
{
	if (!atomic_cas(&busy_switch, 0, 1)) {
		return;
	}

	advertising_stop();
	scan_stop();
	/* Brief settle after radio stop before antenna/role change. */
	k_sleep(K_MSEC(20));

	led_show_role();

	int err;

	if ((int)atomic_get(&role) == ROLE_ADVERTISER) {
		err = advertising_start();
	} else {
		err = scanning_start();
	}

	if (err) {
		LOG_ERR("role apply failed (%d)", err);
	}

	atomic_set(&busy_switch, 0);
}

static void role_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	role_apply();
}

static bool button_active(void)
{
	return gpio_pin_get_dt(&button) > 0;
}

/** Commit role toggle only after release has been stable for BTN_DEBOUNCE_MS. */
static void btn_debounce_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	if (button_active()) {
		return;
	}

	int next = ((int)atomic_get(&role) == ROLE_ADVERTISER) ? ROLE_SCANNER : ROLE_ADVERTISER;

	atomic_set(&role, next);
	LOG_INF("role_override=%s", next == ROLE_ADVERTISER ? "advertiser" : "scanner");
	(void)k_work_submit(&role_work);
}

/**
 * BTN1: ignore press edges; on release start (or restart) debounce.
 * Bounces while held cancel any pending commit.
 */
static void button_isr(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

	if (button_active()) {
		(void)k_work_cancel_delayable(&btn_debounce_work);
		return;
	}

	(void)k_work_reschedule(&btn_debounce_work, K_MSEC(BTN_DEBOUNCE_MS));
}

int main(void)
{
	int err;

	k_mutex_init(&peers_lock);
	k_work_init(&role_work, role_work_handler);
	k_work_init_delayable(&seq_work, seq_work_handler);
	k_work_init_delayable(&scan_cycle_work, scan_cycle_handler);
	k_work_init_delayable(&scan_burst_end_work, scan_burst_end_handler);
	k_work_init_delayable(&scan_ant_switch_work, scan_ant_switch_handler);
	k_work_init_delayable(&btn_debounce_work, btn_debounce_handler);

	if (!gpio_is_ready_dt(&led_r) || !gpio_is_ready_dt(&led_g) ||
	    !gpio_is_ready_dt(&led_b) || !gpio_is_ready_dt(&button)) {
		LOG_ERR("GPIO not ready");
		return -ENODEV;
	}

	err = gpio_pin_configure_dt(&led_r, GPIO_OUTPUT_INACTIVE);
	if (err) {
		return err;
	}
	err = gpio_pin_configure_dt(&led_g, GPIO_OUTPUT_INACTIVE);
	if (err) {
		return err;
	}
	err = gpio_pin_configure_dt(&led_b, GPIO_OUTPUT_INACTIVE);
	if (err) {
		return err;
	}
	err = gpio_pin_configure_dt(&button, GPIO_INPUT);
	if (err) {
		return err;
	}
	err = gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_BOTH);
	if (err) {
		return err;
	}
	gpio_init_callback(&button_cb_data, button_isr, BIT(button.pin));
	err = gpio_add_callback(button.port, &button_cb_data);
	if (err) {
		return err;
	}

	err = antenna_init();
	if (err) {
		LOG_ERR("antenna_init (%d)", err);
		return err;
	}

	err = bt_enable(NULL);
	if (err) {
		LOG_ERR("bt_enable (%d)", err);
		return err;
	}

	err = juxta_id_fill_from_bt(local_name, sizeof(local_name));
	if (err) {
		return err;
	}

	bt_le_scan_cb_register(&scan_callbacks);

	LOG_INF("tag-rssi-adv started local_id=%s tx_pwr_antenna=+8dBm", local_name);
	LOG_INF("BTN1 (release+%u ms) toggles advertiser(red) | scanner(blue/green)",
		BTN_DEBOUNCE_MS);
	LOG_INF("Scanner: %ums ANT1 + %ums ANT2; RTT max RSSI/peer; no dup filter; CID=0x%04X",
		ANT_DWELL_MS, ANT_DWELL_MS, MFG_COMPANY_ID);

	role_apply();

	while (1) {
		k_sleep(K_SECONDS(60));
	}

	return 0;
}
