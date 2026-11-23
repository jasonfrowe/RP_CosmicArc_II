#include <rp6502.h>
#include <string.h>
#include "game.h"
#include "input.h"
#include "math_fixed.h"
#include "music.h"
#include "planet.h"
#include "sfx.h"
#include "video.h"
#include "xram.h"

/* ------------------------------------------------------------------ */
/* World & Mechanics Constants (1024px wrapping world, 320x240 view)  */
/* ------------------------------------------------------------------ */

#define WORLD_W      1024
#define WORLD_MASK   1023
#define ARK_WX       512
#define ARK_ORBIT_Y  56
#define TUNNEL_Y     84
#define PL_DOCK_Y    76
#define PL_YMIN      104
#define PL_YMAX      172
#define BEAM_HALF    10
#define DOCK_ZONE    16

#define MAX_CREATURES 4
#define MAX_CRATES    3
#define NUM_TOWERS    4
#define TOWER_SPACING 256
#define TOWER_OFFSET  128

/* Shuttle states */
#define SHU_DOCKED   0
#define SHU_AUTO_OUT 1
#define SHU_FLYING   2
#define SHU_AUTO_IN  3
#define SHU_DEAD     4

/* Planet sub-states */
#define PS_ARRIVE    0
#define PS_PLAY      1
#define PS_CLEAR     2
#define PS_DEPART    3
#define PS_LOST      4

/* Cargo types carried by shuttle */
#define CARGO_NONE     0
#define CARGO_CREATURE 1
#define CARGO_ORE      2
#define CARGO_RELIC    3

/* ------------------------------------------------------------------ */
/* Entities                                                           */
/* ------------------------------------------------------------------ */

typedef struct {
    uint8_t active;   /* 0=gone/delivered, 1=free, 2=beaming, 3=carried */
    uint8_t species;  /* 0..5 */
    uint8_t state;    /* 0=ground, 1=air/glide */
    int8_t  dir;      /* -1 or +1 */
    uint8_t timer;
    uint8_t react_cd;
    int16_t wx;       /* 0..1023 */
    int16_t y;        /* screen y */
    int8_t  vy;
} creature_t;

typedef struct {
    uint8_t active;   /* 0=gone, 1=ground, 2=beaming, 3=carried */
    bool    is_relic;
    int16_t wx;
    int16_t y;
} crate_t;

/* ------------------------------------------------------------------ */
/* Module State                                                       */
/* ------------------------------------------------------------------ */

#define ground_y  ((uint8_t *)(g_arena))
#define creatures ((creature_t *)(g_arena + 128))
#define crates    ((crate_t *)(g_arena + 192))

static uint8_t  p_state;
static uint8_t  p_world;
static uint8_t  p_level;      /* 1..5 */
static uint8_t  p_sector;
static uint8_t  p_pop_init;
static uint8_t  p_caught;
static bool     p_shuttle_lost;

static int16_t  p_ark_y;
static uint16_t p_timer;

/* Shuttle */
static uint8_t  shu_st;
static int16_t  shu_wx;       /* 0..1023 */
static int16_t  shu_y;
static int16_t  shu_vx;       /* 12.4 fixed point */
static int16_t  shu_subx;     /* 12.4 fractional accumulator */
static uint8_t  shu_launch_hold;
static bool     shu_can_dock;
static uint16_t shu_rebuild_tm;
static uint8_t  shu_cargo;
static uint8_t  shu_cargo_idx;
static bool     shu_beaming;
static uint8_t  leave_hold;

/* Tower Defenses (synchronized sweep) */
static int16_t  gun_y;
static int8_t   gun_dir;
static uint8_t  gun_step_tm;
static uint8_t  gun_dwell;
static uint16_t pulse_tm;
static uint8_t  pulse_warn;
static uint8_t  pulse_on;

/* Storm & Ark defense */
static uint16_t storm_clock;  /* 45*60 = 2700 down to 0 */
static uint16_t storm_delay;  /* 10*60 = 600 down to 0 */
static uint8_t  storm_phase;  /* 0=calm, 1=klaxon warning, 2=rock incoming, 3=resolved */
static uint8_t  storm_dir;    /* 0=top, 1=left, 2=right */
static int16_t  storm_x, storm_y; /* 12.4 fixed point in screen coords when Ark centered */
static int16_t  storm_vx, storm_vy;
static uint8_t  ark_bolt_life;
static int16_t  ark_bolt_x, ark_bolt_y;
static int8_t   ark_bolt_dx, ark_bolt_dy;
static uint8_t  prev_joy;

/* ------------------------------------------------------------------ */
/* Terrain Generation & Tilemap Upload                                */
/* ------------------------------------------------------------------ */

static int16_t ground_at(int16_t wx)
{
    uint8_t col = (uint8_t)((wx & WORLD_MASK) >> 3);
    return (int16_t)ground_y[col];
}

