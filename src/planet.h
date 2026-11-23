/* Planetary Extraction (Wrapping Scrolling Terrain, Shuttle, Beam, Creatures, Storm) */

#ifndef PLANET_H
#define PLANET_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    PLANET_RUNNING = 0,
    PLANET_DONE,
    PLANET_ARK_LOST
} planet_result_t;

void planet_enter(uint8_t world_idx, uint8_t level, uint8_t pop, bool has_relic, uint8_t sector_idx);
planet_result_t planet_update(void);

#endif /* PLANET_H */
