/*
 * Coarse peer discovery HIL (no Channel Sounding).
 *
 * Identical image on both tags:
 *   - non-connectable identity advertising as JX_XXXXXX
 *   - passive scan for other JX_ names
 *   - peer_seen / peer_lost after PEER_LOST_MS without re-sighting
 *
 * PEER_LOST_MS is a documented HIL timeout, not a tuned production value.
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

#include "juxta_id.h"

LOG_MODULE_REGISTER(tag_discover, LOG_LEVEL_INF);

/** Drop peer after this long without a sighting (HIL default, not tuned). */
#define PEER_LOST_MS 5000U
#define PEER_SLOT_COUNT 4U
#define SCAN_CYCLE_MS 1000U
#define SCAN_WINDOW_MS 500U

#if !DT_NODE_EXISTS(DT_NODELABEL(led1_green))
#error "Board must define led1_green"
#endif

#if !DT_NODE_EXISTS(DT_ALIAS(sw0))
#error "Board must define alias sw0"
#endif

static const struct gpio_dt_spec led_g = GPIO_DT_SPEC_GET(DT_NODELABEL(led1_green), gpios);
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);

static struct gpio_callback button_cb_data;
static char local_name[JUXTA_ID_LEN];

static struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_NO_BREDR),
	BT_DATA(BT_DATA_NAME_COMPLETE, local_name, 0),
};

struct peer_slot {
	char name[JUXTA_ID_LEN];
	int8_t last_rssi;
	int64_t last_seen_ms;
	bool active;
};

static struct peer_slot peers[PEER_SLOT_COUNT];
static struct k_mutex peers_lock;

static void button_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

	LOG_INF("sw0 pressed local_id=%s", local_name);
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

static void peer_note(const char *name, int8_t rssi)
{
	int64_t now = k_uptime_get();
	int free_idx = -1;
	bool any;

	k_mutex_lock(&peers_lock, K_FOREVER);

	for (int i = 0; i < (int)PEER_SLOT_COUNT; i++) {
		if (peers[i].active && strcmp(peers[i].name, name) == 0) {
			peers[i].last_rssi = rssi;
			peers[i].last_seen_ms = now;
			k_mutex_unlock(&peers_lock);
			return;
		}
		if (!peers[i].active && free_idx < 0) {
			free_idx = i;
		}
	}

	if (free_idx >= 0) {
		peers[free_idx].active = true;
		(void)snprintf(peers[free_idx].name, sizeof(peers[free_idx].name), "%s", name);
		peers[free_idx].last_rssi = rssi;
		peers[free_idx].last_seen_ms = now;
		LOG_INF("peer_seen id=%s rssi=%d", name, (int)rssi);
	}

	any = false;
	for (int i = 0; i < (int)PEER_SLOT_COUNT; i++) {
		if (peers[i].active) {
			any = true;
			break;
		}
	}
	(void)gpio_pin_set_dt(&led_g, any ? 1 : 0);

	k_mutex_unlock(&peers_lock);
}

static void peer_age_out(void)
{
	int64_t now = k_uptime_get();
	bool any = false;

	k_mutex_lock(&peers_lock, K_FOREVER);
	for (int i = 0; i < (int)PEER_SLOT_COUNT; i++) {
		if (!peers[i].active) {
			continue;
		}
		if ((now - peers[i].last_seen_ms) >= (int64_t)PEER_LOST_MS) {
			LOG_INF("peer_lost id=%s", peers[i].name);
			peers[i].active = false;
			peers[i].name[0] = '\0';
		} else {
			any = true;
		}
	}
	(void)gpio_pin_set_dt(&led_g, any ? 1 : 0);
	k_mutex_unlock(&peers_lock);
}

static void scan_cb(const bt_addr_le_t *addr, int8_t rssi, uint8_t type,
		    struct net_buf_simple *buf)
{
	char name[JUXTA_ID_LEN];

	ARG_UNUSED(addr);
	ARG_UNUSED(type);

	extract_name(buf, name, sizeof(name));
	if (juxta_id_is_peer_name(name, local_name)) {
		peer_note(name, rssi);
	}
}

static int advertising_start(void)
{
	ad[1].data_len = (uint8_t)strlen(local_name);

	int err = bt_le_adv_start(BT_LE_ADV_NCONN_IDENTITY, ad, ARRAY_SIZE(ad), NULL, 0);

	if (err != 0) {
		LOG_ERR("Advertising failed (%d)", err);
	} else {
		LOG_INF("Non-connectable advertising as \"%s\"", local_name);
	}

	return err;
}

int main(void)
{
	int err;
	struct bt_le_scan_param scan_param = {
		.type = BT_LE_SCAN_TYPE_PASSIVE,
		.options = BT_LE_SCAN_OPT_FILTER_DUPLICATE,
		.interval = BT_GAP_SCAN_FAST_INTERVAL,
		.window = BT_GAP_SCAN_FAST_WINDOW,
	};

	k_mutex_init(&peers_lock);

	if (!gpio_is_ready_dt(&led_g) || !gpio_is_ready_dt(&button)) {
		LOG_ERR("GPIO not ready");
		return -ENODEV;
	}

	err = gpio_pin_configure_dt(&led_g, GPIO_OUTPUT_INACTIVE);
	if (err != 0) {
		return err;
	}
	err = gpio_pin_configure_dt(&button, GPIO_INPUT);
	if (err != 0) {
		return err;
	}
	err = gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE);
	if (err != 0) {
		return err;
	}
	gpio_init_callback(&button_cb_data, button_pressed, BIT(button.pin));
	err = gpio_add_callback(button.port, &button_cb_data);
	if (err != 0) {
		return err;
	}

	err = bt_enable(NULL);
	if (err != 0) {
		LOG_ERR("bt_enable failed (%d)", err);
		return err;
	}

	err = juxta_id_fill_from_bt(local_name, sizeof(local_name));
	if (err != 0) {
		return err;
	}

	LOG_INF("tag-discover started local_id=%s peer_lost_ms=%u", local_name, PEER_LOST_MS);

	err = advertising_start();
	if (err != 0) {
		return err;
	}

	while (1) {
		err = bt_le_scan_start(&scan_param, scan_cb);
		if (err != 0) {
			LOG_ERR("scan start failed (%d)", err);
			k_sleep(K_MSEC(SCAN_CYCLE_MS));
			continue;
		}

		k_sleep(K_MSEC(SCAN_WINDOW_MS));
		(void)bt_le_scan_stop();
		peer_age_out();
		k_sleep(K_MSEC(SCAN_CYCLE_MS - SCAN_WINDOW_MS));
	}

	return 0;
}
