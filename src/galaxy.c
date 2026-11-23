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

    /* Grid-indexed scratch */
    uint8_t gc_warp[128];
    int8_t  gc_sec[128];
    uint8_t taken[GX_SECTORS];
    uint8_t ext_cell[32], ext_dir[32];
    uint8_t n_taken = 0, n_ext = 0;

    memset(gc_warp, 0, sizeof(gc_warp));
    memset(gc_sec, -1, sizeof(gc_sec));

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
            /* One-way warp */
            ext_cell[n_ext] = c;
            ext_dir[n_ext] = d;
            n_ext++;
        } else {
            gc_warp[nc] |= (uint8_t)(1u << ((d + 4u) & 7u));
        }
    }
    (void)ext_cell;
    (void)ext_dir;

    /* BFS from home to number sectors 0..63 by distance */
    uint8_t q[GX_SECTORS];
    int8_t  cell_to_s[128];
    memset(cell_to_s, -1, sizeof(cell_to_s));
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

    /* Compute danger = jumps back to Safe Space (0..9) via reverse BFS */
    uint8_t dist[GX_SECTORS];
    memset(dist, 0xFF, sizeof(dist));
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

static void draw_sector_view(void)
{
    uint8_t s = g_voy.sector;
    uint8_t t = sec_type(s);
    uint8_t tier = sec_tier(s);

    txt_clear(C_TRANS);
    txt_box(0, 0, 40, 5, C_CYAN, C_PANEL);
    txt_str(2, 1, "SECTOR ", C_GREY, C_PANEL);
    txt_num(9, 1, (uint32_t)(s + 1), 2, C_WHITE, C_PANEL);
    txt_str(13, 1, tier_lbl[tier], tier_col[tier], C_PANEL);
    txt_str(26, 1, "DAY ", C_GREY, C_PANEL);
    txt_num(30, 1, g_voy.stardate, 4, C_GOLD, C_PANEL);

    if (t == GT_PORT) {
        char cls[4];
        fmt_port_class(cls, sec_sub(s));
        txt_str(2, 3, "STARPORT CLASS: ", C_CYAN, C_PANEL);
        txt_str(18, 3, cls, C_GOLD, C_PANEL);
    } else if (t == GT_PLANET) {
        uint8_t pop = galaxy_planet_pop(s);
        txt_str(2, 3, "PLANET: ", C_GREEN, C_PANEL);
        txt_str(10, 3, WORLD_NAMES[sec_sub(s)], C_WHITE, C_PANEL);
        txt_str(16, 3, "L", C_GREY, C_PANEL);
        txt_num(17, 3, galaxy_planet_level(s), 1, C_CYAN, C_PANEL);
        if (pop > 0) {
            txt_str(20, 3, "LIFE ", C_GREEN, C_PANEL);
            txt_num(25, 3, pop, 1, C_WHITE, C_PANEL);
        } else {
            txt_str(20, 3, "BARREN", C_RED, C_PANEL);
        }
        if (s == g_voy.relic_sec && g_voy.relic_state == 0)
            txt_str(28, 3, "RELIC!", C_MAGENTA, C_PANEL);
    } else if (t == GT_NEBULA) {
        txt_str(2, 3, "NEBULA - HIGH PIRATE ACTIVITY", C_MAGENTA, C_PANEL);
    } else {
        txt_str(2, 3, "DEEP SPACE - EMPTY SECTOR", C_DIM, C_PANEL);
    }

    if (g_pending_ach < ACH_COUNT) {
        txt_str(2, 6, "ACHIEVEMENT UNLOCKED: ", C_GOLD, C_PANEL);
        txt_str(24, 6, ACH_NAMES[g_pending_ach], C_GREEN, C_PANEL);
    }

    /* Action & Warp list */
    txt_box(0, 8, 40, 14, C_FRAME, C_PANEL);
    txt_str(2, 8, " NAVIGATION & ACTIONS ", C_CYAN, C_PANEL);

    for (uint8_t i = 0; i < mv_nitems; i++) {
        uint8_t row = (uint8_t)(10 + i);
        bool sel = (i == mv_sel);
        uint8_t bg = sel ? C_FRAME : C_PANEL;
        if (sel) {
            for (uint8_t c = 1; c < 39; c++) txt_put(c, row, ' ', C_WHITE, bg);
            txt_str(2, row, ">", C_GOLD, bg);
        }
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

    txt_center(29, "[Z] SELECT   [X] STAR CHART   [ESC] MENU", C_DIM, C_TRANS);
}

static void draw_star_chart(void)
{
    txt_clear(C_TRANS);
    txt_box(0, 0, 40, 3, C_CYAN, C_PANEL);
    txt_center(1, "GALACTIC STAR CHART (12x8)", C_GOLD, C_PANEL);

    /* Draw 12x8 grid: each cell is 3 cols x 2 rows starting at (2, 4) */
    for (uint8_t s = 0; s < GX_SECTORS; s++) {
        if (!(g_voy.visited[s] & 3u)) continue;
        uint8_t cx = g_gx.cell[s] & 0x0Fu;
        uint8_t cy = g_gx.cell[s] >> 4;
        uint8_t sx = (uint8_t)(2 + cx * 3);
        uint8_t sy = (uint8_t)(4 + cy * 2);

        /* Draw warp connectors for visited sectors */
        if (g_voy.visited[s] & 1u) {
            if (g_gx.warp[s] & (1u << 2)) txt_put(sx + 1, sy, '-', C_DIM, C_TRANS);
            if (g_gx.warp[s] & (1u << 2)) txt_put(sx + 2, sy, '-', C_DIM, C_TRANS);
            if (g_gx.warp[s] & (1u << 4)) txt_put(sx, sy + 1, '|', C_DIM, C_TRANS);
            if (g_gx.warp[s] & (1u << 3)) txt_put(sx + 1, sy + 1, '\\', C_DIM, C_TRANS);
            if (g_gx.warp[s] & (1u << 5)) txt_put(sx - 1, sy + 1, '/', C_DIM, C_TRANS);
        }
    }

    for (uint8_t s = 0; s < GX_SECTORS; s++) {
        if (!(g_voy.visited[s] & 3u)) continue;
        uint8_t cx = g_gx.cell[s] & 0x0Fu;
        uint8_t cy = g_gx.cell[s] >> 4;
        uint8_t sx = (uint8_t)(2 + cx * 3);
        uint8_t sy = (uint8_t)(4 + cy * 2);
        bool is_cur = (cx == chart_cx && cy == chart_cy);
        uint8_t bg = is_cur ? C_FRAME : C_TRANS;

        if (s == g_voy.sector) {
            txt_put(sx, sy, '@', C_GOLD, C_PANEL);
        } else if (g_voy.visited[s] & 1u) {
            uint8_t t = sec_type(s);
            uint8_t ch = (t == GT_PORT) ? 'P' : (t == GT_PLANET ? 'O' : (t == GT_NEBULA ? '#' : '+'));
            uint8_t fg = (t == GT_PORT) ? C_CYAN : (t == GT_PLANET ? C_GREEN : (t == GT_NEBULA ? C_MAGENTA : C_GREY));
            txt_put(sx, sy, ch, fg, bg);
        } else {
            txt_put(sx, sy, '?', C_DIM, bg);
        }
    }

    /* Highlight cursor brackets */
    uint8_t kx = (uint8_t)(2 + chart_cx * 3);
    uint8_t ky = (uint8_t)(4 + chart_cy * 2);
    if (kx > 0) txt_put(kx - 1, ky, '[', C_WHITE, C_TRANS);
    if (kx + 1 < TXT_COLS) txt_put(kx + 1, ky, ']', C_WHITE, C_TRANS);

    /* Inspect sector under cursor */
    txt_box(0, 21, 40, 6, C_CYAN, C_PANEL);
    int8_t hs = sector_at_cell(chart_cx, chart_cy);
    if (hs >= 0 && (g_voy.visited[hs] & 3u)) {
        txt_str(2, 22, "SECTOR ", C_GREY, C_PANEL);
        txt_num(9, 22, (uint32_t)(hs + 1), 2, C_WHITE, C_PANEL);
        txt_str(13, 22, tier_lbl[sec_tier((uint8_t)hs)], tier_col[sec_tier((uint8_t)hs)], C_PANEL);
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

    txt_str(2, 28, "@:ARK P:PORT O:PLANET #:NEBULA ?:SEEN", C_GREY, C_TRANS);
    txt_center(29, "ARROWS: INSPECT  [X] CLOSE-UP  [Z] BACK", C_DIM, C_TRANS);
}

static void draw_closeup_chart(void)
{
    txt_clear(C_TRANS);
    txt_box(0, 0, 40, 3, C_CYAN, C_PANEL);
    txt_center(1, "CLOSE-UP SECTOR CIRCUIT", C_GOLD, C_PANEL);

    int8_t ax = (int8_t)(g_gx.cell[g_voy.sector] & 0x0Fu);
    int8_t ay = (int8_t)(g_gx.cell[g_voy.sector] >> 4);
    int8_t ox = ax - 2;
    int8_t oy = ay - 2;
    if (ox < 0) ox = 0;
    if (ox > GX_GRID_W - 5) ox = GX_GRID_W - 5;
    if (oy < 0) oy = 0;
    if (oy > GX_GRID_H - 5) oy = GX_GRID_H - 5;

    for (uint8_t ry = 0; ry < 5; ry++) {
        for (uint8_t rx = 0; rx < 5; rx++) {
            uint8_t gx = (uint8_t)(ox + rx);
            uint8_t gy = (uint8_t)(oy + ry);
            int8_t s = sector_at_cell(gx, gy);
            if (s < 0 || !(g_voy.visited[s] & 3u)) continue;

            uint8_t sx = (uint8_t)(1 + rx * 8);
            uint8_t sy = (uint8_t)(4 + ry * 5);
            bool is_ark = ((uint8_t)s == g_voy.sector);
            bool vis = (g_voy.visited[s] & 1u) != 0;

            /* Warp lines to E, S, SE, SW */
            if (vis) {
                if (rx < 4 && (g_gx.warp[s] & (1u << 2))) {
                    txt_str(sx + 6, sy + 1, "--", C_CYAN, C_TRANS);
                }
                if (ry < 4 && (g_gx.warp[s] & (1u << 4))) {
                    txt_put(sx + 3, sy + 3, '|', C_CYAN, C_TRANS);
                    txt_put(sx + 3, sy + 4, '|', C_CYAN, C_TRANS);
                }
                if (rx < 4 && ry < 4 && (g_gx.warp[s] & (1u << 3))) {
                    txt_put(sx + 6, sy + 3, '\\', C_DIM, C_TRANS);
                    txt_put(sx + 7, sy + 4, '\\', C_DIM, C_TRANS);
                }
                if (rx > 0 && ry < 4 && (g_gx.warp[s] & (1u << 5))) {
                    txt_put(sx - 1, sy + 3, '/', C_DIM, C_TRANS);
                    txt_put(sx - 2, sy + 4, '/', C_DIM, C_TRANS);
                }
            }

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

    txt_center(29, "[X] ACHIEVEMENTS    [Z] BACK TO SECTOR", C_DIM, C_TRANS);
}

/* ------------------------------------------------------------------ */
/* Public Map Loop                                                    */
/* ------------------------------------------------------------------ */

void map_enter(void)
{
    mv_mode = MV_SECTOR;
    galaxy_visit(g_voy.sector);
    chart_cx = g_gx.cell[g_voy.sector] & 0x0Fu;
    chart_cy = g_gx.cell[g_voy.sector] >> 4;
    build_sector_items();
    video_set_mode(VID_MODE_MENU, 0);
    draw_sector_view();
    music_play("ROM:Map.vgm");
}

map_action_t map_update(void)
{
    video_twinkle_stars(g_frame);

    if (mv_mode == MV_SECTOR) {
        if (in_pressed & IN_UP) {
            mv_sel = (mv_sel == 0) ? (uint8_t)(mv_nitems - 1) : (uint8_t)(mv_sel - 1);
            sfx_player(SFX_BLIP, SFX_PRI_LOW);
            draw_sector_view();
        } else if (in_pressed & IN_DOWN) {
            mv_sel = (uint8_t)((mv_sel + 1) % mv_nitems);
            sfx_player(SFX_BLIP, SFX_PRI_LOW);
            draw_sector_view();
        } else if (in_pressed & IN_BTN2) {
            mv_mode = MV_CHART;
            g_pending_ach = 0xFFu;
            sfx_player(SFX_BLIP, SFX_PRI_LOW);
            draw_star_chart();
        } else if (in_pressed & IN_START) {
            save_write(g_save_slot);
            sfx_player(SFX_BLIP, SFX_PRI_LOW);
            return MAP_GO_TITLE;
        } else if (in_pressed & IN_FIRE) {
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
                /* Warp to sector it */
                map_dest_sector = it;
                galaxy_step_turn();
                sfx_player(SFX_LAUNCH, SFX_PRI_EVENT);
                if (galaxy_roll_encounter(it)) {
                    return MAP_GO_SPACE;
                }
                g_voy.sector = it;
                galaxy_visit(it);
                chart_cx = g_gx.cell[it] & 0x0Fu;
                chart_cy = g_gx.cell[it] >> 4;
                build_sector_items();
                draw_sector_view();
            }
        }
    } else if (mv_mode == MV_CHART) {
        bool moved = false;
        if ((in_pressed & IN_LEFT) && chart_cx > 0) { chart_cx--; moved = true; }
        if ((in_pressed & IN_RIGHT) && chart_cx < GX_GRID_W - 1) { chart_cx++; moved = true; }
        if ((in_pressed & IN_UP) && chart_cy > 0) { chart_cy--; moved = true; }
        if ((in_pressed & IN_DOWN) && chart_cy < GX_GRID_H - 1) { chart_cy++; moved = true; }
        if (moved) {
            sfx_player(SFX_BLIP, SFX_PRI_LOW);
            draw_star_chart();
        } else if (in_pressed & IN_BTN2) {
            mv_mode = MV_CLOSEUP;
            sfx_player(SFX_BLIP, SFX_PRI_LOW);
            draw_closeup_chart();
        } else if (in_pressed & (IN_FIRE | IN_START)) {
            mv_mode = MV_SECTOR;
            sfx_player(SFX_BLIP, SFX_PRI_LOW);
            draw_sector_view();
        }
    } else if (mv_mode == MV_CLOSEUP) {
        if (in_pressed & IN_BTN2) {
            sfx_player(SFX_BLIP, SFX_PRI_LOW);
            return MAP_GO_ACHIEVE;
        } else if (in_pressed & (IN_FIRE | IN_START)) {
            mv_mode = MV_SECTOR;
            sfx_player(SFX_BLIP, SFX_PRI_LOW);
            draw_sector_view();
        }
    }

    return MAP_STAY;
}
