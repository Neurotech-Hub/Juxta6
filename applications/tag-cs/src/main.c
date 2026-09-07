/*
 * Mobile↔mobile Channel Sounding HIL — one image, both roles.
 *
 * Arbitration: lower JX_ name initiates (connects as central).
 * BTN1 short press cycles role override: auto -> force_initiator ->
 * force_reflector -> auto. Lets either physical tag exercise both CS roles.
 *
 * Nordic CS/RAS calls live only in lib/juxta_range.
 */

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net_buf.h>
#include <zephyr/sys/atomic.h>

#include <bluetooth/services/ras.h>

#include "juxta_id.h"
#include "juxta_range.h"

LOG_MODULE_REGISTER(tag_cs, LOG_LEVEL_INF);

#define SCAN_WINDOW_MS 400U
#define SCAN_CYCLE_MS 800U
#define COOLDOWN_MS 3000U

#if !DT_NODE_EXISTS(DT_NODELABEL(led1_red)) || !DT_NODE_EXISTS(DT_NODELABEL(led1_green)) ||       \
	!DT_NODE_EXISTS(DT_NODELABEL(led1_blue))
#error "Board must define led1_red/green/blue"
#endif

#if !DT_NODE_EXISTS(DT_ALIAS(sw0))
#error "Board must define alias sw0"
#endif

static const struct gpio_dt_spec led_r = GPIO_DT_SPEC_GET(DT_NODELABEL(led1_red), gpios);
static const struct gpio_dt_spec led_g = GPIO_DT_SPEC_GET(DT_NODELABEL(led1_green), gpios);
static const struct gpio_dt_spec led_b = GPIO_DT_SPEC_GET(DT_NODELABEL(led1_blue), gpios);
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);

static struct gpio_callback button_cb_data;

static char local_name[JUXTA_ID_LEN];
static char peer_name[JUXTA_ID_LEN];
static bt_addr_le_t peer_addr;
static atomic_t peer_ready;
static atomic_t busy;

enum role_override {
	ROLE_AUTO = 0,
	ROLE_FORCE_INITIATOR,
	ROLE_FORCE_REFLECTOR,
};

static atomic_t role_override = ATOMIC_INIT(ROLE_AUTO);

static struct bt_conn *default_conn;
static K_SEM_DEFINE(sem_connected, 0, 1);
static K_SEM_DEFINE(sem_disconnected, 0, 1);

static struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID16_ALL, BT_UUID_16_ENCODE(BT_UUID_RANGING_SERVICE_VAL)),
	BT_DATA(BT_DATA_NAME_COMPLETE, local_name, 0),
};

static void leds_off(void)
{
	(void)gpio_pin_set_dt(&led_r, 0);
	(void)gpio_pin_set_dt(&led_g, 0);
	(void)gpio_pin_set_dt(&led_b, 0);
}

static void led_role_initiator(void)
{
	leds_off();
	(void)gpio_pin_set_dt(&led_g, 1);
}

static void led_role_reflector(void)
{
	leds_off();
	(void)gpio_pin_set_dt(&led_b, 1);
}

static void led_fail(void)
{
	leds_off();
	(void)gpio_pin_set_dt(&led_r, 1);
}