static void build_planet_maps(void)
{
    /* Plane 0: 64x26 wrapping sky with stars and distant hills (reuse idle spr[] buffer) */
    uint8_t *row = (uint8_t *)spr;
    uint16_t s = (uint16_t)(0x3175u + p_world * 911u + p_sector * 37u);
    for (uint8_t r = 0; r < PSKY_MAP_ROWS; r++) {
        for (uint8_t c = 0; c < PSKY_MAP_COLS; c++) {
            s ^= s << 7; s ^= s >> 9; s ^= s << 8;
            if (r < 17) {
                uint8_t v = (uint8_t)(s & 31u);
                row[c] = (v == 0) ? 2 : ((v == 1) ? 3 : 1);
            } else if (r == 17) {
                row[c] = ((c + (c >> 2)) & 3) ? 4 : 1;
            } else if (r == 18) {
                row[c] = ((c & 3) == 0) ? 4 : 5;
            } else {
                row[c] = 5;
            }
        }
        xram0_write((uint16_t)(XRAM_MAP_PSKY + (uint16_t)r * PSKY_MAP_COLS), row, PSKY_MAP_COLS);
    }

    /* Compute rolling ground heights for 128 columns (tile row 22 = y 192, row 23 = y 200) */
    /* 4 towers sit at columns 16, 48, 80, 112 (every 32 tiles = 256px, always >=1 on 320px screen) */
    for (uint8_t c = 0; c < 128; c++) {
        uint8_t mod = c & 31u;
        uint8_t ht;
        if (mod >= 14 && mod <= 18) {
            ht = 0; /* Flat high plateau for tower */
        } else if (mod == 13 || mod == 19) {
            ht = 1;
        } else {
            ht = ((c >> 3) & 1u) ? 2 : 1;
        }
        ground_y[c] = (uint8_t)(192 + ht * 4);
    }

    /* Plane 1: 128x26 wrapping terrain & towers (rows 0..25 -> y = 16..223) */
    for (uint8_t r = 0; r < PGND_MAP_ROWS; r++) {
        for (uint8_t half = 0; half < 2; half++) {
            uint8_t base_c = (uint8_t)(half * 64u);
            for (uint8_t i = 0; i < 64; i++) {
                uint8_t c = (uint8_t)(base_c + i);
                bool is_tower = ((c & 31u) == 16u);
                uint8_t ht = (uint8_t)((ground_y[c] - 192u) >> 2);
                uint8_t t = 0; /* transparent sky */
                if (is_tower && r == 10) {
                    t = 13; /* tower cap at y=96 */
                } else if (is_tower && r > 10 && r < 22) {
                    t = 12; /* tower shaft y=104..191 */
                } else if (is_tower && r == 22) {
                    t = 14; /* tower base on ground */
                } else if (r == 22) {
                    if (ht == 0) t = 6;
                    else if (ht == 1) t = 7;
                    else t = 0;
                } else if (r == 23) {
                    if (ht == 2) t = 6;
                    else t = 10;
                } else if (r == 24) {
                    t = 10;
                } else if (r == 25) {
                    t = 11;
                }
                row[i] = t;
            }
            xram0_write((uint16_t)(XRAM_MAP_PGND + (uint16_t)r * PGND_MAP_COLS + base_c), row, 64);
        }
    }
    spr_hide_all();
}

/* ------------------------------------------------------------------ */
/* Helpers                                                            */
/* ------------------------------------------------------------------ */

static int16_t wrap_dx(int16_t a, int16_t b)
{
    int16_t d = (int16_t)((a - b) & WORLD_MASK);
    if (d >= 512) d -= 1024;
    return d;
}

static void planet_ark_damage(uint8_t dmg)
{
    sfx_enemy(SFX_ARK_HIT, SFX_PRI_HIT);
    if (g_voy.energy <= dmg) {
        g_voy.energy = 0;
        p_state = PS_LOST;
        p_timer = 40;
        sfx_enemy(SFX_BOOM, SFX_PRI_TOP);
        msg_center(1, "ARK DESTROYED!", C_RED);
    } else {
        g_voy.energy -= dmg;
    }
}

static void drop_cargo(void)
{
    if (shu_cargo == CARGO_CREATURE) {
        creature_t *c = &creatures[shu_cargo_idx];
        c->active = 1;
        c->wx = shu_wx;
        c->y = shu_y + 10;
        c->state = 1;
        c->vy = 0;
    } else if (shu_cargo == CARGO_ORE || shu_cargo == CARGO_RELIC) {
        crate_t *k = &crates[shu_cargo_idx];
        k->active = 1;
        k->wx = shu_wx;
        k->y = shu_y + 10;
    }
    shu_cargo = CARGO_NONE;
}

static void kill_shuttle(void)
{
    p_shuttle_lost = true;
    drop_cargo();
    shu_st = SHU_DEAD;
    shu_rebuild_tm = 150;
    shu_beaming = false;
    sfx_enemy(SFX_BOOM, SFX_PRI_TOP);
    msg_flash("SHUTTLE DESTROYED!", C_RED, 75);
    planet_ark_damage(12);
}

static void deliver_cargo(void)
{
    if (shu_cargo == CARGO_CREATURE) {
        creature_t *c = &creatures[shu_cargo_idx];
        c->active = 0;
        if (voy_has_hold_room()) {
            g_voy.stasis[c->species]++;
        }
        uint8_t mx = voy_hull_max();
        uint16_t e = (uint16_t)g_voy.energy + 12u;
        g_voy.energy = (e > mx) ? mx : (uint8_t)e;
        p_caught++;
        if (p_sector < GX_SECTORS && g_voy.planet_miss[p_sector] < PL_POP_MAX)
            g_voy.planet_miss[p_sector]++;
        sfx_player(SFX_PICKUP, SFX_PRI_EVENT);
        msg_flash("SPECIMEN DELIVERED +12 EN", C_GREEN, 60);
    } else if (shu_cargo == CARGO_ORE) {
        crates[shu_cargo_idx].active = 0;
        uint8_t room = (uint8_t)(voy_holds_max() - voy_holds_used());
        uint8_t add = (room < 5) ? room : 5;
        g_voy.ore += add;
        sfx_player(SFX_PICKUP, SFX_PRI_EVENT);
        msg_flash("ORE CRATE STORED", C_GOLD, 60);
    } else if (shu_cargo == CARGO_RELIC) {
        crates[shu_cargo_idx].active = 0;
        g_voy.relic_state = 1;
        sfx_player(SFX_FANFARE, SFX_PRI_TOP);
        msg_flash("RELIC ABOARD! RETURN TO PORT", C_MAGENTA, 90);
    }
    shu_cargo = CARGO_NONE;
    ach_check_all();

    if (p_caught >= p_pop_init && p_pop_init > 0 && storm_phase == 0) {
        p_state = PS_CLEAR;
        p_timer = 75;
        if (!p_shuttle_lost) ach_set(ACH_SWEEP);
        sfx_player(SFX_FANFARE, SFX_PRI_TOP);
        msg_center(1, "ALL SPECIMENS CAPTURED!", C_GREEN);
    }
}

/* ------------------------------------------------------------------ */
/* HUD & 16-cell Planet Scanner                                       */
/* ------------------------------------------------------------------ */

