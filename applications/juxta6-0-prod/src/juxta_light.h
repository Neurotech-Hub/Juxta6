#ifndef JUXTA_LIGHT_H_
#define JUXTA_LIGHT_H_

#include <stdint.h>

/*
 * Loaded-millivolt to lux, linear through the origin. Change this one line
 * when recalibrating (for example inside an enclosure). Raw points, method,
 * and fit: applications/tag-light/README.md.
 */
#define JUXTA_LIGHT_LUX_PER_MV 4.99376f

/* Configure LHT_SNS_CTRL (P2.06) and the LIGHT_SENSE ADC channel. */
int juxta_light_init(void);

/*
 * Turn the load FET on, sample LIGHT_SENSE, then turn it off again.
 * Writes a non-negative whole-lux value. Returns 0, or a negative errno.
 */
int juxta_light_read_lux(uint32_t *lux);

#endif /* JUXTA_LIGHT_H_ */
