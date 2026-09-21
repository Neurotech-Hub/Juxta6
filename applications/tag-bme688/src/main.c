/*
 * HIL-13: BME688 one-shot ambient temperature + humidity, then suspend.
 * BMI270 / ADXL367 left unused.
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>

LOG_MODULE_REGISTER(tag_bme688, LOG_LEVEL_INF);

#if !DT_NODE_EXISTS(DT_NODELABEL(bme688))
#error "Board must define nodelabel bme688"
#endif

#if DT_NODE_EXISTS(DT_NODELABEL(led1_green))
static const struct gpio_dt_spec led_g = GPIO_DT_SPEC_GET(DT_NODELABEL(led1_green), gpios);
#endif

int main(void)
{
	const struct device *bme = DEVICE_DT_GET(DT_NODELABEL(bme688));
	struct sensor_value temp;
	struct sensor_value hum;
	int ret;

	LOG_INF("tag-bme688 started (one-shot T/RH)");

#if DT_NODE_EXISTS(DT_NODELABEL(led1_green))
	if (gpio_is_ready_dt(&led_g)) {
		(void)gpio_pin_configure_dt(&led_g, GPIO_OUTPUT_INACTIVE);
	}
#endif

	if (!device_is_ready(bme)) {
		LOG_ERR("[PROBE] bme688 FAIL not ready");
		return -ENODEV;
	}
	LOG_INF("[PROBE] bme688 ok");

	ret = sensor_sample_fetch(bme);
	if (ret != 0) {
		LOG_ERR("[SAMPLE] fetch FAIL rc=%d", ret);
		return ret;
	}

	ret = sensor_channel_get(bme, SENSOR_CHAN_AMBIENT_TEMP, &temp);
	if (ret != 0) {
		LOG_ERR("[SAMPLE] temp FAIL rc=%d", ret);
		return ret;
	}

	ret = sensor_channel_get(bme, SENSOR_CHAN_HUMIDITY, &hum);
	if (ret != 0) {
		LOG_ERR("[SAMPLE] humidity FAIL rc=%d", ret);
		return ret;
	}

	LOG_INF("[SAMPLE] temp_c=%.2f humidity_pct=%.2f", sensor_value_to_double(&temp),
		sensor_value_to_double(&hum));

	ret = pm_device_action_run(bme, PM_DEVICE_ACTION_SUSPEND);
	if (ret != 0 && ret != -ENOTSUP && ret != -ENOSYS) {
		LOG_ERR("[SUSPEND] bme688 FAIL rc=%d", ret);
		return ret;
	}
	if (ret == -ENOTSUP || ret == -ENOSYS) {
		LOG_WRN("[SUSPEND] bme688 PM not supported (rc=%d)", ret);
	} else {
		LOG_INF("[SUSPEND] bme688 ok");
	}

#if DT_NODE_EXISTS(DT_NODELABEL(led1_green))
	if (gpio_is_ready_dt(&led_g)) {
		(void)gpio_pin_set_dt(&led_g, 1);
		k_sleep(K_MSEC(200));
		(void)gpio_pin_set_dt(&led_g, 0);
	}
#endif

	LOG_INF("Idle — BME688 suspended after one-shot");

	while (1) {
		k_sleep(K_SECONDS(30));
		LOG_INF("still idle");
	}

	return 0;
}