static void extract_name(struct net_buf_simple *ad_buf, char *dev_name, size_t name_sz)
{
	struct net_buf_simple_state state;

	memset(dev_name, 0, name_sz);
	if (ad_buf == NULL || ad_buf->len == 0U) {
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

static bool we_should_initiate(const char *peer)
{
	int ov = (int)atomic_get(&role_override);

	if (ov == ROLE_FORCE_INITIATOR) {
		return true;
	}
	if (ov == ROLE_FORCE_REFLECTOR) {
		return false;
	}
	/* auto: lower ID initiates */
	return juxta_id_cmp(local_name, peer) < 0;
}

static void button_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

	int ov = (int)atomic_get(&role_override);

	ov = (ov + 1) % 3;
	atomic_set(&role_override, ov);
	LOG_INF("role_override=%s",
		ov == ROLE_AUTO	 ? "auto(lower_id_initiates)"
		: ov == ROLE_FORCE_INITIATOR ? "force_initiator"
					     : "force_reflector");
}

static void scan_recv(const bt_addr_le_t *addr, int8_t rssi, uint8_t type,
		      struct net_buf_simple *buf)
{
	char name[JUXTA_ID_LEN];

	ARG_UNUSED(rssi);
	ARG_UNUSED(type);

	if (atomic_get(&busy) != 0 || atomic_get(&peer_ready) != 0) {
		return;
	}

	extract_name(buf, name, sizeof(name));
	if (!juxta_id_is_peer_name(name, local_name)) {
		return;
	}

	bt_addr_le_copy(&peer_addr, addr);
	(void)snprintf(peer_name, sizeof(peer_name), "%s", name);
	atomic_set(&peer_ready, 1);
	LOG_INF("peer_seen id=%s rssi=%d", name, (int)rssi);
}

static void connected_cb(struct bt_conn *conn, uint8_t err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	if (err) {
		LOG_ERR("Connect failed %s err=0x%02x", addr, err);
		return;
	}

	LOG_INF("Connected %s", addr);
	/* Inbound (reflector) path: take a ref. Initiator already holds create ref. */
	if (default_conn == NULL) {
		default_conn = bt_conn_ref(conn);
	}
	k_sem_give(&sem_connected);
}

static void disconnected_cb(struct bt_conn *conn, uint8_t reason)
{
	ARG_UNUSED(conn);

	LOG_INF("Disconnected reason=0x%02x", reason);
	if (default_conn) {
		bt_conn_unref(default_conn);
		default_conn = NULL;
	}
	k_sem_give(&sem_disconnected);
}

BT_CONN_CB_DEFINE(tag_cs_conn_cb) = {
	.connected = connected_cb,
	.disconnected = disconnected_cb,
};

static int advertising_start(void)
{
	ad[2].data_len = (uint8_t)strlen(local_name);

	int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_2, ad, ARRAY_SIZE(ad), NULL, 0);

	if (err && err != -EALREADY) {
		LOG_ERR("adv start failed (%d)", err);
		return err;
	}
	LOG_INF("Advertising as %s (RAS UUID)", local_name);
	return 0;
}

static void advertising_stop(void)
{
	(void)bt_le_adv_stop();
}

static int scan_burst(void)
{
	struct bt_le_scan_param scan_param = {
		.type = BT_LE_SCAN_TYPE_PASSIVE,
		.options = BT_LE_SCAN_OPT_FILTER_DUPLICATE,
		.interval = BT_GAP_SCAN_FAST_INTERVAL,
		.window = BT_GAP_SCAN_FAST_WINDOW,
	};
	int err = bt_le_scan_start(&scan_param, scan_recv);

	if (err) {
		return err;
	}
	k_sleep(K_MSEC(SCAN_WINDOW_MS));
	(void)bt_le_scan_stop();
	return 0;
}

static void run_as_initiator(void)
{
	struct juxta_range_result result;
	struct bt_conn *conn = NULL;
	int err;

	atomic_set(&busy, 1);
	led_role_initiator();
	LOG_INF("role=initiator local=%s peer=%s", local_name, peer_name);

	advertising_stop();
	(void)bt_le_scan_stop();
	k_sem_reset(&sem_connected);
	k_sem_reset(&sem_disconnected);

	err = bt_conn_le_create(&peer_addr, BT_CONN_LE_CREATE_CONN, BT_LE_CONN_PARAM_DEFAULT,
				&conn);
	if (err) {
		LOG_ERR("bt_conn_le_create (%d)", err);
		led_fail();
		atomic_set(&busy, 0);
		atomic_set(&peer_ready, 0);
		(void)advertising_start();
		return;
	}

	default_conn = conn;

	err = k_sem_take(&sem_connected, K_SECONDS(10));
	if (err) {
		LOG_ERR("connect timeout");
		bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
		bt_conn_unref(conn);
		default_conn = NULL;
		led_fail();
		atomic_set(&busy, 0);
		atomic_set(&peer_ready, 0);
		(void)advertising_start();
		return;
	}

	err = juxta_range_as_initiator(conn, &result);
	LOG_INF("range result local=%s peer=%s role=initiator distance_m=%.3f quality=0x%04x "
		"status=%d",
		local_name, peer_name, (double)result.distance_m, result.quality, result.status);

	if (err != 0 || result.status != 0) {
		led_fail();
		k_sleep(K_MSEC(1000));
	}

	bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	(void)k_sem_take(&sem_disconnected, K_SECONDS(5));
	/* default_conn released in disconnected_cb */

	leds_off();
	atomic_set(&peer_ready, 0);
	atomic_set(&busy, 0);
	k_sleep(K_MSEC(COOLDOWN_MS));
	(void)advertising_start();
}

