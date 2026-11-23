#ifndef SFX_H
#define SFX_H

#include <stdint.h>
#include "gen/sfx_ids.h"

#define SFX_PRI_LOW   1
#define SFX_PRI_SHOT  2
#define SFX_PRI_HIT   3
#define SFX_PRI_EVENT 4
#define SFX_PRI_TOP   5

void sfx_init(void);
void sfx_player(uint8_t id, uint8_t priority);
void sfx_enemy(uint8_t id, uint8_t priority);
void sfx_update(void);

#endif /* SFX_H */