static void planet_hud_draw(void)
{
    static const uint8_t world_col[NUM_WORLDS] = {
        C_GREEN, C_RED, C_CYAN, C_MAGENTA, C_GOLD, C_GREEN
    };

    /* Row 0: Credits | World + Level | Graphical Creature Icons | Storm Status */
    hud_top_str(1, 0, "$", C_GOLD);
    hud_top_num(2, 0, g_voy.credits, 5, C_WHITE);
    hud_top_tile(8, 0, HT_DIV);

    hud_top_str(9, 0, WORLD_NAMES[p_world], world_col[p_world]);
    hud_top_str(15, 0, "L", C_GREY);
    hud_top_num(16, 0, p_level, 1, C_WHITE);
    hud_top_tile(18, 0, HT_DIV);

    /* Custom 4bpp pixel-art creature gem icons */
    for (uint8_t i = 0; i < p_pop_init; i++) {
        bool caught = (creatures[i].active == 0);
        hud_top_tile((uint8_t)(20 + i * 2), 0, caught ? HT_ICO_BIO_HI : HT_ICO_BIO_LO);
    }
    hud_top_tile(29, 0, HT_DIV);

    if (storm_phase == 0) {
        hud_top_str(30, 0, " ORBIT OK", C_TEAL);
    } else {
        hud_top_str(30, 0, "STORM!!  ", (g_frame & 8u) ? C_RED : C_GOLD);
    }

    /* Row 1: Custom 4bpp Pixel-Art 24-cell Planetary Radar (composed then written in-place) */
    uint8_t sc0 = 8;
    hud_top_str(1, 1, "RADAR", C_CYAN);
    hud_top_tile(sc0 - 1, 1, HT_RAD_L);
    hud_top_tile(sc0 + 24, 1, HT_RAD_R);
    uint8_t *rad = g_arena + 224;
    memset(rad, HT_RAD_EMPTY, 24);

    for (uint8_t t = 0; t < NUM_TOWERS; t++) {
        uint8_t cell = (uint8_t)((uint16_t)(TOWER_OFFSET + t * TOWER_SPACING) * 24u / WORLD_W);
        rad[cell] = HT_RAD_TOWER;
    }
    for (uint8_t i = 0; i < MAX_CREATURES; i++) {
        if (creatures[i].active == 1 || creatures[i].active == 2) {
            uint8_t cell = (uint8_t)((uint16_t)(creatures[i].wx & WORLD_MASK) * 24u / WORLD_W);
            rad[cell] = HT_RAD_BIO;
        }
    }
    for (uint8_t i = 0; i < MAX_CRATES; i++) {
        if (crates[i].active && crates[i].is_relic) {
            uint8_t cell = (uint8_t)((uint16_t)(crates[i].wx & WORLD_MASK) * 24u / WORLD_W);
            rad[cell] = HT_RAD_RELIC;
        }
    }
    rad[12] = HT_RAD_ARK;
    if (shu_st != SHU_DEAD) {
        uint8_t cell = (uint8_t)((uint16_t)(shu_wx & WORLD_MASK) * 24u / WORLD_W);
        rad[cell] = HT_RAD_SHU;
    }
    if (storm_phase == 2 && (g_frame & 4u)) {
        rad[12] = HT_RAD_STORM;
    }
    for (uint8_t i = 0; i < 24; i++)
        hud_top_tile((uint8_t)(sc0 + i), 1, rad[i]);

    /* Bottom HUD: Row 0 = 4bpp Energy Bar | Shuttle Bay | Cargo Holds */
    hud_bot_tile(0, 0, HT_ICO_BOLT);
    hud_bot_tile(1, 0, HT_BAR_L);
    hud_bot_bar(2, 0, 10, g_voy.energy, voy_hull_max(), false);
    hud_bot_tile(12, 0, HT_BAR_R);
    hud_bot_tile(13, 0, HT_DIV);

    hud_bot_tile(14, 0, HT_ICO_SAUCER);
    hud_bot_str(16, 0, "           ", C_WHITE);
    if (shu_st == SHU_DEAD)
        hud_bot_str(16, 0, "REBUILDING", (g_frame & 8u) ? C_RED : C_ORANGE);
    else if (shu_cargo == CARGO_CREATURE)
        hud_bot_str(16, 0, SPECIES_NAMES[creatures[shu_cargo_idx].species], C_GREEN);
    else if (shu_cargo == CARGO_ORE)
        hud_bot_str(16, 0, "ORE CRATE", C_GOLD);
    else if (shu_cargo == CARGO_RELIC)
        hud_bot_str(16, 0, "RELIC!", (g_frame & 4u) ? C_MAGENTA : C_WHITE);
    else
        hud_bot_str(16, 0, "EMPTY", C_GREY);

    hud_bot_tile(28, 0, HT_DIV);
    hud_bot_tile(29, 0, HT_ICO_CRATE);
    hud_bot_str(31, 0, "HLD", C_GOLD);
    hud_bot_put(35, 0, (uint8_t)('0' + voy_holds_used() / 10), C_WHITE, C_TRANS);
    hud_bot_put(36, 0, (uint8_t)('0' + voy_holds_used() % 10), C_WHITE, C_TRANS);
    hud_bot_put(37, 0, '/', C_DIM, C_TRANS);
    hud_bot_put(38, 0, (uint8_t)('0' + voy_holds_max() / 10), C_CYAN, C_TRANS);
    hud_bot_put(39, 0, (uint8_t)('0' + voy_holds_max() % 10), C_CYAN, C_TRANS);

    /* Row 1: Fixed-width control hints (overwrites in-place, zero flicker) */
    if (storm_phase == 1 || storm_phase == 2) {
        hud_bot_str(4, 1, "STORM! DOCK & FIRE [UP/LT/RT]   ", (g_frame & 8u) ? C_RED : C_GOLD);
    } else if (shu_st == SHU_DOCKED) {
        hud_bot_str(4, 1, " [DOWN] LAUNCH   [HOLD B] LEAVE ", C_CYAN);
    } else {
        hud_bot_str(4, 1, "[A] BEAM   [B] DROP   [UP] DOCK ", C_GREY);
    }
}

/* ------------------------------------------------------------------ */
/* Public Entry                                                       */
/* ------------------------------------------------------------------ */

