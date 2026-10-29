#include "juxta_rtt_log.h"

#include <zephyr/logging/log.h>

#include "juxta_time.h"

LOG_MODULE_REGISTER(juxta_rtt_log, LOG_LEVEL_INF);

void juxta_rtt_jxs(const char *event)
{
	uint32_t t = juxta_time_now();

	LOG_INF("JXS unix=%u event=%s", t, event != NULL ? event : "?");
}

void juxta_rtt_jxb(const char *peer, int8_t rssi)
{
	uint32_t t = juxta_time_now();

	LOG_INF("JXB unix=%u peer=%s rssi=%d", t, peer != NULL ? peer : "?", (int)rssi);
}

void juxta_rtt_jxv(uint32_t motion, int32_t batt_mv, float temp_c, bool temp_ok, float humidity,
		   bool humidity_ok, uint32_t lux, bool lux_ok)
{
	uint32_t t = juxta_time_now();
	char lux_field[16];

	if (lux_ok) {
		(void)snprintf(lux_field, sizeof(lux_field), "%u", lux);
	} else {
		(void)snprintf(lux_field, sizeof(lux_field), "nan");
	}

	if (temp_ok && humidity_ok) {
		LOG_INF("JXV unix=%u motion=%u batt_mv=%d temp_c=%.1f humidity=%.1f lux=%s", t,
			motion, (int)batt_mv, (double)temp_c, (double)humidity, lux_field);
	} else if (temp_ok) {
		LOG_INF("JXV unix=%u motion=%u batt_mv=%d temp_c=%.1f humidity=nan lux=%s", t,
			motion, (int)batt_mv, (double)temp_c, lux_field);
	} else if (humidity_ok) {
		LOG_INF("JXV unix=%u motion=%u batt_mv=%d temp_c=nan humidity=%.1f lux=%s", t,
			motion, (int)batt_mv, (double)humidity, lux_field);
	} else {
		LOG_INF("JXV unix=%u motion=%u batt_mv=%d temp_c=nan humidity=nan lux=%s", t, motion,
			(int)batt_mv, lux_field);
	}
}
