/*
 * ADXL367 motion + die temperature HIL (via lib/juxta_motion).
 * BMI270 + BME688 suspended. ADXL_IRQ still deferred (not in stock Tag DTS).
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "juxta_motion.h"

LOG_MODULE_REGISTER(tag_adxl367, LOG_LEVEL_INF);

#define LOG_PERIOD_MS 2000U

int main(void)
{
	int err;
	struct juxta_motion_sample s;
	int64_t next_log_ms;

	LOG_INF("tag-adxl367 started (juxta_motion: XYZ + DIE_TEMP)");

	err = juxta_motion_init(true);
	if (err) {
		LOG_ERR("juxta_motion_init (%d)", err);
		return err;
	}

	next_log_ms = k_uptime_get() + LOG_PERIOD_MS;

	while (1) {
		if (k_uptime_get() >= next_log_ms) {
			next_log_ms = k_uptime_get() + LOG_PERIOD_MS;
			juxta_motion_peek(&s);
			if (s.temp_valid) {
				LOG_INF("motion_count=%u temp_c=%.2f x=%.3f y=%.3f z=%.3f m/s^2",
					s.motion_count, (double)s.temp_c, (double)s.x, (double)s.y,
					(double)s.z);
			} else {
				LOG_INF("motion_count=%u temp_c=n/a x=%.3f y=%.3f z=%.3f m/s^2",
					s.motion_count, (double)s.x, (double)s.y, (double)s.z);
			}
		}
		k_sleep(K_MSEC(200));
	}

	return 0;
}
