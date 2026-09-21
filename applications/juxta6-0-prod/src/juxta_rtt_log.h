#ifndef JUXTA_RTT_LOG_H_
#define JUXTA_RTT_LOG_H_

#include <stdbool.h>
#include <stdint.h>

void juxta_rtt_jxs(const char *event);
void juxta_rtt_jxb(const char *peer, int8_t rssi);
void juxta_rtt_jxv(uint32_t motion, int32_t batt_mv, float temp_c, bool temp_ok, float humidity,
		   bool humidity_ok);

#endif /* JUXTA_RTT_LOG_H_ */
