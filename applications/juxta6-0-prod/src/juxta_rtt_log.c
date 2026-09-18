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

void juxta_rtt_jxv(uint32_t motion, int32_t batt_mv, float temp_c, bool temp_valid)
{
	uint32_t t = juxta_time_now();

	if (temp_valid) {
		LOG_INF("JXV unix=%u motion=%u batt_mv=%d temp_c=%.2f", t, motion, (int)batt_mv,
			(double)temp_c);
	} else {
		LOG_INF("JXV unix=%u motion=%u batt_mv=%d temp_c=nan", t, motion, (int)batt_mv);
	}
}
