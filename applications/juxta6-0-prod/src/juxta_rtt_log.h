#ifndef JUXTA_RTT_LOG_H_
#define JUXTA_RTT_LOG_H_

#include <stdbool.h>
#include <stdint.h>

void juxta_rtt_jxs(const char *event);
/* v6: observer dropped from the JXB row (constant per device). */
void juxta_rtt_jxb(const char *peer, int8_t rssi);
void juxta_rtt_jxv(uint32_t motion, int32_t batt_mv, float temp_c, bool temp_valid);

#endif /* JUXTA_RTT_LOG_H_ */
