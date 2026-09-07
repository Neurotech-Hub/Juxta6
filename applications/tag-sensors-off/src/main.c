/*
 * Probe BME688 (I2C 0x76) and BMI270 (SPI), then suspend both.
 * ADXL367 is left unconfigured (board default standby / unused).
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>

LOG_MODULE_REGISTER(tag_sensors_off, LOG_LEVEL_INF);

#if !DT_NODE_EXISTS(DT_NODELABEL(bme688))
#error "Board must define nodelabel bme688"
#endif

#if !DT_NODE_EXISTS(DT_NODELABEL(bmi270))
#error "Board must define nodelabel bmi270"
#endif

static int probe_and_suspend(const struct device *dev, const char *name)
{
	int ret;
	enum pm_device_state state;

	if (!device_is_ready(dev)) {
		LOG_ERR("[PROBE] %s FAIL not ready", name);
		return -ENODEV;
	}

	LOG_INF("[PROBE] %s ok", name);

	ret = pm_device_action_run(dev, PM_DEVICE_ACTION_SUSPEND);
	if (ret != 0 && ret != -ENOTSUP && ret != -ENOSYS) {
		LOG_ERR("[SUSPEND] %s FAIL rc=%d", name, ret);
		return ret;
	}
	if (ret == -ENOTSUP || ret == -ENOSYS) {
		LOG_WRN("[SUSPEND] %s PM not supported (rc=%d) — driver may still idle", name, ret);
	} else {
		LOG_INF("[SUSPEND] %s ok", name);
	}

	ret = pm_device_state_get(dev, &state);
	if (ret == 0) {
		LOG_INF("[STATE] %s pm_state=%d", name, (int)state);
	}

	return 0;
}

int main(void)
{
	const struct device *bme = DEVICE_DT_GET(DT_NODELABEL(bme688));
	const struct device *bmi = DEVICE_DT_GET(DT_NODELABEL(bmi270));
	int rc;

	LOG_INF("tag-sensors-off started (ADXL367 left unconfigured)");

	rc = probe_and_suspend(bme, "bme688");
	if (rc != 0) {
		return rc;
	}

	rc = probe_and_suspend(bmi, "bmi270");
	if (rc != 0) {
		return rc;
	}

	LOG_INF("Idle — BMI270 + BME688 suspended; measure with PPK2");

	while (1) {
		k_sleep(K_SECONDS(30));
		LOG_INF("still idle (sensors suspended)");
	}

	return 0;
}
