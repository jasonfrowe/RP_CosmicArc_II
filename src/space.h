/* Deep Space Combat (Ark, Asteroids, Fighter, Pirates, Escape Pod) */

#ifndef SPACE_H
#define SPACE_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    SPACE_RUNNING = 0,
    SPACE_CLEARED,
    SPACE_ARK_LOST
} space_result_t;

void space_enter(uint8_t wave, bool quick_mode);
space_result_t space_update(void);

/* Escape pod sequence (after Ark is lost in space or on planet) */
void escape_enter(void);
bool escape_update(void); /* returns true when player presses fire to respawn at Sector 1 */

#endif /* SPACE_H */
