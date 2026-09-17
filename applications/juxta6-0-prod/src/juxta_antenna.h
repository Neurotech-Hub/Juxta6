#ifndef JUXTA_ANTENNA_H_
#define JUXTA_ANTENNA_H_

#include <stdint.h>

int juxta_antenna_init(void);
int juxta_antenna_select(uint8_t ant); /* 1 or 2 */
uint8_t juxta_antenna_current(void);

#endif /* JUXTA_ANTENNA_H_ */
