/*
 * HIL: unique device identity + mobile profile (no NVS / policy machinery).
 */

#include <stdio.h>
#include <string.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>

#include "juxta_id.h"

LOG_MODULE_REGISTER(tag_id, LOG_LEVEL_INF);

#define BOOT_LED_MS 500U

#if !DT_NODE_EXISTS(DT_NODELABEL(led1_green))
#error "Board must define led1_green"
#endif

static const struct gpio_dt_spec led_g = GPIO_DT_SPEC_GET(DT_NODELABEL(led1_green), gpios);

static void log_hwinfo_id(void)
{
	uint8_t buf[16];
	ssize_t len;
	char hex[33];

	memset(buf, 0, sizeof(buf));
	len = hwinfo_get_device_id(buf, sizeof(buf));
	if (len < 0) {
		LOG_WRN("hwinfo_get_device_id failed (%d)", (int)len);
		return;
	}

	size_t n = (size_t)len;

	if (n > 16U) {
		n = 16U;
	}
	for (size_t i = 0; i < n; i++) {
		(void)snprintf(&hex[i * 2U], 3, "%02x", buf[i]);
	}
	hex[n * 2U] = '\0';
	LOG_INF("hwinfo_device_id len=%d hex=%s", (int)len, hex);
}

static void log_bt_addrs(void)
{
	bt_addr_le_t addrs[CONFIG_BT_ID_MAX];
	size_t count = ARRAY_SIZE(addrs);
	char addr_str[BT_ADDR_LE_STR_LEN];

	bt_id_get(addrs, &count);
	LOG_INF("bt_id count=%u", (unsigned int)count);
	for (size_t i = 0; i < count; i++) {
		bt_addr_le_to_str(&addrs[i], addr_str, sizeof(addr_str));
		LOG_INF("bt_id[%u]=%s", (unsigned int)i, addr_str);
	}
}

int main(void)
{
	char jx_name[JUXTA_ID_LEN];
	int err;

	if (!gpio_is_ready_dt(&led_g)) {
		LOG_ERR("led1_green not ready");
		return -ENODEV;
	}

	err = gpio_pin_configure_dt(&led_g, GPIO_OUTPUT_INACTIVE);
	if (err != 0) {
		LOG_ERR("led1_green configure failed (%d)", err);
		return err;
	}

	(void)gpio_pin_set_dt(&led_g, 1);

	LOG_INF("tag-id started");
	log_hwinfo_id();

	err = bt_enable(NULL);
	if (err != 0) {
		LOG_ERR("bt_enable failed (%d)", err);
		return err;
	}

	/* Let HCI boot logs drain so HIL identity lines are not dropped on RTT. */
	k_sleep(K_MSEC(50));

	log_bt_addrs();

	err = juxta_id_fill_from_bt(jx_name, sizeof(jx_name));
	if (err != 0) {
		LOG_ERR("juxta_id_fill_from_bt failed (%d)", err);
		return err;
	}

	LOG_INF("juxta_name=%s", jx_name);
#if IS_ENABLED(CONFIG_JUXTA_PROFILE_MOBILE)
	LOG_INF("juxta_profile=mobile");
#else
	LOG_INF("juxta_profile=unknown");
#endif

	k_sleep(K_MSEC(BOOT_LED_MS));
	(void)gpio_pin_set_dt(&led_g, 0);

	LOG_INF("tag-id done (idle) juxta_name=%s", jx_name);
	while (1) {
		k_sleep(K_SECONDS(60));
	}

	return 0;
}
