#include <string.h>
#include "achieve.h"
#include "galaxy.h"
#include "game.h"
#include "input.h"
#include "math_fixed.h"
#include "music.h"
#include "save.h"
#include "sfx.h"
#include "video.h"

uint8_t map_enc_wave = 1;
uint8_t map_dest_sector = 0;

/* 8 compass directions: 0=N, 1=NE, 2=E, 3=SE, 4=S, 5=SW, 6=W, 7=NW */
static const int8_t gdx[8] = {  0,  1,  1,  1,  0, -1, -1, -1 };
static const int8_t gdy[8] = { -1, -1,  0,  1,  1,  1,  0, -1 };
static const char *const dir_lbl[8] = { "N ", "NE", "E ", "SE", "S ", "SW", "W ", "NW" };
static const char *const tier_lbl[4] = { "SAFE SPACE", "CORE", "MID-RIM", "OUTER RIM" };
static const uint8_t tier_col[4] = { C_GREEN, C_CYAN, C_GOLD, C_RED };

/* Map view state */
#define MV_SECTOR  0
#define MV_CHART   1
#define MV_CLOSEUP 2

static uint8_t mv_mode = MV_SECTOR;
static uint8_t mv_sel = 0;
static uint8_t mv_nitems = 0;
static uint8_t mv_items[12]; /* 0..63=warp to sec, 0x80=land, 0x81=dock, 0x82=save */
static uint8_t mv_item_dir[12];
static uint8_t chart_cx = 5, chart_cy = 3;
static bool mv_confirm_exit = false;

/* ------------------------------------------------------------------ */
/* Helpers                                                            */
/* ------------------------------------------------------------------ */

static uint8_t sec_tier(uint8_t s)
{
    return (uint8_t)((g_gx.info[s] >> 6) & 3u);
}

static uint8_t sec_type(uint8_t s)
{
    return (uint8_t)(g_gx.info[s] & 3u);
}

static uint8_t sec_sub(uint8_t s)
{
    return (uint8_t)((g_gx.info[s] >> 2) & 7u);
}

uint8_t galaxy_planet_level(uint8_t sec)
{
    uint8_t lv;
    if (sec < 10) lv = 1;
    else if (sec < 16) lv = 2;
    else if (sec < 28) lv = 3;
    else if (sec < 44) lv = 4;
    else lv = 5;
    if ((g_gx.info[sec] & 0x20u) && lv > 1)
        lv--;
    return lv;
}

uint8_t galaxy_planet_pop(uint8_t sec)
{
    uint8_t m = g_voy.planet_miss[sec];
    return (m >= PL_POP_MAX) ? 0 : (uint8_t)(PL_POP_MAX - m);
}

uint8_t galaxy_port_index(uint8_t sec)
{
    for (uint8_t i = 0; i < PT_NPORTS; i++)
        if (g_gx.port_sec[i] == sec) return i;
    return 0;
}

static int8_t sector_at_cell(uint8_t x, uint8_t y)
{
    uint8_t code = (uint8_t)((y << 4) | x);
    for (uint8_t s = 0; s < GX_SECTORS; s++)
        if (g_gx.cell[s] == code) return (int8_t)s;
    return -1;
}

static int8_t neighbor_sector(uint8_t s, uint8_t d)
{
    if (!(g_gx.warp[s] & (uint8_t)(1u << d)))
        return -1;
    int8_t x = (int8_t)((g_gx.cell[s] & 0x0Fu) + gdx[d]);
    int8_t y = (int8_t)((g_gx.cell[s] >> 4) + gdy[d]);
    if (x < 0 || x >= GX_GRID_W || y < 0 || y >= GX_GRID_H)
        return -1;
    return sector_at_cell((uint8_t)x, (uint8_t)y);
}

/* ------------------------------------------------------------------ */
/* Seeded 64-Sector Galaxy Generator                                  */
/* ------------------------------------------------------------------ */

