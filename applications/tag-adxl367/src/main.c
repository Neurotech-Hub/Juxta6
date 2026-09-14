/*
 * ADXL367 motion/activity count (software XYZ delta).
 * BMI270 + BME688 suspended for ADXL-only power characterization.
 * ADXL_IRQ is not in stock Tag DTS — IRQ path deferred until DTS adds it.
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>

LOG_MODULE_REGISTER(tag_adxl367, LOG_LEVEL_INF);

#define SAMPLE_PERIOD_MS 100U
#define LOG_PERIOD_MS 2000U
/* ~50 mg delta on any axis counts as a motion tick (software activity). */
#define MOTION_DELTA_MS2 0.5f

#if !DT_NODE_EXISTS(DT_NODELABEL(adxl367))
#error "Board must define nodelabel adxl367"
#endif

#if !DT_NODE_EXISTS(DT_NODELABEL(bme688))
#error "Board must define nodelabel bme688"
#endif

#if !DT_NODE_EXISTS(DT_NODELABEL(bmi270))
#error "Board must define nodelabel bmi270"
#endif

static void suspend_unused(const struct device *dev, const char *name)
{
	int ret;

	if (!device_is_ready(dev)) {
		LOG_WRN("[PROBE] %s not ready — skip suspend", name);
		return;
	}

	LOG_INF("[PROBE] %s ok — suspending", name);
	ret = pm_device_action_run(dev, PM_DEVICE_ACTION_SUSPEND);
	if (ret != 0 && ret != -ENOTSUP && ret != -ENOSYS) {
		LOG_WRN("[SUSPEND] %s rc=%d", name, ret);
	} else if (ret == -ENOTSUP || ret == -ENOSYS) {
		LOG_WRN("[SUSPEND] %s PM not supported (rc=%d) — driver may still idle", name, ret);
	} else {
		LOG_INF("[SUSPEND] %s ok", name);
	}
}

int main(void)
{
	const struct device *adxl = DEVICE_DT_GET(DT_NODELABEL(adxl367));
	const struct device *bme = DEVICE_DT_GET(DT_NODELABEL(bme688));
	const struct device *bmi = DEVICE_DT_GET(DT_NODELABEL(bmi270));
	struct sensor_value xyz[3];
	float last_x = 0.0f;
	float last_y = 0.0f;
	float last_z = 0.0f;
	bool have_last = false;
	uint32_t motion_count = 0U;
	int64_t next_log_ms;
	int ret;

	LOG_INF("tag-adxl367 started (software motion delta; no ADXL_IRQ in stock DTS)");

	suspend_unused(bme, "bme688");
	suspend_unused(bmi, "bmi270");

	if (!device_is_ready(adxl)) {
		LOG_ERR("[PROBE] adxl367 FAIL not ready");
		return -ENODEV;
	}
	LOG_INF("[PROBE] adxl367 ok");

	next_log_ms = k_uptime_get() + LOG_PERIOD_MS;

	while (1) {
		ret = sensor_sample_fetch(adxl);
		if (ret != 0) {
			LOG_WRN("sensor_sample_fetch failed (%d)", ret);
			k_sleep(K_MSEC(SAMPLE_PERIOD_MS));
			continue;
		}

		ret = sensor_channel_get(adxl, SENSOR_CHAN_ACCEL_XYZ, xyz);
		if (ret != 0) {
			LOG_WRN("sensor_channel_get XYZ failed (%d)", ret);
			k_sleep(K_MSEC(SAMPLE_PERIOD_MS));
			continue;
		}

		float x = sensor_value_to_float(&xyz[0]);
		float y = sensor_value_to_float(&xyz[1]);
		float z = sensor_value_to_float(&xyz[2]);

		if (have_last) {
			float dx = x - last_x;
			float dy = y - last_y;
			float dz = z - last_z;

			if (dx < 0.0f) {
				dx = -dx;
			}
			if (dy < 0.0f) {
				dy = -dy;
			}
			if (dz < 0.0f) {
				dz = -dz;
			}

			if (dx > MOTION_DELTA_MS2 || dy > MOTION_DELTA_MS2 || dz > MOTION_DELTA_MS2) {
				motion_count++;
			}
		}

		last_x = x;
		last_y = y;
		last_z = z;
		have_last = true;

		if (k_uptime_get() >= next_log_ms) {
			next_log_ms = k_uptime_get() + LOG_PERIOD_MS;
			LOG_INF("motion_count=%u x=%.3f y=%.3f z=%.3f m/s^2", motion_count,
				(double)x, (double)y, (double)z);
		}

		k_sleep(K_MSEC(SAMPLE_PERIOD_MS));
	}

	return 0;
}
