/*
 * SAADC internal VDD helper for nRF54L15 Tag (CR2032 direct supply).
 */

#ifndef JUXTA_VDD_H_
#define JUXTA_VDD_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize ADC channel for internal VDD.
 * Requires DT io-channel / adc channel with NRF_SAADC_VDD (see app overlay).
 *
 * @return 0 on success, negative errno on failure.
 */
int juxta_vdd_init(void);

/**
 * Read supply voltage in millivolts.
 * @return mV on success, negative errno on failure.
 */
int32_t juxta_vdd_read_mv(void);

/**
 * Rough CR2032 capacity estimate from mV (not calibrated).
 * @return 0–100
 */
uint8_t juxta_vdd_mv_to_percent_cr2032(int32_t mv);

#ifdef __cplusplus
}
#endif

#endif /* JUXTA_VDD_H_ */