void galaxy_generate(uint32_t seed)
{
    uint32_t saved_rng = rng_state();
    rng_seed(seed ? seed : 0xCA206502u);

    memset(&g_gx, 0, sizeof(g_gx));

    /* Grid-indexed scratch (reuse idle spr[] buffer during galaxy generation) */
    uint8_t *gc_warp = (uint8_t *)spr;
    int8_t  *gc_sec  = (int8_t *)((uint8_t *)spr + 128);
    uint8_t *taken   = (uint8_t *)spr + 256;
    uint8_t n_taken = 0, n_ext = 0;

    memset(gc_warp, 0, 128);
    memset(gc_sec, -1, 128);

    /* Start spanning tree at home cell (5, 3) */
    uint8_t home = (3u << 4) | 5u;
    gc_sec[home] = 0;
    taken[n_taken++] = home;

    while (n_taken < GX_SECTORS) {
        uint8_t base = taken[rng_u8() % n_taken];
        uint8_t bx = base & 0x0Fu;
        uint8_t by = base >> 4;
        uint8_t d0 = rng_u8() & 7u;
        for (uint8_t k = 0; k < 8; k++) {
            uint8_t d = (uint8_t)((d0 + k) & 7u);
            int8_t nx = (int8_t)bx + gdx[d];
            int8_t ny = (int8_t)by + gdy[d];
            if (nx < 0 || nx >= GX_GRID_W || ny < 0 || ny >= GX_GRID_H)
                continue;
            uint8_t nc = (uint8_t)(((uint8_t)ny << 4) | (uint8_t)nx);
            if (gc_sec[nc] < 0) {
                gc_sec[nc] = (int8_t)n_taken;
                gc_warp[base] |= (uint8_t)(1u << d);
                gc_warp[nc]   |= (uint8_t)(1u << ((d + 4u) & 7u));
                taken[n_taken++] = nc;
                break;
            }
        }
    }

    /* Add up to 60 extra warps; first 5 are one-way! */
    for (uint8_t t = 0; t < 60; t++) {
        uint8_t c = taken[rng_u8() % GX_SECTORS];
        uint8_t d = rng_u8() & 7u;
        if (gc_warp[c] & (uint8_t)(1u << d)) continue;
        int8_t nx = (int8_t)(c & 0x0Fu) + gdx[d];
        int8_t ny = (int8_t)(c >> 4) + gdy[d];
        if (nx < 0 || nx >= GX_GRID_W || ny < 0 || ny >= GX_GRID_H)
            continue;
        uint8_t nc = (uint8_t)(((uint8_t)ny << 4) | (uint8_t)nx);
        if (gc_sec[nc] < 0) continue;
        gc_warp[c] |= (uint8_t)(1u << d);
        if (n_ext < 5) {
            n_ext++;
        } else {
            gc_warp[nc] |= (uint8_t)(1u << ((d + 4u) & 7u));
        }
    }

    /* BFS from home to number sectors 0..63 by distance (reuse taken[] & gc_sec[]) */
    uint8_t *q = taken;
    int8_t  *cell_to_s = gc_sec;
    memset(cell_to_s, -1, 128);
    uint8_t qh = 0, qt = 0;
    q[qt++] = home;
    cell_to_s[home] = 0;

    while (qh < qt) {
        uint8_t c = q[qh++];
        uint8_t bx = c & 0x0Fu, by = c >> 4;
        for (uint8_t d = 0; d < 8; d++) {
            if (!(gc_warp[c] & (uint8_t)(1u << d))) continue;
            uint8_t nc = (uint8_t)(((uint8_t)(by + gdy[d]) << 4) | (uint8_t)(bx + gdx[d]));
            if (cell_to_s[nc] < 0 && qt < GX_SECTORS) {
                cell_to_s[nc] = (int8_t)qt;
                q[qt++] = nc;
            }
        }
    }

    for (uint8_t s = 0; s < GX_SECTORS; s++) {
        uint8_t c = q[s];
        g_gx.cell[s] = c;
        g_gx.warp[s] = gc_warp[c];
        uint8_t tier = (s < 10) ? TIER_SAFE : (s < 15 ? TIER_CORE : (s < 40 ? TIER_MID : TIER_OUTER));
        g_gx.info[s] = (uint8_t)(tier << 6);
    }

    /* Compute danger = jumps back to Safe Space (0..9) via reverse BFS (reuse gc_warp[]) */
    uint8_t *dist = gc_warp;
    memset(dist, 0xFF, GX_SECTORS);
    qh = 0; qt = 0;
    for (uint8_t s = 0; s < 10; s++) {
        dist[s] = 0;
        q[qt++] = s;
    }
    while (qh < qt) {
        uint8_t u = q[qh++];
        /* Find sectors v that have a warp to u */
        uint8_t ux = g_gx.cell[u] & 0x0Fu, uy = g_gx.cell[u] >> 4;
        for (uint8_t d = 0; d < 8; d++) {
            int8_t vx = (int8_t)ux + gdx[d];
            int8_t vy = (int8_t)uy + gdy[d];
            if (vx < 0 || vx >= GX_GRID_W || vy < 0 || vy >= GX_GRID_H) continue;
            uint8_t vc = (uint8_t)(((uint8_t)vy << 4) | (uint8_t)vx);
            int8_t v = cell_to_s[vc];
            if (v >= 0 && dist[v] == 0xFF && (g_gx.warp[v] & (uint8_t)(1u << ((d + 4u) & 7u)))) {
                dist[v] = (uint8_t)(dist[u] + 1u);
                q[qt++] = (uint8_t)v;
            }
        }
    }
    for (uint8_t s = 0; s < GX_SECTORS; s++) {
        uint8_t d = (dist[s] == 0xFF) ? 5 : dist[s];
        if (d < 1) d = 1;
        if (d > 10) d = 10;
        g_gx.danger[s] = d;
    }

    /* Place 11 Starports: 0 (BBB), 2 more safe (1st SSS), 2 core, 3 mid, 3 outer */
    uint8_t pcnt = 0;
    /* Port 0: Home sector 0, class 0 (BBB = buys all 3) */
    g_gx.info[0] |= GT_PORT | (0u << 2);
    g_gx.port_sec[pcnt++] = 0;

    static const uint8_t p_lo[10] = { 1, 1, 10, 10, 15, 15, 15, 40, 40, 40 };
    static const uint8_t p_hi[10] = { 9, 9, 14, 14, 39, 39, 39, 63, 63, 63 };
    for (uint8_t i = 0; i < 10; i++) {
        uint8_t s;
        for (uint8_t tries = 0; tries < 64; tries++) {
            s = (uint8_t)(p_lo[i] + (rng_u8() % (uint8_t)(p_hi[i] - p_lo[i] + 1u)));
            if ((g_gx.info[s] & 3u) == GT_EMPTY) break;
        }
        uint8_t cls;
        if (i == 0) cls = 7; /* SSS: sells everything in Safe Space */
        else cls = (uint8_t)(1u + (rng_u8() % 6u)); /* mixed B/S */
        g_gx.info[s] |= (uint8_t)(GT_PORT | (cls << 2));
        g_gx.port_sec[pcnt++] = s;
    }

    /* Populate remaining sectors: 40% planets, 12% nebulae (outside safe) */
    uint8_t relic_cand = 40;
    for (uint8_t s = 1; s < GX_SECTORS; s++) {
        if ((g_gx.info[s] & 3u) != GT_EMPTY) continue;
        uint8_t r = rng_u8();
        if (r < 102) { /* 40% planet */
            uint8_t w = rng_u8() % NUM_WORLDS;
            uint8_t easy = (rng_u8() & 1u) ? 0x20u : 0u;
            g_gx.info[s] |= (uint8_t)(GT_PLANET | (w << 2) | easy);
            if (s >= 40) relic_cand = s;
        } else if (r < 133 && s >= 10) { /* 12% nebula */
            g_gx.info[s] |= GT_NEBULA;
        }
    }

    /* Ensure relic sits on an Outer Rim planet */
    if ((g_gx.info[relic_cand] & 3u) != GT_PLANET) {
        g_gx.info[60] = (uint8_t)((TIER_OUTER << 6) | GT_PLANET | ((rng_u8() % NUM_WORLDS) << 2));
        relic_cand = 60;
    }
    if (g_voy.relic_sec == 0)
        g_voy.relic_sec = relic_cand;

    spr_hide_all();
    rng_seed(saved_rng);
}

