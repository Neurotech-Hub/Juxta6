/*
 * Connectionless dual-antenna advertising RSSI HIL.
 *
 * One image, two roles (BTN1 toggles):
 *   - advertiser (LED1 red): non-connectable identity adv with sequenced mfg data
 *   - scanner (LED1 blue): passive scan, no duplicate filter; logs every JX_ packet
 *
 * Scanner time-multiplexes SKY13348 ANT1/ANT2. Advertiser stays on ANT1.
 * Manufacturer company ID 0xFFFF is Bluetooth SIG development/test only.
 *
 * No Channel Sounding, RAS, cs_de, or nrf_dm.
 */

#include <ctype.h>
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
#define ANT_DWELL_MS 500U

/* Development/test Company ID (not a Juxta product CID). */
#define MFG_COMPANY_ID 0xFFFFU
#define MFG_MAGIC0 'J'
#define MFG_MAGIC1 'X'
#define MFG_TX_ANT_ADVERTISER 1U

#define MFG_LEN 7U

#if !DT_NODE_EXISTS(DT_NODELABEL(led1_red)) || !DT_NODE_EXISTS(DT_NODELABEL(led1_blue))
#error "Board must define led1_red and led1_blue"
#endif

#if !DT_NODE_EXISTS(DT_ALIAS(sw0))
#error "Board must define alias sw0"
#endif

#if !DT_NODE_EXISTS(DT_NODELABEL(sky13348))
#error "Board must define sky13348 (SKY13348 antenna switch)"
#endif

static const struct gpio_dt_spec led_r = GPIO_DT_SPEC_GET(DT_NODELABEL(led1_red), gpios);
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

static atomic_t role = ATOMIC_INIT(ROLE_ADVERTISER);
static atomic_t rx_ant = ATOMIC_INIT(1); /* 1 = ANT1, 2 = ANT2 */
static atomic_t busy_switch;

static uint16_t adv_seq;
static uint8_t mfg_payload[MFG_LEN];

static struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_NO_BREDR),
	BT_DATA(BT_DATA_NAME_COMPLETE, local_name, 0),
	BT_DATA(BT_DATA_MANUFACTURER_DATA, mfg_payload, sizeof(mfg_payload)),
};

static struct k_work role_work;
static struct k_work_delayable seq_work;
static struct k_work_delayable ant_mux_work;

static void leds_off(void)
{
	(void)gpio_pin_set_dt(&led_r, 0);
	(void)gpio_pin_set_dt(&led_b, 0);
}