void planet_enter(uint8_t world_idx, uint8_t level, uint8_t pop, bool has_relic, uint8_t sector_idx)
{
    memset(creatures, 0, sizeof(creature_t) * MAX_CREATURES);
    memset(crates, 0, sizeof(crate_t) * MAX_CRATES);

    p_world = (world_idx < NUM_WORLDS) ? world_idx : 0;
    p_level = (level < 1) ? 1 : (level > 5 ? 5 : level);
    p_sector = sector_idx;
    p_pop_init = (pop > MAX_CREATURES) ? MAX_CREATURES : pop;
    p_caught = 0;
    p_shuttle_lost = false;
    p_state = PS_ARRIVE;
    p_ark_y = -24;
    p_timer = 0;

    shu_st = SHU_DOCKED;
    shu_wx = ARK_WX;
    shu_y = PL_DOCK_Y;
    shu_vx = 0;
    shu_subx = 0;
    shu_launch_hold = 0;
    shu_cargo = CARGO_NONE;
    shu_beaming = false;
    leave_hold = 0;

    /* Tower guns */
    gun_y = PL_YMIN;
    gun_dir = 1;
    gun_step_tm = 4;
    gun_dwell = 0;
    pulse_tm = 120;
    pulse_warn = 0;
    pulse_on = 0;

    /* Storm */
    storm_clock = 45u * 60u;
    storm_delay = 10u * 60u;
    storm_phase = 0;
    ark_bolt_life = 0;
    prev_joy = 8;

    video_set_mode(VID_MODE_PLANET, p_world);
    build_planet_maps();

    /* Spawn creatures from species band (level-1 .. level+1, clamped 0..5) */
    uint8_t sp_base = (p_level >= 4) ? 3 : (uint8_t)(p_level - 1);
    for (uint8_t i = 0; i < p_pop_init; i++) {
        creature_t *c = &creatures[i];
        c->active = 1;
        c->species = (uint8_t)(sp_base + (rng_u8() % 3u));
        if (c->species >= NUM_SPECIES) c->species = NUM_SPECIES - 1;
        /* Place away from Ark */
        int16_t wx = (int16_t)((ARK_WX + 140 + i * 210 + (rng_u8() & 63)) & WORLD_MASK);
        c->wx = wx;
        c->dir = (rng_u8() & 1u) ? 1 : -1;
        if (c->species == 4) { /* Glider */
            c->state = 1;
            c->y = 144;
        } else {
            c->state = 0;
            c->y = ground_at(wx) - 12;
        }
    }

    /* Spawn ore crates + optional Relic */
    for (uint8_t i = 0; i < MAX_CRATES; i++) {
        crates[i].active = 1;
        crates[i].is_relic = (has_relic && i == MAX_CRATES - 1);
        int16_t wx = (int16_t)((ARK_WX + 220 + i * 260) & WORLD_MASK);
        crates[i].wx = wx;
        crates[i].y = ground_at(wx) - 12;
    }

    video_set_planet_scroll((int16_t)(shu_wx - SCREEN_W / 2));
    hud_top_clear();
    hud_bot_clear();
    planet_hud_draw();
    music_play("ROM:Planet.vgm");
}

/* ------------------------------------------------------------------ */
/* Update Subsystems                                                  */
/* ------------------------------------------------------------------ */