void galaxy_init_voyage_stocks(void)
{
    for (uint8_t p = 0; p < PT_NPORTS; p++) {
        uint8_t cls = sec_sub(g_gx.port_sec[p]);
        for (uint8_t c = 0; c < 3; c++) {
            /* If port sells commodity c (bit set): starts at PT_NORMAL (12);
             * if it buys commodity c (bit 0): starts at 0 bought. */
            g_voy.port_stock[p][c] = (cls & (1u << c)) ? PT_NORMAL : 0;
        }
        g_voy.port_glut[p] = 0;
    }
    galaxy_visit(0);
}

void galaxy_visit(uint8_t sec)
{
    if (sec >= GX_SECTORS) return;
    g_voy.visited[sec] |= 3u;
    for (uint8_t d = 0; d < 8; d++) {
        int8_t nb = neighbor_sector(sec, d);
        if (nb >= 0) g_voy.visited[nb] |= 2u;
    }
    ach_check_all();
}

void galaxy_step_turn(void)
{
    g_voy.stardate++;
    /* Port stock steps back toward normal every jump */
    for (uint8_t p = 0; p < PT_NPORTS; p++) {
        uint8_t cls = sec_sub(g_gx.port_sec[p]);
        for (uint8_t c = 0; c < 3; c++) {
            if (cls & (1u << c)) {
                if (g_voy.port_stock[p][c] < PT_NORMAL) g_voy.port_stock[p][c]++;
            } else {
                if (g_voy.port_stock[p][c] > 0) g_voy.port_stock[p][c]--;
            }
        }
    }
    /* Glut decays every 2 jumps */
    if (++g_voy.tick_port >= 2) {
        g_voy.tick_port = 0;
        for (uint8_t p = 0; p < PT_NPORTS; p++)
            if (g_voy.port_glut[p] > 0) g_voy.port_glut[p]--;
    }
    /* Planets repopulate every 3 jumps */
    if (++g_voy.tick_planet >= 3) {
        g_voy.tick_planet = 0;
        for (uint8_t s = 0; s < GX_SECTORS; s++)
            if (g_voy.planet_miss[s] > 0) g_voy.planet_miss[s]--;
    }
}

bool galaxy_roll_encounter(uint8_t sec)
{
    uint8_t tier = sec_tier(sec);
    if (tier == TIER_SAFE) return false;
    static const uint8_t odds[4] = { 0, 77, 115, 154 }; /* 0%, 30%, 45%, 60% of 256 */
    uint16_t ch = odds[tier];
    if (sec_type(sec) == GT_NEBULA) ch += 64; /* +25% */
    if (ch > 240) ch = 240;
    if (rng_u8() < (uint8_t)ch) {
        uint8_t w = (uint8_t)(g_gx.danger[sec] + (rng_u8() & 1u));
        if (w < 1) w = 1;
        if (w > 10) w = 10;
        map_enc_wave = w;
        return true;
    }
    return false;
}

/* ------------------------------------------------------------------ */
/* UI Drawing: Sector View, Star Chart, Close-Up Circuit Chart        */
/* ------------------------------------------------------------------ */

static void build_sector_items(void)
{
    uint8_t s = g_voy.sector;
    uint8_t t = sec_type(s);
    mv_nitems = 0;

    if (t == GT_PORT) {
        mv_items[mv_nitems] = 0x81;
        mv_item_dir[mv_nitems] = 8;
        mv_nitems++;
    } else if (t == GT_PLANET) {
        mv_items[mv_nitems] = 0x80;
        mv_item_dir[mv_nitems] = 8;
        mv_nitems++;
    }

    for (uint8_t d = 0; d < 8; d++) {
        int8_t nb = neighbor_sector(s, d);
        if (nb >= 0 && mv_nitems < 11) {
            mv_items[mv_nitems] = (uint8_t)nb;
            mv_item_dir[mv_nitems] = d;
            mv_nitems++;
        }
    }
    if (mv_sel >= mv_nitems) mv_sel = 0;
}

