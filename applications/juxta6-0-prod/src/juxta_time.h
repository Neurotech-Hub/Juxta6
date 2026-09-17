#ifndef JUXTA_TIME_H_
#define JUXTA_TIME_H_

#include <stdint.h>
#include <stdbool.h>

void juxta_time_init(void);
void juxta_time_set(uint32_t unix_time);
uint32_t juxta_time_now(void);
bool juxta_time_is_set(void);

/** Fill out[9] with YYYYMMDD + NUL for calendar file names. */
void juxta_time_date_string(uint32_t unix_time, char out[9]);

#endif /* JUXTA_TIME_H_ */