static void update_shuttle_and_beam(void)
{
    shu_beaming = false;

    if (shu_st == SHU_DEAD) {
        if (shu_rebuild_tm > 0) {
            shu_rebuild_tm--;
            /* After the 32-frame explosion, smoothly fast-scroll camera back to the Ark */
            if (shu_rebuild_tm <= 118u) {
                int16_t d = wrap_dx(ARK_WX, shu_wx);
                if (d > 8)       shu_wx = (int16_t)((shu_wx + 8) & WORLD_MASK);
                else if (d < -8) shu_wx = (int16_t)((shu_wx - 8) & WORLD_MASK);
                else             shu_wx = ARK_WX;
            }
        }
        if (shu_rebuild_tm == 0) {
            shu_st = SHU_DOCKED;
            shu_wx = ARK_WX;
            shu_y = PL_DOCK_Y;
            shu_vx = 0;
            sfx_player(SFX_DOCK, SFX_PRI_EVENT);
        }
        return;
    }

    if (shu_st == SHU_DOCKED) {
        shu_wx = ARK_WX;
        shu_y = PL_DOCK_Y;

        /* Early departure: hold X while calm */
        if (storm_phase == 0 && (in_held & IN_BTN2) && !(in_held & (IN_UP | IN_DOWN | IN_LEFT | IN_RIGHT | IN_FIRE))) {
            leave_hold++;
            if (leave_hold == 30) msg_center(1, "LEAVING IN 3...", C_GOLD);
            else if (leave_hold == 90) msg_center(1, "LEAVING IN 2...", C_GOLD);
            else if (leave_hold == 150) msg_center(1, "LEAVING IN 1...", C_GOLD);
            else if (leave_hold >= 210) {
                msg_clear();
                p_state = PS_DEPART;
                return;
            }
        } else if (leave_hold > 0) {
            if (leave_hold >= 30) msg_clear();
            leave_hold = 0;
        }

        /* Hold down to launch out of tunnel */
        if (in_held & IN_DOWN) {
            if (++shu_launch_hold >= 6) {
                shu_st = SHU_AUTO_OUT;
                shu_launch_hold = 0;
                sfx_player(SFX_LAUNCH, SFX_PRI_EVENT);
            }
        } else {
            shu_launch_hold = 0;
        }
        return;
    }

    if (shu_st == SHU_AUTO_OUT) {
        shu_y += 2;
        if (shu_y >= PL_YMIN + 4) {
            shu_y = PL_YMIN + 4;
            shu_st = SHU_FLYING;
            shu_can_dock = false;
        }
        return;
    }

    if (shu_st == SHU_AUTO_IN) {
        int16_t dx = wrap_dx(ARK_WX, shu_wx);
        if (dx > 1) shu_wx = (shu_wx + 1) & WORLD_MASK;
        else if (dx < -1) shu_wx = (shu_wx - 1) & WORLD_MASK;
        else shu_wx = ARK_WX;

        if (shu_wx == ARK_WX) {
            shu_y -= 2;
            if (shu_y <= PL_DOCK_Y) {
                shu_y = PL_DOCK_Y;
                shu_st = SHU_DOCKED;
                shu_vx = 0;
                sfx_player(SFX_DOCK, SFX_PRI_EVENT);
                deliver_cargo();
            }
        }
        return;
    }

    /* SHU_FLYING */
    /* Drop cargo with Button 2 */
    if ((in_pressed & IN_BTN2) && shu_cargo != CARGO_NONE) {
        drop_cargo();
        sfx_player(SFX_BLIP, SFX_PRI_LOW);
    }

    /* Tractor beam with Button 1 (stops shuttle dead!) */
    if ((in_held & IN_FIRE) && shu_cargo == CARGO_NONE) {
        shu_beaming = true;
        shu_vx = 0;
        if ((g_frame & 15u) == 0)
            sfx_player(SFX_BEAM, SFX_PRI_LOW);

        /* Lift a creature or crate under the shuttle (one at a time) */
        bool busy = false;
        for (uint8_t i = 0; i < MAX_CREATURES; i++) {
            creature_t *c = &creatures[i];
            if (c->active == 2) {
                busy = true;
                int16_t dx = wrap_dx(c->wx, shu_wx);
                if (dx < -BEAM_HALF || dx > BEAM_HALF) {
                    c->active = 1;
                    c->state = 1;
                    c->vy = 0;
                } else {
                    /* Creature rises and actively struggles in its facing direction */
                    if ((g_frame & 1u) == 0) c->y--;
                    uint8_t strug_mask = (c->species >= 2) ? 3u : 5u;
                    if ((g_frame % (strug_mask + 1u)) == 0) {
                        if ((rng_u8() & 7u) == 0) c->dir = -c->dir;
                        c->wx = (c->wx + c->dir) & WORLD_MASK;
                    }
                    if (c->y <= shu_y + 10) {
                        c->active = 3;
                        shu_cargo = CARGO_CREATURE;
                        shu_cargo_idx = i;
                        sfx_player(SFX_PICKUP, SFX_PRI_EVENT);
                    }
                }
                break;
            }
        }
        if (!busy) {
            for (uint8_t i = 0; i < MAX_CRATES; i++) {
                crate_t *k = &crates[i];
                if (k->active == 2) {
                    busy = true;
                    uint8_t mask = k->is_relic ? 7u : 3u;
                    if ((g_frame & mask) == 0) k->y--;
                    if (k->y <= shu_y + 10) {
                        k->active = 3;
                        shu_cargo = k->is_relic ? CARGO_RELIC : CARGO_ORE;
                        shu_cargo_idx = i;
                        sfx_player(SFX_PICKUP, SFX_PRI_EVENT);
                    }
                    break;
                }
            }
        }
        if (!busy) {
            /* Grab a new target under the beam */
            for (uint8_t i = 0; i < MAX_CREATURES; i++) {
                if (creatures[i].active == 1) {
                    int16_t dx = wrap_dx(creatures[i].wx, shu_wx);
                    if (dx >= -BEAM_HALF && dx <= BEAM_HALF && creatures[i].y > shu_y) {
                        if (!voy_has_hold_room()) {
                            msg_flash("HOLDS FULL!", C_RED, 45);
                        } else {
                            creatures[i].active = 2;
                            busy = true;
                        }
                        break;
                    }
                }
            }
            if (!busy) {
                for (uint8_t i = 0; i < MAX_CRATES; i++) {
                    if (crates[i].active == 1) {
                        int16_t dx = wrap_dx(crates[i].wx, shu_wx);
                        if (dx >= -BEAM_HALF && dx <= BEAM_HALF && crates[i].y > shu_y) {
                            if (!crates[i].is_relic && !voy_has_hold_room()) {
                                msg_flash("HOLDS FULL!", C_RED, 45);
                            } else {
                                crates[i].active = 2;
                            }
                            break;
                        }
                    }
                }
            }
        }
        return;
    }

    /* Release any half-beamed targets when beam stops */
    for (uint8_t i = 0; i < MAX_CREATURES; i++) {
        if (creatures[i].active == 2) { creatures[i].active = 1; creatures[i].state = 1; creatures[i].vy = 0; }
    }
    for (uint8_t i = 0; i < MAX_CRATES; i++) {
        if (crates[i].active == 2) { crates[i].active = 1; }
    }

    /* Horizontal flight with cargo weight */
    int16_t vmax = (shu_cargo == CARGO_RELIC) ? 10 : ((shu_cargo == CARGO_ORE) ? 18 : 36);
    if (in_held & IN_LEFT) {
        shu_vx -= 4;
        if (shu_vx < -vmax) shu_vx = -vmax;
    } else if (in_held & IN_RIGHT) {
        shu_vx += 4;
        if (shu_vx > vmax) shu_vx = vmax;
    } else {
        shu_vx -= (shu_vx >> 3);
        if (shu_vx > -3 && shu_vx < 3) shu_vx = 0;
    }
    shu_subx += shu_vx;
    int16_t step_x = shu_subx >> 4;
    shu_subx -= (step_x << 4);
    shu_wx = (shu_wx + step_x) & WORLD_MASK;

    /* Re-enable docking once shuttle has dropped 12px or moved outside the doorway */
    int16_t dx_ark = wrap_dx(shu_wx, ARK_WX);
    if (!shu_can_dock) {
        if (shu_y >= PL_YMIN + 16 || dx_ark < -DOCK_ZONE || dx_ark > DOCK_ZONE || !(in_held & (IN_LEFT | IN_RIGHT))) {
            if (shu_y >= PL_YMIN + 16 || dx_ark < -DOCK_ZONE || dx_ark > DOCK_ZONE)
                shu_can_dock = true;
        }
    }

    /* Vertical flight */
    bool move_v = (shu_cargo == CARGO_RELIC) ? ((g_frame & 3u) == 0) :
                  ((shu_cargo == CARGO_ORE) ? ((g_frame & 1u) == 0) : true);
    if (move_v) {
        if (in_held & IN_UP) {
            /* Allow docking with Up, Up+Left, or Up+Right when in the doorway */
            if (shu_y <= PL_YMIN + 16 && dx_ark >= -DOCK_ZONE && dx_ark <= DOCK_ZONE &&
                (shu_can_dock || !(in_held & (IN_LEFT | IN_RIGHT)))) {
                shu_st = SHU_AUTO_IN;
                shu_vx = 0;
                return;
            }
            if (shu_y > PL_YMIN) shu_y -= 1;
        } else if (in_held & IN_DOWN) {
            if (shu_y < PL_YMAX) shu_y += 1;
        }
    }
}