static void fmt_port_class(char *out, uint8_t cls)
{
    out[0] = (cls & 1u) ? 'S' : 'B';
    out[1] = (cls & 2u) ? 'S' : 'B';
    out[2] = (cls & 4u) ? 'S' : 'B';
    out[3] = '\0';
}

static void draw_sector_item(uint8_t i)
{
    uint8_t s = g_voy.sector;
    uint8_t row = (uint8_t)(10 + i);
    bool sel = (i == mv_sel);
    uint8_t bg = sel ? C_FRAME : C_PANEL;
    txt_fill(1, row, 38, bg);
    if (sel) txt_str(2, row, ">", C_GOLD, bg);

    uint8_t it = mv_items[i];
    if (it == 0x81) {
        txt_str(4, row, "DOCK AT STARPORT", C_CYAN, bg);
    } else if (it == 0x80) {
        bool can = (galaxy_planet_pop(s) > 0) || (s == g_voy.relic_sec && g_voy.relic_state == 0);
        txt_str(4, row, can ? "LAND ON PLANET" : "PLANET BARREN (WAIT TO REPOP)",
                can ? C_GREEN : C_DIM, bg);
    } else {
        uint8_t d = mv_item_dir[i];
        bool oneway = !(g_gx.warp[it] & (uint8_t)(1u << ((d + 4u) & 7u)));
        txt_str(4, row, "WARP ", C_GREY, bg);
        txt_str(9, row, dir_lbl[d], C_GOLD, bg);
        txt_str(12, row, oneway ? "-> SEC " : "-- SEC ", oneway ? C_RED : C_DIM, bg);
        txt_num(19, row, (uint32_t)(it + 1), 2, C_WHITE, bg);
        if (g_voy.visited[it] & 1u) {
            uint8_t nt = sec_type(it);
            if (nt == GT_PORT) {
                char cls[4];
                fmt_port_class(cls, sec_sub(it));
                txt_str(23, row, "PORT ", C_CYAN, bg);
                txt_str(28, row, cls, C_GOLD, bg);
            } else if (nt == GT_PLANET) {
                txt_str(23, row, WORLD_NAMES[sec_sub(it)], C_GREEN, bg);
                txt_str(29, row, "L", C_GREY, bg);
                txt_num(30, row, galaxy_planet_level(it), 1, C_CYAN, bg);
            } else if (nt == GT_NEBULA) {
                txt_str(23, row, "NEBULA", C_MAGENTA, bg);
            } else {
                txt_str(23, row, "EMPTY", C_DIM, bg);
            }
        } else {
            txt_str(23, row, "???", C_DIM, bg);
        }
    }
}

void galaxy_draw_tabs(uint8_t active)
{
    static const char *const tabs[4] = { "SECTOR", "CHART", "CLOSEUP", "ACHIEVE" };
    static const uint8_t tx[4] = { 6, 13, 19, 27 };
    txt_box(0, 0, 40, 3, C_CYAN, C_PANEL);
    txt_str(1, 1, "[L1]", C_CYAN, C_PANEL);
    txt_str(35, 1, "[R1]", C_CYAN, C_PANEL);
    for (uint8_t i = 0; i < 4; i++) {
        bool act = (i == active);
        txt_str(tx[i], 1, tabs[i], act ? C_GOLD : C_GREY, act ? C_FRAME : C_PANEL);
    }
}

static void draw_sector_footer(void)
{
    txt_fill(0, 29, 40, C_TRANS);
    if (mv_confirm_exit) {
        txt_center(29, "EXIT TO TITLE?  [A] YES   [B] NO", C_GOLD, C_FRAME);
    } else {
        txt_center(29, "A:SELECT   L1/R1:PAGES   B/START:TITLE", C_DIM, C_TRANS);
    }
}

