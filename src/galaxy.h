/* 64-Sector Galaxy Generator, Sector View, Star Chart & Close-Up Circuit Chart */

#ifndef GALAXY_H
#define GALAXY_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    MAP_STAY = 0,
    MAP_GO_SPACE,   /* Transit or departure encounter (wave = map_enc_wave) */
    MAP_GO_PLANET,  /* Land on current sector's planet */
    MAP_GO_PORT,    /* Dock at current sector's starport */
    MAP_GO_ACHIEVE, /* Open 4x4 achievements screen */
    MAP_GO_TITLE    /* Return to title screen */
} map_action_t;

extern uint8_t map_enc_wave;
extern uint8_t map_dest_sector;

void    galaxy_generate(uint32_t seed);
void    galaxy_init_voyage_stocks(void);
uint8_t galaxy_planet_level(uint8_t sec);
uint8_t galaxy_planet_pop(uint8_t sec);
uint8_t galaxy_port_index(uint8_t sec);
bool    galaxy_roll_encounter(uint8_t sec);
void    galaxy_visit(uint8_t sec);
void    galaxy_step_turn(void);

void         galaxy_draw_tabs(uint8_t active);
void         map_enter(void);
void         map_enter_tab(uint8_t tab); /* 0=Sector, 1=Chart, 2=Close-Up */
map_action_t map_update(void);

#endif /* GALAXY_H */