static void update_creatures_and_crates(void)
{
    for (uint8_t i = 0; i < MAX_CREATURES; i++) {
        creature_t *c = &creatures[i];
        if (c->active != 1) continue;

        int16_t gy = ground_at(c->wx) - 12;
        if (c->react_cd > 0) c->react_cd--;

        /* Check low shuttle overhead (75% react chance) */
        bool react = false;
        if (shu_st == SHU_FLYING && c->react_cd == 0) {
            int16_t dx = wrap_dx(c->wx, shu_wx);
            if (dx >= -52 && dx <= 52 && shu_y >= 112) {
                c->react_cd = 26;
                if (rng_u8() < 192) react = true;
            }
        }

        if (c->species == 4) {
            /* Glider: bobs mid-air, accelerates away when shuttle approaches */
            if (react) {
                c->dir = (wrap_dx(c->wx, shu_wx) >= 0) ? 1 : -1;
                c->timer = 30;
            }
            int8_t step = (c->timer > 0) ? (int8_t)(c->dir * 2) : c->dir;
            if (c->timer > 0) c->timer--;
            c->wx = (c->wx + step) & WORLD_MASK;
            c->y = (int16_t)(146 + ((sin256((uint8_t)(g_frame * 3u + i * 50u)) * 18) >> 8));
            continue;
        }

        if (c->state == 1) {
            /* Falling or jumping */
            c->y += c->vy;
            if ((g_frame & 2u) == 0 && c->vy < 4) c->vy++;
            int8_t air_step = (c->species == 1) ? (int8_t)(c->dir * 2) : c->dir;
            c->wx = (c->wx + air_step) & WORLD_MASK;
            if (c->y >= gy) {
                c->y = gy;
                c->state = 0;
                c->vy = 0;
            }
        } else {
            c->y = gy;
            if (react) {
                int8_t away = (wrap_dx(c->wx, shu_wx) >= 0) ? 1 : -1;
                if (c->species == 1) { /* Hopper leaps high away */
                    c->dir = away;
                    c->state = 1;
                    c->vy = -4;
                } else if (c->species == 0) { /* Walker reverses randomly & hurries */
                    c->dir = (rng_u8() & 1u) ? away : -away;
                    c->timer = 28;
                } else { /* Runner / Dodger / Skitterer sprint away */
                    c->dir = away;
                    c->timer = 36;
                }
            }
            if (c->timer > 0) c->timer--;
            int8_t step = 0;
            if (c->species == 5) {
                /* Skitterer: always fast & twitchy */
                step = (int8_t)(c->dir * ((g_frame & 1u) ? 2 : 1));
                if ((rng_u8() & 15u) == 0) c->dir = -c->dir;
            } else if (c->timer > 0) {
                /* Fleeing burst */
                step = (c->species >= 2) ? (int8_t)(c->dir * 2) : c->dir;
            } else if (c->species == 2 || c->species == 3) {
                step = c->dir;
            } else if ((g_frame & 1u) == 0) {
                step = c->dir;
            }
            c->wx = (c->wx + step) & WORLD_MASK;
            if (c->species == 1 && (rng_u8() & 63u) == 0) {
                c->state = 1;
                c->vy = -3;
            } else if ((rng_u8() & 47u) == 0) {
                c->dir = -c->dir;
            }
        }
    }

    /* Falling crates */
    for (uint8_t i = 0; i < MAX_CRATES; i++) {
        crate_t *k = &crates[i];
        if (k->active == 1) {
            int16_t gy = ground_at(k->wx) - 12;
            if (k->y < gy) k->y += 2;
            if (k->y > gy) k->y = gy;
        }
    }
}

static void update_defenses(void)
{
    /* Step interval scales with planet level 1..5 */
    uint8_t step_gap = (p_level >= 4) ? 1 : (uint8_t)(5 - p_level);
    if (pulse_warn == 0 && pulse_on == 0) {
        if (--gun_step_tm == 0) {
            gun_step_tm = step_gap;
            if (gun_dwell > 0) {
                gun_dwell--;
            } else {
                gun_y += gun_dir;
                if (gun_y <= PL_YMIN) { gun_y = PL_YMIN; gun_dir = 1; gun_dwell = 9; }
                else if (gun_y >= PL_YMAX) { gun_y = PL_YMAX; gun_dir = -1; gun_dwell = 9; }
            }
        }
    }

    if (pulse_on > 0) {
        pulse_on--;
        /* Check shuttle collision with screen-wide pulse */
        if (shu_st == SHU_FLYING) {
            int16_t dy = shu_y - gun_y;
            if (dy >= -6 && dy <= 6) {
                kill_shuttle();
            }
        }
        return;
    }

    if (pulse_warn > 0) {
        if (--pulse_warn == 0) {
            pulse_on = 6;
            sfx_enemy(SFX_PULSE, SFX_PRI_HIT);
        }
        return;
    }

    if (--pulse_tm == 0) {
        pulse_warn = 24;
        uint16_t base = (p_level >= 5) ? 45 : (uint16_t)(165 - p_level * 25);
        /* Randomize 0.5x to 1.5x period so no line is safe to camp */
        uint16_t half = base >> 1;
        pulse_tm = (uint16_t)(half + (rng_u8() % (base + 1u)));
    }
}