static void draw_sector_view(void)
{
    uint8_t s = g_voy.sector;
    uint8_t t = sec_type(s);
    uint8_t tier = sec_tier(s);

    txt_clear(C_TRANS);
    galaxy_draw_tabs(0);
    txt_box(0, 3, 40, 4, C_FRAME, C_PANEL);
    txt_str(2, 4, "SECTOR ", C_GREY, C_PANEL);
    txt_num(9, 4, (uint32_t)(s + 1), 2, C_WHITE, C_PANEL);
    txt_str(13, 4, tier_lbl[tier], tier_col[tier], C_PANEL);
    txt_str(26, 4, "DAY ", C_GREY, C_PANEL);
    txt_num(30, 4, g_voy.stardate, 4, C_GOLD, C_PANEL);

    if (t == GT_PORT) {
        char cls[4];
        fmt_port_class(cls, sec_sub(s));
        txt_str(2, 5, "STARPORT CLASS: ", C_CYAN, C_PANEL);
        txt_str(18, 5, cls, C_GOLD, C_PANEL);
    } else if (t == GT_PLANET) {
        uint8_t pop = galaxy_planet_pop(s);
        txt_str(2, 5, "PLANET: ", C_GREEN, C_PANEL);
        txt_str(10, 5, WORLD_NAMES[sec_sub(s)], C_WHITE, C_PANEL);
        txt_str(16, 5, "L", C_GREY, C_PANEL);
        txt_num(17, 5, galaxy_planet_level(s), 1, C_CYAN, C_PANEL);
        if (pop > 0) {
            txt_str(20, 5, "LIFE ", C_GREEN, C_PANEL);
            txt_num(25, 5, pop, 1, C_WHITE, C_PANEL);
        } else {
            txt_str(20, 5, "BARREN", C_RED, C_PANEL);
        }
        if (s == g_voy.relic_sec && g_voy.relic_state == 0)
            txt_str(28, 5, "RELIC!", C_MAGENTA, C_PANEL);
    } else if (t == GT_NEBULA) {
        txt_str(2, 5, "NEBULA - HIGH PIRATE ACTIVITY", C_MAGENTA, C_PANEL);
    } else {
        txt_str(2, 5, "DEEP SPACE - EMPTY SECTOR", C_DIM, C_PANEL);
    }

    if (g_pending_ach < ACH_COUNT) {
        txt_str(2, 7, "ACHIEVEMENT UNLOCKED: ", C_GOLD, C_PANEL);
        txt_str(24, 7, ACH_NAMES[g_pending_ach], C_GREEN, C_PANEL);
    }

    /* Action & Warp list */
    txt_box(0, 8, 40, 14, C_FRAME, C_PANEL);
    txt_str(2, 8, " NAVIGATION & ACTIONS ", C_CYAN, C_PANEL);

    for (uint8_t i = 0; i < mv_nitems; i++)
        draw_sector_item(i);

    /* Status Footer */
    txt_box(0, 23, 40, 5, C_CYAN, C_PANEL);
    txt_str(2, 24, "CR:$", C_GOLD, C_PANEL);
    txt_num(6, 24, g_voy.credits, 5, C_WHITE, C_PANEL);
    txt_str(13, 24, "EN:", C_GREEN, C_PANEL);
    txt_num(16, 24, g_voy.energy, 3, C_WHITE, C_PANEL);
    txt_put(19, 24, '/', C_DIM, C_PANEL);
    txt_num(20, 24, voy_hull_max(), 3, C_GREY, C_PANEL);
    txt_str(25, 24, "HLD:", C_CYAN, C_PANEL);
    txt_num(29, 24, voy_holds_used(), 2, C_WHITE, C_PANEL);
    txt_put(31, 24, '/', C_DIM, C_PANEL);
    txt_num(32, 24, voy_holds_max(), 2, C_GREY, C_PANEL);

    uint8_t kept = 0;
    for (uint8_t i = 0; i < NUM_SPECIES; i++) kept += g_voy.keep[i];
    txt_str(2, 26, "MANIFEST:", C_GREY, C_PANEL);
    txt_num(12, 26, kept, 2, C_GREEN, C_PANEL);
    txt_str(14, 26, "/12", C_DIM, C_PANEL);
    txt_str(20, 26, "BANK:$", C_GOLD, C_PANEL);
    txt_num(26, 26, g_voy.bank, 6, C_WHITE, C_PANEL);

    draw_sector_footer();
}

static void draw_chart_cursor(uint8_t cx, uint8_t cy, bool show)
{
    uint8_t kx = (uint8_t)(8 + cx * 2);
    uint8_t ky = (uint8_t)(4 + cy * 2);
    if (show) {
        if (kx > 0) txt_put(kx - 1, ky, '[', C_WHITE, C_TRANS);
        if (kx + 1 < TXT_COLS) txt_put(kx + 1, ky, ']', C_WHITE, C_TRANS);
    } else {
        if (kx > 0) txt_put(kx - 1, ky, ' ', C_WHITE, C_TRANS);
        if (kx + 1 < TXT_COLS) txt_put(kx + 1, ky, ' ', C_WHITE, C_TRANS);
    }
}

static void chart_set_diag(uint8_t col, uint8_t row, uint8_t tile)
{
    uint8_t cur = chart_bg_get(col, row);
    if ((cur == HT_CH_DR && tile == HT_CH_DL) || (cur == HT_CH_DL && tile == HT_CH_DR))
        chart_bg_tile(col, row, HT_CH_X);
    else if (cur == HT_CH_EMPTY_T)
        chart_bg_tile(col, row, tile);
}

static bool is_reachable_neighbor(uint8_t dest)
{
    uint8_t s = g_voy.sector;
    for (uint8_t d = 0; d < 8; d++) {
        if (neighbor_sector(s, d) == (int8_t)dest) return true;
    }
    return false;
}

