/* Shared Game State and Constants for Cosmic Arc II (RP6502) */

#ifndef GAME_H
#define GAME_H

#include <stdbool.h>
#include <stdint.h>

/* ------------------------------------------------------------------ */
/* Game Modes                                                         */
/* ------------------------------------------------------------------ */

typedef enum {
    GM_TITLE = 0,
    GM_MAP,        /* Sector View / Star Chart / Close-up Chart */
    GM_PORT,       /* Starport tabbed menu */
    GM_ACHIEVE,    /* 4x4 Achievements screen */
    GM_SPACE,      /* Deep space combat (Ark + Fighter + Pirates) */
    GM_PLANET,     /* Planetary extraction (Shuttle + Beam + Storm) */
    GM_ESCAPEPOD   /* Ark destroyed -> saucer escapes to Sector 1 */
} game_mode_t;

/* ------------------------------------------------------------------ */
/* Galaxy & Economy Constants (matching 7800 Cosmic Arc II)           */
/* ------------------------------------------------------------------ */

#define GX_SECTORS    64
#define GX_GRID_W     12
#define GX_GRID_H     8
#define PT_NPORTS     11
#define PT_NORMAL     12
#define PT_FULL       15
#define PL_POP_MAX    4
#define NUM_SPECIES   6
#define NUM_WORLDS    6

#define GT_EMPTY      0
#define GT_PORT       1
#define GT_PLANET     2
#define GT_NEBULA     3

#define TIER_SAFE     0
#define TIER_CORE     1
#define TIER_MID      2
#define TIER_OUTER    3

#define EN_BASE       100
#define EN_STEP       5
#define EN_TOP        200
#define EN_UPS_MAX    20

/* Achievement bit indices (16 total) */
#define ACH_PAIRS     0
#define ACH_SURVEY    1
#define ACH_ACE       2
#define ACH_CHARTED   3
#define ACH_TYCOON    4
#define ACH_REFIT     5
#define ACH_RELIC     6
#define ACH_UNTOUCHED 7
#define ACH_SWEEP     8
#define ACH_STORMS    9
#define ACH_FARSHORE  10
#define ACH_DEEPRAID  11
#define ACH_MERCHANT  12
#define ACH_BANKER    13
#define ACH_LOADED    14
#define ACH_LIFEBOAT  15
#define ACH_COUNT     16

/* ------------------------------------------------------------------ */
/* Persistent Voyage State (saved to SAVE:slotN.dat)                  */
/* ------------------------------------------------------------------ */

typedef struct {
    uint32_t magic;       /* 'CA2S' */
    uint32_t seed;        /* Galaxy RNG seed */
    uint8_t  sector;      /* Current sector 0..63 (displayed 1..64) */
    uint16_t stardate;    /* Turns / jumps elapsed */
    uint32_t credits;     /* Credits aboard Ark */
    uint32_t bank;        /* Credits in Safe Space bank (survives loss) */
    uint8_t  energy;      /* Current Ark energy (0..hull_max) */
    uint8_t  u_hull;      /* Hull upgrades 0..20 (+5 max energy each) */
    uint8_t  u_holds;     /* Hold upgrades 0..2 (10, 15, 20 holds) */
    uint8_t  ore;         /* Fuel Ore in holds */
    uint8_t  org;         /* Organics in holds */
    uint8_t  eqp;         /* Equipment in holds */
    uint8_t  stasis[NUM_SPECIES]; /* Unsold creatures in holds */
    uint8_t  keep[NUM_SPECIES];   /* Kept specimens in Ark bays (0..2 each) */
    uint8_t  port_stock[PT_NPORTS][3]; /* Stock per port per commodity (0..15) */
    uint8_t  port_glut[PT_NPORTS];     /* Recent creature sales at each port */
    uint8_t  planet_miss[GX_SECTORS];  /* Creatures depleted from each planet */
    uint8_t  visited[GX_SECTORS];      /* Bit 0: visited, Bit 1: seen */
    uint16_t ach_bits;    /* 16 achievement flags */
    uint16_t kills;       /* Pirates destroyed */
    uint8_t  storms;      /* Storm rocks shot down */
    uint8_t  sold_goods;  /* Goods sold at ports (clamped 255) */
    uint8_t  relic_sec;   /* Sector holding the Relic */
    uint8_t  relic_state; /* 0=on planet, 1=aboard Ark, 2=delivered */
    uint8_t  tick_port;   /* Jump counter for port restock */
    uint8_t  tick_planet; /* Jump counter for planet repop */
} voyage_t;

/* Rebuilt from voyage.seed on New Game / Continue */
typedef struct {
    uint8_t warp[GX_SECTORS];   /* 8-bit compass mask (bit 0=N, clockwise) */
    uint8_t cell[GX_SECTORS];   /* row * 16 + col */
    uint8_t info[GX_SECTORS];   /* bits 0-1: type, 2-4: class/world, 5: easier, 6-7: tier */
    uint8_t danger[GX_SECTORS]; /* Jumps back to safe space (encounter wave strength) */
    uint8_t port_sec[PT_NPORTS];/* Sector index of each port 0..10 */
} galaxy_t;

extern game_mode_t g_mode;
extern voyage_t    g_voy;
extern galaxy_t    g_gx;
extern uint8_t     g_frame;
extern uint8_t     g_save_slot;
extern uint8_t     g_pending_ach; /* 0xFF if none, else newly earned ach id */
extern volatile uint16_t lag_frames;

/* Helpers */
uint8_t  voy_hull_max(void);
uint8_t  voy_holds_max(void);
uint8_t  voy_holds_used(void);
bool     voy_has_hold_room(void);
void     voy_new(uint32_t seed);
void     ach_set(uint8_t id);
void     ach_check_all(void);

extern const char *const WORLD_NAMES[NUM_WORLDS];
extern const char *const SPECIES_NAMES[NUM_SPECIES];

#endif /* GAME_H */
