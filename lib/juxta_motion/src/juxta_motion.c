#include "juxta_motion.h"

#include <errno.h>
#include <math.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(juxta_motion, LOG_LEVEL_INF);

#define SAMPLE_PERIOD_MS 100U
#define MOTION_DELTA_MS2 0.5f

#if !DT_NODE_EXISTS(DT_NODELABEL(adxl367))
#error "juxta_motion requires DT nodelabel adxl367"
#endif

static const struct device *adxl_dev;
static struct k_work_delayable poll_work;
static struct k_mutex lock;

static float last_x;
static float last_y;
static float last_z;
static bool have_last;
static uint32_t motion_count;
static float last_temp_c;
static bool temp_valid;
static float cur_x;
static float cur_y;
static float cur_z;
static bool ready;

static void suspend_unused(const struct device *dev, const char *name)
{
	int ret;

	if (dev == NULL || !device_is_ready(dev)) {
		return;
	}

	ret = pm_device_action_run(dev, PM_DEVICE_ACTION_SUSPEND);
	if (ret != 0 && ret != -ENOTSUP && ret != -ENOSYS) {
		LOG_WRN("suspend %s rc=%d", name, ret);
	} else {
		LOG_INF("suspend %s ok (or PM n/a)", name);
	}
}

/* Consecutive sensor-read failures: warn every ~60 s of continuous failure
 * (600 polls at 100 ms) so persistent I2C trouble is visible in RTT without
 * per-poll spam; log recovery once when reads come back. */
#define POLL_FAIL_WARN_EVERY 600U
static uint32_t poll_fail_streak;

static void poll_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	struct sensor_value xyz[3];
	struct sensor_value temp;
	int ret;

	ret = sensor_sample_fetch(adxl_dev);
	if (ret == 0) {
		ret = sensor_channel_get(adxl_dev, SENSOR_CHAN_ACCEL_XYZ, xyz);
	}

	if (ret != 0) {
		poll_fail_streak++;
		if (poll_fail_streak == 1U || (poll_fail_streak % POLL_FAIL_WARN_EVERY) == 0U) {
			LOG_WRN("ADXL367 poll failing rc=%d (streak %u)", ret,
				(unsigned int)poll_fail_streak);
		}
	} else if (poll_fail_streak != 0U) {
		LOG_INF("ADXL367 poll recovered after %u failures",
			(unsigned int)poll_fail_streak);
		poll_fail_streak = 0U;
	}

	if (ret == 0) {
		float x = sensor_value_to_float(&xyz[0]);
		float y = sensor_value_to_float(&xyz[1]);
		float z = sensor_value_to_float(&xyz[2]);

		(void)k_mutex_lock(&lock, K_FOREVER);
		cur_x = x;
		cur_y = y;
		cur_z = z;

		if (have_last) {
			float dx = fabsf(x - last_x);
			float dy = fabsf(y - last_y);
			float dz = fabsf(z - last_z);

			if (dx > MOTION_DELTA_MS2 || dy > MOTION_DELTA_MS2 || dz > MOTION_DELTA_MS2) {
				motion_count++;
			}
		}

		last_x = x;
		last_y = y;
		last_z = z;
		have_last = true;
		(void)k_mutex_unlock(&lock);
	}

	ret = sensor_channel_get(adxl_dev, SENSOR_CHAN_DIE_TEMP, &temp);
	if (ret == 0) {
		(void)k_mutex_lock(&lock, K_FOREVER);
		last_temp_c = sensor_value_to_float(&temp);
		temp_valid = true;
		(void)k_mutex_unlock(&lock);
	}

	(void)k_work_schedule(&poll_work, K_MSEC(SAMPLE_PERIOD_MS));
}

int juxta_motion_init(bool suspend_unused_sensors)
{
	k_mutex_init(&lock);
	k_work_init_delayable(&poll_work, poll_handler);

	adxl_dev = DEVICE_DT_GET(DT_NODELABEL(adxl367));
	if (!device_is_ready(adxl_dev)) {
		LOG_ERR("adxl367 not ready");
		return -ENODEV;
	}

	if (suspend_unused_sensors) {
#if DT_NODE_EXISTS(DT_NODELABEL(bme688))
		suspend_unused(DEVICE_DT_GET(DT_NODELABEL(bme688)), "bme688");
#endif
#if DT_NODE_EXISTS(DT_NODELABEL(bmi270))
		suspend_unused(DEVICE_DT_GET(DT_NODELABEL(bmi270)), "bmi270");
#endif
	}

	ready = true;
	LOG_INF("juxta_motion ready (ADXL367 XYZ + DIE_TEMP)");
	(void)k_work_schedule(&poll_work, K_NO_WAIT);
	return 0;
}

bool juxta_motion_ready(void)
{
	return ready;
}

void juxta_motion_stop(void)
{
	struct k_work_sync sync;

	if (!ready) {
		return;
	}

	ready = false;
	/* Blocks until a running poll_handler (and its I2C transfer) returns. */
	(void)k_work_cancel_delayable_sync(&poll_work, &sync);
}

void juxta_motion_peek(struct juxta_motion_sample *out)
{
	if (out == NULL) {
		return;
	}

	(void)k_mutex_lock(&lock, K_FOREVER);
	out->motion_count = motion_count;
	out->temp_c = last_temp_c;
	out->temp_valid = temp_valid;
	out->x = cur_x;
	out->y = cur_y;
	out->z = cur_z;
	(void)k_mutex_unlock(&lock);
}

void juxta_motion_take(struct juxta_motion_sample *out)
{
	if (out == NULL) {
		return;
	}

	(void)k_mutex_lock(&lock, K_FOREVER);
	out->motion_count = motion_count;
	out->temp_c = last_temp_c;
	out->temp_valid = temp_valid;
	out->x = cur_x;
	out->y = cur_y;
	out->z = cur_z;
	motion_count = 0U;
	(void)k_mutex_unlock(&lock);
}