static void draw_chart_inspect(void)
{
    for (uint8_t r = 22; r <= 25; r++)
        txt_fill(1, r, 38, C_PANEL);
    int8_t hs = sector_at_cell(chart_cx, chart_cy);
    if (hs >= 0 && (g_voy.visited[hs] & 3u)) {
        txt_str(2, 22, "SECTOR ", C_GREY, C_PANEL);
        txt_num(9, 22, (uint32_t)(hs + 1), 2, C_WHITE, C_PANEL);
        txt_str(13, 22, tier_lbl[sec_tier((uint8_t)hs)], tier_col[sec_tier((uint8_t)hs)], C_PANEL);
        if ((uint8_t)hs == g_voy.sector) {
            txt_str(27, 22, "YOU ARE HERE", C_GOLD, C_PANEL);
        } else if (is_reachable_neighbor((uint8_t)hs)) {
            txt_str(30, 22, "[A] WARP", C_GREEN, C_FRAME);
        }
        if (g_voy.visited[hs] & 1u) {
            uint8_t t = sec_type((uint8_t)hs);
            if (t == GT_PORT) {
                char cls[4]; fmt_port_class(cls, sec_sub((uint8_t)hs));
                txt_str(2, 24, "STARPORT CLASS ", C_CYAN, C_PANEL);
                txt_str(17, 24, cls, C_GOLD, C_PANEL);
            } else if (t == GT_PLANET) {
                txt_str(2, 24, WORLD_NAMES[sec_sub((uint8_t)hs)], C_GREEN, C_PANEL);
                txt_str(9, 24, "LEVEL ", C_GREY, C_PANEL);
                txt_num(15, 24, galaxy_planet_level((uint8_t)hs), 1, C_CYAN, C_PANEL);
                txt_str(18, 24, "LIFE ", C_GREEN, C_PANEL);
                txt_num(23, 24, galaxy_planet_pop((uint8_t)hs), 1, C_WHITE, C_PANEL);
            } else if (t == GT_NEBULA) {
                txt_str(2, 24, "NEBULA SECTOR", C_MAGENTA, C_PANEL);
            } else {
                txt_str(2, 24, "EMPTY SPACE", C_DIM, C_PANEL);
            }
        } else {
            txt_str(2, 24, "UNVISITED - WARP TO EXPLORE", C_DIM, C_PANEL);
        }
    } else {
        txt_str(2, 23, "UNCHARTED VOID", C_DIM, C_PANEL);
    }
}

static void draw_star_chart(void)
{
    video_set_mode(VID_MODE_CHART, 0);
    chart_bg_clear();
    txt_clear(C_TRANS);
    galaxy_draw_tabs(1);

    /* Draw ALL 8 hyperlane directions from every visited sector on 4bpp Plane 1 */
    static const uint8_t d_tile[8] = {
        HT_CH_V, HT_CH_DL, HT_CH_H, HT_CH_DR, HT_CH_V, HT_CH_DL, HT_CH_H, HT_CH_DR
    };
    for (uint8_t s = 0; s < GX_SECTORS; s++) {
        if (!(g_voy.visited[s] & 1u)) continue;
        int8_t cx = (int8_t)(g_gx.cell[s] & 0x0Fu);
        int8_t cy = (int8_t)(g_gx.cell[s] >> 4);
        int8_t sx = (int8_t)(8 + cx * 2);
        int8_t sy = (int8_t)(cy * 2);
        uint8_t w = g_gx.warp[s];
        for (uint8_t d = 0; d < 8; d++) {
            if (!(w & (uint8_t)(1u << d))) continue;
            int8_t nx = cx + gdx[d], ny = cy + gdy[d];
            if (nx < 0 || nx >= GX_GRID_W || ny < 0 || ny >= GX_GRID_H) continue;
            uint8_t tx = (uint8_t)(sx + gdx[d]);
            uint8_t ty = (uint8_t)(sy + gdy[d]);
            if (gdx[d] && gdy[d]) chart_set_diag(tx, ty, d_tile[d]);
            else chart_bg_tile(tx, ty, d_tile[d]);
        }
    }

    /* Draw 4bpp graphical sector nodes on top of hyperlanes */
    for (uint8_t s = 0; s < GX_SECTORS; s++) {
        if (!(g_voy.visited[s] & 3u)) continue;
        uint8_t cx = g_gx.cell[s] & 0x0Fu;
        uint8_t cy = g_gx.cell[s] >> 4;
        uint8_t sx = (uint8_t)(8 + cx * 2);
        uint8_t sy = (uint8_t)(cy * 2);

        uint8_t tile;
        if (s == g_voy.sector) {
            tile = HT_CH_ARK;
        } else if (g_voy.visited[s] & 1u) {
            uint8_t t = sec_type(s);
            tile = (t == GT_PORT) ? HT_CH_PORT :
                   (t == GT_PLANET ? HT_CH_PLANET :
                   (t == GT_NEBULA ? HT_CH_NEBULA : HT_CH_EMPTY));
        } else {
            tile = HT_CH_SEEN;
        }
        chart_bg_tile(sx, sy, tile);
    }

    draw_chart_cursor(chart_cx, chart_cy, true);
    txt_box(0, 21, 40, 6, C_CYAN, C_PANEL);
    draw_chart_inspect();

    txt_center(28, "GOLD:ARK  CYAN:PORT  GRN:PLANET  MAG:NEB", C_GREY, C_TRANS);
    txt_center(29, "A:WARP   B:BACK   L1/R1:PAGES", C_DIM, C_TRANS);
}