static void update_storm(void)
{
    if (storm_phase == 0) {
        if (storm_clock > 0 && --storm_clock == 0) {
            storm_phase = 1;
            storm_delay = 10u * 60u;
            music_play("ROM:Pirates.vgm");
            sfx_enemy(SFX_KLAXON, SFX_PRI_TOP);
            msg_flash("STORM COMING - DOCK NOW!", C_RED, 120);
        }
        return;
    }

    if (storm_phase == 1) {
        if ((storm_delay % 120u) == 0 && storm_delay > 0)
            sfx_enemy(SFX_KLAXON, SFX_PRI_EVENT);
        if (--storm_delay == 0) {
            storm_phase = 2;
            storm_dir = rng_u8() % 3u; /* 0=top, 1=left, 2=right */
            int16_t spd = (int16_t)(14 + p_level * 3);
            if (spd > 32) spd = 32;
            if (storm_dir == 0) {
                storm_x = 160 << 4; storm_y = 18 << 4;
                storm_vx = 0; storm_vy = spd >> 1;
            } else if (storm_dir == 1) {
                storm_x = 8 << 4; storm_y = (ARK_ORBIT_Y + 14) << 4;
                storm_vx = spd; storm_vy = 0;
            } else {
                storm_x = (SCREEN_W - 8) << 4; storm_y = (ARK_ORBIT_Y + 14) << 4;
                storm_vx = -spd; storm_vy = 0;
            }
        }
        return;
    }

    if (storm_phase == 2) {
        /* Docked player fires Ark laser Up/Left/Right (fresh push) */
        uint8_t j = in_joy8();
        if (shu_st == SHU_DOCKED && ark_bolt_life == 0 && j != prev_joy) {
            if (j == 0 || j == 2 || j == 6) {
                ark_bolt_life = 40;
                ark_bolt_x = 160;
                ark_bolt_y = ARK_ORBIT_Y + 14;
                ark_bolt_dx = (j == 2) ? 5 : ((j == 6) ? -5 : 0);
                ark_bolt_dy = (j == 0) ? -4 : 0;
                sfx_player(SFX_LASER, SFX_PRI_SHOT);
            }
        }
        prev_joy = j;

        if (ark_bolt_life > 0) {
            ark_bolt_x += ark_bolt_dx;
            ark_bolt_y += ark_bolt_dy;
            ark_bolt_life--;
            int16_t rx = storm_x >> 4, ry = storm_y >> 4;
            if (ark_bolt_x >= rx - 12 && ark_bolt_x <= rx + 12 &&
                ark_bolt_y >= ry - 12 && ark_bolt_y <= ry + 12) {
                ark_bolt_life = 0;
                storm_phase = 3;
                g_voy.storms++;
                ach_check_all();
                sfx_enemy(SFX_BOOM, SFX_PRI_TOP);
                msg_center(1, "ALL CLEAR!", C_GREEN);
                p_state = PS_CLEAR;
                p_timer = 75;
                return;
            }
        }

        storm_x += storm_vx;
        storm_y += storm_vy;
        int16_t rx = storm_x >> 4, ry = storm_y >> 4;
        if (rx >= 134 && rx <= 186 && ry >= ARK_ORBIT_Y && ry <= ARK_ORBIT_Y + 28) {
            storm_phase = 3;
            msg_center(1, "ARK HIT BY STORM!", C_RED);
            if (shu_st != SHU_DOCKED && shu_st != SHU_DEAD)
                kill_shuttle();
            planet_ark_damage(16);
            if (p_state != PS_LOST) {
                p_state = PS_CLEAR;
                p_timer = 75;
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* Render Sprites                                                     */
/* ------------------------------------------------------------------ */

static void planet_render(void)
{
    int16_t cam_x = (int16_t)((shu_wx - SCREEN_W / 2) & WORLD_MASK);
    video_set_planet_scroll(cam_x);

    uint8_t idx = 0;
    spr_hide_all();

    /* Ark & Tunnel (if on screen) */
    int16_t ark_sx = wrap_dx(ARK_WX, shu_wx) + SCREEN_W / 2;
    if (ark_sx > -40 && ark_sx < SCREEN_W + 40 && p_state != PS_LOST) {
        uint16_t ark_ptr = (uint16_t)(XRAM_IMG_ARK + (((g_frame >> 3) & 1u) * ARK_FRAME_BYTES));
        spr_place(idx++, ark_ptr, PAL_ARK, MODE5_SIZE(ARK_W, ARK_H), MODE5_4BPP,
                  ark_sx - (int16_t)(ARK_W / 2), p_ark_y);
        if (p_state == PS_PLAY || p_state == PS_CLEAR) {
            spr_place(idx++, XRAM_IMG_TUNNEL, PAL_ARK, MODE5_SIZE(TUNNEL_W, TUNNEL_H), MODE5_4BPP,
                      ark_sx - (int16_t)(TUNNEL_W / 2), TUNNEL_Y);
        }
    }

    /* Tractor Beam (tiled vertically from shuttle down to ground) */
    if (shu_beaming && shu_st == SHU_FLYING) {
        int16_t gy = ground_at(shu_wx);
        uint16_t bp = (uint16_t)(XRAM_IMG_BEAM + (((g_frame >> 2) & 3u) * BEAM_FRAME_BYTES));
        for (int16_t by = shu_y + 8; by < gy && idx < SPR_TOTAL_COUNT - 12; by += 16) {
            spr_place(idx++, bp, PAL_SAUCER, MODE5_SIZE(BEAM_W, BEAM_H), MODE5_4BPP,
                      SCREEN_W / 2 - (int16_t)(BEAM_W / 2), by);
        }
    }

    /* Shuttle (always centered X=160 while flying/auto) */
    if (shu_st == SHU_FLYING || shu_st == SHU_AUTO_OUT || shu_st == SHU_AUTO_IN) {
        uint16_t sp = (uint16_t)(XRAM_IMG_SAUCER + (((g_frame >> 2) & 3u) * SAUCER_FRAME_BYTES));
        spr_place(idx++, sp, PAL_SAUCER, MODE5_SIZE(SAUCER_W, SAUCER_H), MODE5_4BPP,
                  SCREEN_W / 2 - (int16_t)(SAUCER_W / 2), shu_y - (int16_t)(SAUCER_H / 2));
        /* Carried cargo slung beneath shuttle */
        if (shu_cargo == CARGO_CREATURE) {
            uint8_t spc = creatures[shu_cargo_idx].species;
            uint16_t cp = (uint16_t)(XRAM_IMG_CREATURES + (spc * 2u) * CREATURES_FRAME_BYTES);
            spr_place(idx++, cp, PAL_CREATURES, MODE5_SIZE(12, 12), MODE5_4BPP,
                      SCREEN_W / 2 - 6, shu_y + 6);
        } else if (shu_cargo == CARGO_ORE || shu_cargo == CARGO_RELIC) {
            uint8_t cf = (shu_cargo == CARGO_RELIC) ? (uint8_t)(1u + ((g_frame >> 2) & 1u)) : 0u;
            spr_place(idx++, (uint16_t)(XRAM_IMG_CARGO + cf * CARGO_FRAME_BYTES), PAL_CREATURES,
                      MODE5_SIZE(12, 12), MODE5_4BPP, SCREEN_W / 2 - 6, shu_y + 6);
        }
    } else if (shu_st == SHU_DEAD && shu_rebuild_tm > 118u) {
        /* 32-frame multi-burst shuttle explosion animation */
        uint8_t age = (uint8_t)(150u - shu_rebuild_tm);
        if (age < 16u && idx < SPR_TOTAL_COUNT) {
            uint8_t f = age >> 2;
            spr_place(idx++, (uint16_t)(XRAM_IMG_BOOM + f * BOOM_FRAME_BYTES), PAL_ROCK,
                      MODE5_SIZE(BOOM_W, BOOM_H), MODE5_4BPP, SCREEN_W / 2 - 8, shu_y - 8);
        }
        if (age >= 4u && age < 24u && idx + 1 < SPR_TOTAL_COUNT) {
            uint8_t a2 = (uint8_t)(age - 4u);
            uint8_t f = (a2 / 5u) & 3u;
            int16_t sp = (int16_t)(4 + (a2 >> 1));
            uint16_t bp = (uint16_t)(XRAM_IMG_BOOM + f * BOOM_FRAME_BYTES);
            spr_place(idx++, bp, PAL_ROCK, MODE5_SIZE(BOOM_W, BOOM_H), MODE5_4BPP,
                      SCREEN_W / 2 - 8 - sp, shu_y - 8 - (sp >> 1));
            spr_place(idx++, bp, PAL_ROCK, MODE5_SIZE(BOOM_W, BOOM_H), MODE5_4BPP,
                      SCREEN_W / 2 - 8 + sp, shu_y - 8 + (sp >> 1));
        }
        if (age >= 12u && age < 32u && idx < SPR_TOTAL_COUNT) {
            uint8_t f = ((uint8_t)(age - 12u) / 5u) & 3u;
            spr_place(idx++, (uint16_t)(XRAM_IMG_BOOM + f * BOOM_FRAME_BYTES), PAL_ROCK,
                      MODE5_SIZE(BOOM_W, BOOM_H), MODE5_4BPP, SCREEN_W / 2 - 12, shu_y - 2);
        }
    }

    /* Creatures */
    for (uint8_t i = 0; i < MAX_CREATURES && idx < SPR_TOTAL_COUNT; i++) {
        creature_t *c = &creatures[i];
        if (c->active != 1 && c->active != 2) continue;
        int16_t sx = wrap_dx(c->wx, shu_wx) + SCREEN_W / 2;
        if (sx < -16 || sx >= SCREEN_W + 16) continue;
        uint8_t f = (uint8_t)(c->species * 2u + (((g_frame >> 3) + i) & 1u));
        uint8_t opt = (c->dir < 0) ? (MODE5_4BPP | MODE5_HFLIP) : MODE5_4BPP;
        spr_place(idx++, (uint16_t)(XRAM_IMG_CREATURES + f * CREATURES_FRAME_BYTES), PAL_CREATURES,
                  MODE5_SIZE(12, 12), opt, sx - 6, c->y);
    }

    /* Crates & Relic */
    for (uint8_t i = 0; i < MAX_CRATES && idx < SPR_TOTAL_COUNT; i++) {
        crate_t *k = &crates[i];
        if (k->active != 1 && k->active != 2) continue;
        int16_t sx = wrap_dx(k->wx, shu_wx) + SCREEN_W / 2;
        if (sx < -16 || sx >= SCREEN_W + 16) continue;
        uint8_t f = k->is_relic ? (uint8_t)(1u + ((g_frame >> 2) & 1u)) : 0u;
        spr_place(idx++, (uint16_t)(XRAM_IMG_CARGO + f * CARGO_FRAME_BYTES), PAL_CREATURES,
                  MODE5_SIZE(12, 12), MODE5_4BPP, sx - 6, k->y);
    }

    /* Tower Guns (visible towers) */
    uint8_t gf = (pulse_warn > 0 && (g_frame & 4u)) ? 1u : 0u;
    uint16_t gp = (uint16_t)(XRAM_IMG_GUN + gf * GUN_FRAME_BYTES);
    uint8_t wpal = (uint8_t)(PAL_WORLD_0 + p_world);
    for (uint8_t t = 0; t < NUM_TOWERS && idx < SPR_TOTAL_COUNT; t++) {
        int16_t twx = (int16_t)(TOWER_OFFSET + t * TOWER_SPACING);
        int16_t sx = wrap_dx(twx, shu_wx) + SCREEN_W / 2;
        if (sx >= -12 && sx < SCREEN_W + 12) {
            spr_place(idx++, gp, wpal, MODE5_SIZE(GUN_W, GUN_H), MODE5_4BPP, sx - 4, gun_y - 4);
        }
    }

    /* Screen-wide Horizontal Defense Pulse */
    if (pulse_on > 0) {
        uint16_t pp = (uint16_t)(XRAM_IMG_PULSE + ((g_frame & 1u) * PULSE_FRAME_BYTES));
        for (int16_t px = 0; px < SCREEN_W && idx < SPR_TOTAL_COUNT; px += 32) {
            spr_place(idx++, pp, wpal, MODE5_SIZE(PULSE_W, PULSE_H), MODE5_4BPP, px, gun_y - 2);
        }
    }

    /* Storm Meteor & Ark Laser Bolt */
    if (storm_phase == 2 && idx < SPR_TOTAL_COUNT) {
        int16_t sx = (storm_x >> 4) + (ark_sx - 160);
        int16_t sy = storm_y >> 4;
        uint8_t rf = (g_frame >> 3) & 3u;
        spr_place(idx++, (uint16_t)(XRAM_IMG_ROCK_L + rf * ROCK_L_FRAME_BYTES), PAL_ROCK,
                  MODE5_SIZE(ROCK_L_W, ROCK_L_H), MODE5_4BPP, sx - 12, sy - 12);
    }
    if (ark_bolt_life > 0 && idx < SPR_TOTAL_COUNT) {
        uint8_t bf = (ark_bolt_dy != 0) ? 0u : 2u;
        spr_place(idx++, (uint16_t)(XRAM_IMG_SHOTS + bf * SHOTS_FRAME_BYTES), PAL_ROCK,
                  MODE5_SIZE(8, 8), MODE5_4BPP, ark_bolt_x - 4, ark_bolt_y - 4);
    }
}

/* ------------------------------------------------------------------ */
/* Main Planet Frame                                                  */
/* ------------------------------------------------------------------ */

planet_result_t planet_update(void)
{
    switch (p_state) {
    case PS_ARRIVE:
        p_ark_y += 2;
        if (p_ark_y >= ARK_ORBIT_Y) {
            p_ark_y = ARK_ORBIT_Y;
            p_state = PS_PLAY;
            msg_flash(WORLD_NAMES[p_world], C_CYAN, 60);
        }
        break;

    case PS_PLAY:
        update_shuttle_and_beam();
        update_creatures_and_crates();
        update_defenses();
        update_storm();
        break;

    case PS_CLEAR:
        if (--p_timer == 0) {
            msg_clear();
            p_state = PS_DEPART;
        }
        break;

    case PS_DEPART:
        p_ark_y -= 2;
        if (p_ark_y < -(int16_t)ARK_H)
            return PLANET_DONE;
        break;

    case PS_LOST:
        if (p_timer > 0 && --p_timer == 0)
            return PLANET_ARK_LOST;
        break;
    }

    if ((g_frame & 3u) == 0)
        planet_hud_draw();
    planet_render();
    return PLANET_RUNNING;
}
