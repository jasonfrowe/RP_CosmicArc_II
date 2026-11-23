/* 16 Achievements 4x4 Screen */

#ifndef ACHIEVE_H
#define ACHIEVE_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    ACH_STAY = 0,
    ACH_BACK, /* B: back to Sector */
    ACH_PREV, /* L1: back to Close-Up */
    ACH_NEXT  /* R1: wrap to Sector */
} ach_action_t;

extern const char *const ACH_NAMES[16];

void         achieve_enter(void);
ach_action_t achieve_update(void);

#endif /* ACHIEVE_H */