static void draw_closeup_chart(void)
{
    video_set_mode(VID_MODE_MENU, 0);
    txt_clear(C_TRANS);
    galaxy_draw_tabs(2);

    int8_t ax = (int8_t)(g_gx.cell[g_voy.sector] & 0x0Fu);
    int8_t ay = (int8_t)(g_gx.cell[g_voy.sector] >> 4);
    int8_t ox = ax - 2;
    int8_t oy = ay - 2;
    if (ox < 0) ox = 0;
    if (ox > GX_GRID_W - 5) ox = GX_GRID_W - 5;
    if (oy < 0) oy = 0;
    if (oy > GX_GRID_H - 5) oy = GX_GRID_H - 5;

    /* Pass 1: Draw ALL 8 warp directions from every visited sector in the 5x5 window */
    static const int8_t cz_x1[8] = {  3,  6,  6,  6,  3, -1, -1, -1 };
    static const int8_t cz_y1[8] = { -1, -1,  1,  3,  3,  3,  1, -1 };
    static const int8_t cz_x2[8] = {  3,  7,  7,  7,  3, -2, -2, -2 };
    static const int8_t cz_y2[8] = { -2, -2,  1,  4,  4,  4,  1, -2 };
    static const uint8_t cz_ch[8] = { '|', '/', '-', '\\', '|', '/', '-', '\\' };
    for (uint8_t ry = 0; ry < 5; ry++) {
        for (uint8_t rx = 0; rx < 5; rx++) {
            int8_t s = sector_at_cell((uint8_t)(ox + rx), (uint8_t)(oy + ry));
            if (s < 0 || !(g_voy.visited[s] & 1u)) continue;
            int8_t sx = (int8_t)(1 + rx * 8);
            int8_t sy = (int8_t)(4 + ry * 5);
            uint8_t w = g_gx.warp[s];
            for (uint8_t d = 0; d < 8; d++) {
                if (!(w & (uint8_t)(1u << d))) continue;
                int8_t nrx = (int8_t)rx + gdx[d], nry = (int8_t)ry + gdy[d];
                if (nrx < 0 || nrx >= 5 || nry < 0 || nry >= 5) continue;
                uint8_t col = (d & 1u) ? C_TEAL : C_CYAN;
                txt_put((uint8_t)(sx + cz_x1[d]), (uint8_t)(sy + cz_y1[d]), cz_ch[d], col, C_TRANS);
                txt_put((uint8_t)(sx + cz_x2[d]), (uint8_t)(sy + cz_y2[d]), cz_ch[d], col, C_TRANS);
            }
        }
    }

    /* Pass 2: Draw sector boxes cleanly on top */
    for (uint8_t ry = 0; ry < 5; ry++) {
        for (uint8_t rx = 0; rx < 5; rx++) {
            int8_t s = sector_at_cell((uint8_t)(ox + rx), (uint8_t)(oy + ry));
            if (s < 0 || !(g_voy.visited[s] & 3u)) continue;

            uint8_t sx = (uint8_t)(1 + rx * 8);
            uint8_t sy = (uint8_t)(4 + ry * 5);
            bool is_ark = ((uint8_t)s == g_voy.sector);
            bool vis = (g_voy.visited[s] & 1u) != 0;

            uint8_t bfg = is_ark ? C_GOLD : (vis ? C_CYAN : C_DIM);
            uint8_t bbg = is_ark ? C_FRAME : C_PANEL;
            txt_box(sx, sy, 6, 3, bfg, bbg);
            txt_num(sx + 1, sy + 1, (uint32_t)(s + 1), 2, is_ark ? C_GOLD : C_WHITE, bbg);
            uint8_t ch = '?';
            uint8_t cf = C_DIM;
            if (vis) {
                uint8_t t = sec_type((uint8_t)s);
                if (t == GT_PORT) { ch = 'P'; cf = C_CYAN; }
                else if (t == GT_PLANET) { ch = 'O'; cf = C_GREEN; }
                else if (t == GT_NEBULA) { ch = '#'; cf = C_MAGENTA; }
                else { ch = '.'; cf = C_GREY; }
            }
            txt_put(sx + 4, sy + 1, ch, cf, bbg);
        }
    }

    txt_center(29, "B:BACK TO SECTOR     L1/R1:PAGES", C_DIM, C_TRANS);
}

/* ------------------------------------------------------------------ */
/* Public Map Loop                                                    */
/* ------------------------------------------------------------------ */

static map_action_t perform_warp(uint8_t dest)
{
    map_dest_sector = dest;
    galaxy_step_turn();
    sfx_player(SFX_LAUNCH, SFX_PRI_EVENT);
    if (galaxy_roll_encounter(dest)) {
        return MAP_GO_SPACE;
    }
    g_voy.sector = dest;
    galaxy_visit(dest);
    save_write(g_save_slot);
    chart_cx = g_gx.cell[dest] & 0x0Fu;
    chart_cy = g_gx.cell[dest] >> 4;
    build_sector_items();
    if (mv_mode == MV_CHART) {
        draw_star_chart();
    } else {
        video_set_mode(VID_MODE_MENU, 0);
        draw_sector_view();
    }
    return MAP_STAY;
}

void map_enter_tab(uint8_t tab)
{
    mv_mode = tab;
    mv_confirm_exit = false;
    galaxy_visit(g_voy.sector);
    chart_cx = g_gx.cell[g_voy.sector] & 0x0Fu;
    chart_cy = g_gx.cell[g_voy.sector] >> 4;
    build_sector_items();
    if (tab == MV_CHART) {
        draw_star_chart();
    } else if (tab == MV_CLOSEUP) {
        draw_closeup_chart();
    } else {
        mv_mode = MV_SECTOR;
        video_set_mode(VID_MODE_MENU, 0);
        draw_sector_view();
    }
    music_play("ROM:Map.vgm");
}

