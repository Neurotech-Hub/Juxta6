/*
 * ADXL367 motion + die temperature helper for Tag (software activity count).
 */

#ifndef JUXTA_MOTION_H_
#define JUXTA_MOTION_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct juxta_motion_sample {
	uint32_t motion_count; /* ticks since last take/clear */
	float temp_c;
	float x;
	float y;
	float z;
	bool temp_valid;
};

/**
 * Probe ADXL367. Optionally suspend BMI270 + BME688 when those nodes exist.
 * Starts a 100 ms poll work item.
 */
int juxta_motion_init(bool suspend_unused_sensors);

/** Latest sample (does not clear motion_count). */
void juxta_motion_peek(struct juxta_motion_sample *out);

/**
 * Copy sample and clear the motion counter (for vitals windows).
 */
void juxta_motion_take(struct juxta_motion_sample *out);

bool juxta_motion_ready(void);

#ifdef __cplusplus
}
#endif

#endif /* JUXTA_MOTION_H_ */