static void led_show_role(void)
{
	leds_off();
	if ((int)atomic_get(&role) == ROLE_ADVERTISER) {
		(void)gpio_pin_set_dt(&led_r, 1);
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

static bool parse_mfg(struct net_buf_simple *ad_buf, uint16_t *seq_out, uint8_t *tx_ant_out)
{
	struct net_buf_simple_state state;
	bool found = false;

	if (ad_buf == NULL || ad_buf->len == 0U) {
		return false;
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

		if (ftype == BT_DATA_MANUFACTURER_DATA && flen >= MFG_LEN) {
			const uint8_t *p = ad_buf->data;
			uint16_t cid = sys_get_le16(p);

			if (cid == MFG_COMPANY_ID && p[2] == MFG_MAGIC0 && p[3] == MFG_MAGIC1) {
				*seq_out = sys_get_le16(&p[4]);
				*tx_ant_out = p[6];
				found = true;
			}
		}
		net_buf_simple_pull(ad_buf, flen);
	}
	net_buf_simple_restore(ad_buf, &state);
	return found;
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

static void advertising_stop(void)
{
	(void)k_work_cancel_delayable(&seq_work);
	(void)bt_le_adv_stop();
}

static void scan_stop(void)
{
	(void)k_work_cancel_delayable(&ant_mux_work);
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
	int err;

	err = antenna_select(1);
	if (err) {
		return err;
	}

	adv_seq = 0;
	mfg_pack(adv_seq, MFG_TX_ANT_ADVERTISER);
	ad[1].data_len = (uint8_t)strlen(local_name);

	err = bt_le_adv_start(BT_LE_ADV_NCONN_IDENTITY, ad, ARRAY_SIZE(ad), NULL, 0);
	if (err && err != -EALREADY) {
		LOG_ERR("Advertising failed (%d)", err);
		return err;
	}

	LOG_INF("role=advertiser id=%s ant=1 (red)", local_name);
	(void)k_work_schedule(&seq_work, K_MSEC(SEQ_UPDATE_MS));
	return 0;
}

static void ant_mux_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	if ((int)atomic_get(&role) != ROLE_SCANNER) {
		return;
	}

	uint8_t next = (atomic_get(&rx_ant) == 1) ? 2U : 1U;
	int err = antenna_select(next);

	if (err) {
		LOG_WRN("antenna_select(%u) failed (%d)", next, err);
	} else {
		LOG_DBG("rx_ant=%u", next);
	}

	(void)k_work_schedule(&ant_mux_work, K_MSEC(ANT_DWELL_MS));
}

static void scan_recv(const struct bt_le_scan_recv_info *info, struct net_buf_simple *buf)
{
	char name[JUXTA_ID_LEN];
	uint16_t seq = 0;
	uint8_t tx_ant = 0;

	if ((int)atomic_get(&role) != ROLE_SCANNER) {
		return;
	}

	extract_name(buf, name, sizeof(name));
	if (!juxta_id_is_peer_name(name, local_name)) {
		return;
	}

	if (!parse_mfg(buf, &seq, &tx_ant)) {
		/* Still log name-only peers without mfg (older images). */
		LOG_INF("rssi_pkt id=%s seq=- rssi=%d rx_ant=%d tx_ant=- uptime_ms=%u", name,
			(int)info->rssi, (int)atomic_get(&rx_ant), k_uptime_get_32());
		return;
	}

	LOG_INF("rssi_pkt id=%s seq=%u rssi=%d rx_ant=%d tx_ant=%u uptime_ms=%u", name, seq,
		(int)info->rssi, (int)atomic_get(&rx_ant), tx_ant, k_uptime_get_32());
}

static struct bt_le_scan_cb scan_callbacks = {
	.recv = scan_recv,
};

static int scanning_start(void)
{
	struct bt_le_scan_param scan_param = {
		.type = BT_LE_SCAN_TYPE_PASSIVE,
		.options = BT_LE_SCAN_OPT_NONE,
		.interval = BT_GAP_SCAN_FAST_INTERVAL,
		.window = BT_GAP_SCAN_FAST_WINDOW,
	};
	int err;

	err = antenna_select(1);
	if (err) {
		return err;
	}

	err = bt_le_scan_start(&scan_param, NULL);
	if (err && err != -EALREADY) {
		LOG_ERR("scan start failed (%d)", err);
		return err;
	}

	LOG_INF("role=scanner local=%s ant_dwell_ms=%u (blue)", local_name, ANT_DWELL_MS);
	(void)k_work_schedule(&ant_mux_work, K_MSEC(ANT_DWELL_MS));
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

static void button_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

	int next = ((int)atomic_get(&role) == ROLE_ADVERTISER) ? ROLE_SCANNER : ROLE_ADVERTISER;

	atomic_set(&role, next);
	LOG_INF("role_override=%s", next == ROLE_ADVERTISER ? "advertiser" : "scanner");
	(void)k_work_submit(&role_work);
}

int main(void)
{
	int err;

	k_work_init(&role_work, role_work_handler);
	k_work_init_delayable(&seq_work, seq_work_handler);
	k_work_init_delayable(&ant_mux_work, ant_mux_work_handler);

	if (!gpio_is_ready_dt(&led_r) || !gpio_is_ready_dt(&led_b) || !gpio_is_ready_dt(&button)) {
		LOG_ERR("GPIO not ready");
		return -ENODEV;
	}

	err = gpio_pin_configure_dt(&led_r, GPIO_OUTPUT_INACTIVE);
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
	err = gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE);
	if (err) {
		return err;
	}
	gpio_init_callback(&button_cb_data, button_pressed, BIT(button.pin));
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

	LOG_INF("tag-rssi-adv started local_id=%s", local_name);
	LOG_INF("BTN1 toggles advertiser(red) | scanner(blue)");
	LOG_INF("Scanner RX antenna mux ANT1/ANT2 every %u ms; mfg CID=0x%04X (dev/test)",
		ANT_DWELL_MS, MFG_COMPANY_ID);

	role_apply();

	while (1) {
		k_sleep(K_SECONDS(60));
	}

	return 0;
}
