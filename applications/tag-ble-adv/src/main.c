/*
 * Connectable BLE advertising smoke test.
 * LED1 green: slow blink while advertising, solid while connected.
 * BTN1 (sw0): log presses only — no shelf / magnet semantics.
 */

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

LOG_MODULE_REGISTER(tag_ble_adv, LOG_LEVEL_INF);

#define SLOW_ON_MS 50U
#define SLOW_OFF_MS 450U

#if !DT_NODE_EXISTS(DT_NODELABEL(led1_green))
#error "Board must define led1_green"
#endif

#if !DT_NODE_EXISTS(DT_ALIAS(sw0))
#error "Board must define alias sw0"
#endif

static const struct gpio_dt_spec led_g = GPIO_DT_SPEC_GET(DT_NODELABEL(led1_green), gpios);
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);

static struct gpio_callback button_cb_data;
static atomic_t connected;

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static int start_advertising(void)
{
	int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_2, ad, ARRAY_SIZE(ad), NULL, 0);

	if (err) {
		LOG_ERR("bt_le_adv_start failed (%d)", err);
	} else {
		LOG_INF("Advertising as %s", CONFIG_BT_DEVICE_NAME);
	}

	return err;
}

/* bt_le_adv_start from disconnected() returns -ENOMEM; defer to workqueue. */
static void adv_restart_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	(void)start_advertising();
}

static K_WORK_DEFINE(adv_restart_work, adv_restart_work_handler);

static void button_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

	LOG_INF("sw0 pressed (no shelf action in this HIL app)");
}

static void connected_cb(struct bt_conn *conn, uint8_t err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	if (err) {
		LOG_ERR("Connect failed %s (err 0x%02x)", addr, err);
		return;
	}

	atomic_set(&connected, 1);
	(void)gpio_pin_set_dt(&led_g, 1);
	LOG_INF("Connected %s — LED1 green solid", addr);
}

static void disconnected_cb(struct bt_conn *conn, uint8_t reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	atomic_set(&connected, 0);
	LOG_INF("Disconnected %s (reason 0x%02x) — restarting adv", addr, reason);

	(void)k_work_submit(&adv_restart_work);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected_cb,
	.disconnected = disconnected_cb,
};

int main(void)
{
	int err;

	if (!gpio_is_ready_dt(&led_g) || !gpio_is_ready_dt(&button)) {
		LOG_ERR("GPIO not ready");
		return -ENODEV;
	}

	err = gpio_pin_configure_dt(&led_g, GPIO_OUTPUT_INACTIVE);
	if (err != 0) {
		LOG_ERR("led1_green configure failed (%d)", err);
		return err;
	}

	err = gpio_pin_configure_dt(&button, GPIO_INPUT);
	if (err != 0) {
		LOG_ERR("sw0 configure failed (%d)", err);
		return err;
	}

	err = gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE);
	if (err != 0) {
		LOG_ERR("sw0 interrupt configure failed (%d)", err);
		return err;
	}

	gpio_init_callback(&button_cb_data, button_pressed, BIT(button.pin));
	err = gpio_add_callback(button.port, &button_cb_data);
	if (err != 0) {
		LOG_ERR("sw0 callback failed (%d)", err);
		return err;
	}

	err = bt_enable(NULL);
	if (err) {
		LOG_ERR("bt_enable failed (%d)", err);
		return err;
	}

	err = start_advertising();
	if (err) {
		return err;
	}

	LOG_INF("tag-ble-adv started");

	while (1) {
		if (atomic_get(&connected) == 0) {
			(void)gpio_pin_set_dt(&led_g, 1);
			k_sleep(K_MSEC(SLOW_ON_MS));
			(void)gpio_pin_set_dt(&led_g, 0);
			k_sleep(K_MSEC(SLOW_OFF_MS));
		} else {
			k_sleep(K_MSEC(200));
		}
	}

	return 0;
}