static void run_as_reflector(void)
{
	struct juxta_range_result result;
	int err;

	atomic_set(&busy, 1);
	led_role_reflector();
	LOG_INF("role=reflector local=%s peer=%s (waiting for connection)", local_name, peer_name);

	(void)bt_le_scan_stop();
	k_sem_reset(&sem_connected);
	k_sem_reset(&sem_disconnected);
	(void)advertising_start();

	err = k_sem_take(&sem_connected, K_SECONDS(15));
	if (err) {
		LOG_WRN("reflector: no inbound connection");
		led_fail();
		k_sleep(K_MSEC(1000));
		leds_off();
		atomic_set(&peer_ready, 0);
		atomic_set(&busy, 0);
		return;
	}

	err = juxta_range_as_reflector(default_conn, &result);
	LOG_INF("range result local=%s peer=%s role=reflector distance_m=nan quality=0x%04x "
		"status=%d",
		local_name, peer_name, result.quality, result.status);

	if (err != 0 || result.status != 0) {
		led_fail();
		k_sleep(K_MSEC(1000));
	}

	if (default_conn) {
		bt_conn_disconnect(default_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
		(void)k_sem_take(&sem_disconnected, K_SECONDS(5));
	}

	leds_off();
	atomic_set(&peer_ready, 0);
	atomic_set(&busy, 0);
	k_sleep(K_MSEC(COOLDOWN_MS));
	(void)advertising_start();
}

int main(void)
{
	int err;

	if (!gpio_is_ready_dt(&led_r) || !gpio_is_ready_dt(&led_g) || !gpio_is_ready_dt(&led_b) ||
	    !gpio_is_ready_dt(&button)) {
		LOG_ERR("GPIO not ready");
		return -ENODEV;
	}

	(void)gpio_pin_configure_dt(&led_r, GPIO_OUTPUT_INACTIVE);
	(void)gpio_pin_configure_dt(&led_g, GPIO_OUTPUT_INACTIVE);
	(void)gpio_pin_configure_dt(&led_b, GPIO_OUTPUT_INACTIVE);
	(void)gpio_pin_configure_dt(&button, GPIO_INPUT);
	(void)gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE);
	gpio_init_callback(&button_cb_data, button_pressed, BIT(button.pin));
	(void)gpio_add_callback(button.port, &button_cb_data);

	err = bt_enable(NULL);
	if (err) {
		LOG_ERR("bt_enable (%d)", err);
		return err;
	}

	err = juxta_id_fill_from_bt(local_name, sizeof(local_name));
	if (err) {
		return err;
	}

	LOG_INF("tag-cs started local_id=%s profile=mobile", local_name);
	LOG_INF("BTN1 cycles role_override: auto | force_initiator | force_reflector");
	LOG_INF("Do not use nRF54L15 DK CS antenna overlays on Tag TWI pins");

	err = advertising_start();
	if (err) {
		return err;
	}

	while (1) {
		if (atomic_get(&busy) != 0) {
			k_sleep(K_MSEC(100));
			continue;
		}

		(void)scan_burst();

		if (atomic_get(&peer_ready) == 0) {
			k_sleep(K_MSEC(SCAN_CYCLE_MS - SCAN_WINDOW_MS));
			continue;
		}

		if (we_should_initiate(peer_name)) {
			run_as_initiator();
		} else {
			run_as_reflector();
		}
	}

	return 0;
}
