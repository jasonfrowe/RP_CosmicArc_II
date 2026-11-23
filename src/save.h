/* 3-Slot Save/Continue Persistence on RP6502 SAVE: drive */

#ifndef SAVE_H
#define SAVE_H

#include <stdbool.h>
#include <stdint.h>

#define SAVE_SLOTS 3

typedef struct {
    bool     valid;
    uint8_t  sector;
    uint16_t stardate;
    uint32_t credits;
    uint8_t  ach_count;
} save_info_t;

bool save_write(uint8_t slot);
bool save_read(uint8_t slot);
void save_erase(uint8_t slot);
void save_probe(uint8_t slot, save_info_t *info);

#endif /* SAVE_H */
