/* 16 Achievements 4x4 Screen */

#ifndef ACHIEVE_H
#define ACHIEVE_H

#include <stdbool.h>
#include <stdint.h>

extern const char *const ACH_NAMES[16];

void achieve_enter(void);
bool achieve_update(void); /* returns true when exiting back to Sector Map */

#endif /* ACHIEVE_H */