void map_enter(void)
{
    map_enter_tab(MV_SECTOR);
}

map_action_t map_update(void)
{
    video_twinkle_stars(g_frame);

    if (mv_mode == MV_SECTOR) {
        if (mv_confirm_exit) {
            if (in_pressed & IN_A) {
                mv_confirm_exit = false;
                save_write(g_save_slot);
                sfx_player(SFX_BLIP, SFX_PRI_LOW);
                return MAP_GO_TITLE;
            } else if (in_pressed & (IN_B | IN_START)) {
                mv_confirm_exit = false;
                sfx_player(SFX_BLIP, SFX_PRI_LOW);
                draw_sector_footer();
            }
            return MAP_STAY;
        }
        if (in_pressed & IN_UP) {
            uint8_t old = mv_sel;
            mv_sel = (mv_sel == 0) ? (uint8_t)(mv_nitems - 1) : (uint8_t)(mv_sel - 1);
            sfx_player(SFX_BLIP, SFX_PRI_LOW);
            draw_sector_item(old);
            draw_sector_item(mv_sel);
        } else if (in_pressed & IN_DOWN) {
            uint8_t old = mv_sel;
            mv_sel = (uint8_t)((mv_sel + 1) % mv_nitems);
            sfx_player(SFX_BLIP, SFX_PRI_LOW);
            draw_sector_item(old);
            draw_sector_item(mv_sel);
        } else if (in_pressed & IN_R1) {
            mv_mode = MV_CHART;
            g_pending_ach = 0xFFu;
            sfx_player(SFX_BLIP, SFX_PRI_LOW);
            draw_star_chart();
        } else if (in_pressed & IN_L1) {
            g_pending_ach = 0xFFu;
            sfx_player(SFX_BLIP, SFX_PRI_LOW);
            return MAP_GO_ACHIEVE;
        } else if (in_pressed & (IN_B | IN_START)) {
            mv_confirm_exit = true;
            sfx_player(SFX_BLIP, SFX_PRI_LOW);
            draw_sector_footer();
        } else if (in_pressed & IN_A) {
            g_pending_ach = 0xFFu;
            uint8_t it = mv_items[mv_sel];
            if (it == 0x81) {
                sfx_player(SFX_DOCK, SFX_PRI_EVENT);
                return MAP_GO_PORT;
            } else if (it == 0x80) {
                uint8_t s = g_voy.sector;
                if (galaxy_planet_pop(s) > 0 || (s == g_voy.relic_sec && g_voy.relic_state == 0)) {
                    sfx_player(SFX_LAUNCH, SFX_PRI_EVENT);
                    return MAP_GO_PLANET;
                } else {
                    sfx_player(SFX_DENY, SFX_PRI_LOW);
                }
            } else {
                return perform_warp(it);
            }
        }
    } else if (mv_mode == MV_CHART) {
        uint8_t ox = chart_cx, oy = chart_cy;
        if ((in_pressed & IN_LEFT) && chart_cx > 0) chart_cx--;
        if ((in_pressed & IN_RIGHT) && chart_cx < GX_GRID_W - 1) chart_cx++;
        if ((in_pressed & IN_UP) && chart_cy > 0) chart_cy--;
        if ((in_pressed & IN_DOWN) && chart_cy < GX_GRID_H - 1) chart_cy++;
        if (chart_cx != ox || chart_cy != oy) {
            sfx_player(SFX_BLIP, SFX_PRI_LOW);
            draw_chart_cursor(ox, oy, false);
            draw_chart_cursor(chart_cx, chart_cy, true);
            draw_chart_inspect();
        } else if (in_pressed & IN_A) {
            int8_t hs = sector_at_cell(chart_cx, chart_cy);
            if (hs >= 0 && (uint8_t)hs == g_voy.sector) {
                mv_mode = MV_SECTOR;
                sfx_player(SFX_BLIP, SFX_PRI_LOW);
                video_set_mode(VID_MODE_MENU, 0);
                draw_sector_view();
            } else if (hs >= 0 && is_reachable_neighbor((uint8_t)hs)) {
                return perform_warp((uint8_t)hs);
            } else {
                sfx_player(SFX_DENY, SFX_PRI_LOW);
            }
        } else if (in_pressed & IN_R1) {
            mv_mode = MV_CLOSEUP;
            sfx_player(SFX_BLIP, SFX_PRI_LOW);
            draw_closeup_chart();
        } else if (in_pressed & (IN_L1 | IN_B)) {
            mv_mode = MV_SECTOR;
            sfx_player(SFX_BLIP, SFX_PRI_LOW);
            video_set_mode(VID_MODE_MENU, 0);
            draw_sector_view();
        }
    } else if (mv_mode == MV_CLOSEUP) {
        if (in_pressed & IN_R1) {
            sfx_player(SFX_BLIP, SFX_PRI_LOW);
            return MAP_GO_ACHIEVE;
        } else if (in_pressed & IN_L1) {
            mv_mode = MV_CHART;
            sfx_player(SFX_BLIP, SFX_PRI_LOW);
            draw_star_chart();
        } else if (in_pressed & IN_B) {
            mv_mode = MV_SECTOR;
            sfx_player(SFX_BLIP, SFX_PRI_LOW);
            video_set_mode(VID_MODE_MENU, 0);
            draw_sector_view();
        }
    }

    return MAP_STAY;
}
