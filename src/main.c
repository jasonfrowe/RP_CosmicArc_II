#include <rp6502.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "achieve.h"
#include "galaxy.h"
#include "game.h"
#include "input.h"
#include "math_fixed.h"
#include "music.h"
#include "planet.h"
#include "port.h"
#include "save.h"
#include "sfx.h"
#include "space.h"
#include "video.h"
#include "xram.h"

game_mode_t g_mode = GM_TITLE;
voyage_t    g_voy;
galaxy_t    g_gx;
uint8_t     g_frame = 0;
uint8_t     g_arena[256];
uint8_t     g_save_slot = 0;
uint8_t     g_pending_ach = 0xFFu;
volatile uint16_t lag_frames = 0;

static bool quick_run = false;
static bool space_from_warp = false;

const char *const WORLD_NAMES[NUM_WORLDS] = {
    "VERDA", "RUBRA", "GLACI", "UMBRA", "ARENA", "TOXIS"
};

const char *const SPECIES_NAMES[NUM_SPECIES] = {
    "WALKER", "HOPPER", "RUNNER", "DODGER", "GLIDER", "SKITTER"
};

/* ------------------------------------------------------------------ */
/* Voyage Helpers                                                     */
/* ------------------------------------------------------------------ */

uint8_t voy_hull_max(void)
{
    uint16_t m = (uint16_t)EN_BASE + (uint16_t)g_voy.u_hull * EN_STEP;
    return (m > EN_TOP) ? EN_TOP : (uint8_t)m;
}

uint8_t voy_holds_max(void)
{
    static const uint8_t tbl[3] = {10, 15, 20};
    return tbl[g_voy.u_holds > 2 ? 2 : g_voy.u_holds];
}

uint8_t voy_holds_used(void)
{
    uint16_t u = (uint16_t)g_voy.ore + g_voy.org + g_voy.eqp;
    for (uint8_t i = 0; i < NUM_SPECIES; i++)
        u += g_voy.stasis[i];
    return (u > 255u) ? 255u : (uint8_t)u;
}

bool voy_has_hold_room(void)
{
    return voy_holds_used() < voy_holds_max();
}

void ach_set(uint8_t id)
{
    if (id >= ACH_COUNT) return;
    uint16_t bit = (uint16_t)(1u << id);
    if (!(g_voy.ach_bits & bit)) {
        g_voy.ach_bits |= bit;
        g_pending_ach = id;
        sfx_player(SFX_FANFARE, SFX_PRI_TOP);
    }
}

void ach_check_all(void)
{
    uint8_t pairs = 0, ones = 0;
    for (uint8_t i = 0; i < NUM_SPECIES; i++) {
        if (g_voy.keep[i] >= 1) ones++;
        if (g_voy.keep[i] >= 2) pairs++;
    }
    if (pairs == NUM_SPECIES) ach_set(ACH_PAIRS);
    if (ones == NUM_SPECIES && g_voy.stardate <= 150) ach_set(ACH_SURVEY);
    if (g_voy.kills >= 50) ach_set(ACH_ACE);
    uint8_t vis = 0;
    for (uint8_t i = 0; i < GX_SECTORS; i++)
        if (g_voy.visited[i] & 1u) vis++;
    if (vis == GX_SECTORS) ach_set(ACH_CHARTED);
    if (g_voy.credits >= 10000u) ach_set(ACH_TYCOON);
    if (g_voy.u_hull >= EN_UPS_MAX && g_voy.u_holds >= 2) ach_set(ACH_REFIT);
    if (g_voy.relic_state == 2) ach_set(ACH_RELIC);
    if (g_voy.storms >= 5) ach_set(ACH_STORMS);
    if (g_voy.sector == 63) ach_set(ACH_FARSHORE);
    if (g_voy.sold_goods >= 50) ach_set(ACH_MERCHANT);
    if (g_voy.bank >= 5000u) ach_set(ACH_BANKER);
    if (voy_holds_used() >= 20) ach_set(ACH_LOADED);
}

void voy_new(uint32_t seed)
{
    memset(&g_voy, 0, sizeof(g_voy));
    g_voy.magic = 0x53324143u;
    g_voy.seed = seed ? seed : 0xC051C202u;
    g_voy.sector = 0;
    g_voy.stardate = 1;
    g_voy.credits = 200;
    g_voy.energy = EN_BASE;
    g_pending_ach = 0xFFu;
}

/* ------------------------------------------------------------------ */
/* Title Screen & 3-Slot Save Picker                                  */
/* ------------------------------------------------------------------ */

static uint8_t title_sel = 0;
static uint16_t title_ticks = 0;
static uint8_t slot_mode = 0; /* 0=main menu, 1=pick slot new, 2=pick slot continue */
static uint8_t slot_sel = 0;
static bool slot_confirm_erase = false;
static save_info_t slot_info[SAVE_SLOTS];

static const char *const title_items[] = {
    "NEW VOYAGE",
    "CONTINUE VOYAGE",
    "QUICK DEEP SPACE",
    "QUICK PLANET RUN"
};
#define TITLE_NITEMS 4

static void probe_all_slots(void)
{
    for (uint8_t i = 0; i < SAVE_SLOTS; i++)
        save_probe(i, &slot_info[i]);
}

static void title_draw_item(uint8_t i)
{
    uint8_t row = (uint8_t)(16 + i * 2);
    bool sel = (i == title_sel);
    if (sel) {
        txt_str(10, row, "> ", C_GOLD, C_PANEL);
        txt_str(12, row, title_items[i], C_WHITE, C_PANEL);
        txt_str(28, row, " <", C_GOLD, C_PANEL);
    } else {
        txt_str(10, row, "  ", C_GREY, C_TRANS);
        txt_str(12, row, title_items[i], C_GREY, C_TRANS);
        txt_str(28, row, "  ", C_GREY, C_TRANS);
    }
}

static void slot_draw_item(uint8_t i)
{
    uint8_t row = (uint8_t)(18 + i * 2);
    bool sel = (i == slot_sel);
    uint8_t bg = sel ? C_FRAME : C_PANEL;
    txt_fill(2, row, 36, bg);
    txt_str(3, row, sel ? ">" : " ", C_GOLD, bg);
    txt_str(4, row, "SLOT ", C_CYAN, bg);
    txt_num(9, row, (uint32_t)(i + 1), 1, C_WHITE, bg);
    if (slot_info[i].valid) {
        txt_str(12, row, "S", C_GREY, bg);
        txt_num(13, row, (uint32_t)(slot_info[i].sector + 1), 2, C_WHITE, bg);
        txt_str(16, row, "D", C_GREY, bg);
        txt_num(17, row, slot_info[i].stardate, 3, C_CYAN, bg);
        txt_str(21, row, "$", C_GOLD, bg);
        txt_num(22, row, slot_info[i].credits, 5, C_WHITE, bg);
        txt_str(29, row, "ACH", C_GREEN, bg);
        txt_num(33, row, slot_info[i].ach_count, 2, C_GOLD, bg);
    } else {
        txt_str(14, row, "--- EMPTY SLOT ---", C_DIM, bg);
    }
}

static void slot_draw_footer(void)
{
    txt_fill(0, 26, 40, C_TRANS);
    if (slot_confirm_erase) {
        txt_center(26, "ERASE THIS SLOT?  [A] YES   [B] NO", C_RED, C_FRAME);
    } else {
        txt_center(26, "A:SELECT   B:BACK   X:ERASE SLOT", C_DIM, C_TRANS);
    }
}

static void title_draw_menu(void)
{
    txt_clear(C_TRANS);
    txt_center(3,  "==================================", C_CYAN, C_TRANS);
    txt_center(4,  "     C O S M I C   A R C   I I    ", C_GOLD, C_PANEL);
    txt_center(5,  "   ALPHA RO CLUSTER  -  RP6502    ", C_CYAN, C_TRANS);
    txt_center(6,  "==================================", C_CYAN, C_TRANS);

    if (slot_mode == 0) {
        for (uint8_t i = 0; i < TITLE_NITEMS; i++)
            title_draw_item(i);
        txt_center(25, "D-PAD/WASD: MOVE    A/Z: FIRE/SELECT", C_DIM, C_TRANS);
        txt_center(26, "B/X/ESC: FIGHTER/BACK   L1/R1(Q/E): TABS", C_DIM, C_TRANS);
        txt_center(28, "PICOCOMPUTER 6502  -  OPL2 FM AUDIO", C_TEAL, C_TRANS);
    } else {
        txt_center(15, slot_mode == 1 ? "SELECT SLOT FOR NEW VOYAGE" : "SELECT VOYAGE TO CONTINUE",
                   C_GOLD, C_PANEL);
        for (uint8_t i = 0; i < SAVE_SLOTS; i++)
            slot_draw_item(i);
        slot_draw_footer();
    }
}

static void enter_title(void)
{
    g_mode = GM_TITLE;
    slot_mode = 0;
    slot_confirm_erase = false;
    title_ticks = 0;
    quick_run = false;
    video_set_mode(VID_MODE_MENU, 0);
    title_draw_menu();
    music_play("ROM:Title.vgm");
}

static void title_update(void)
{
    title_ticks++;

    int16_t ay = 76 + (sin256((uint8_t)(title_ticks << 1)) >> 6);
    uint16_t ark_ptr = (uint16_t)(XRAM_IMG_ARK + (((title_ticks >> 4) & 1u) * ARK_FRAME_BYTES));
    spr_place(0, ark_ptr, PAL_ARK, MODE5_SIZE(ARK_W, ARK_H), MODE5_4BPP, (SCREEN_W - ARK_W) / 2, ay);

    uint8_t ang = (uint8_t)(title_ticks * 2u);
    int16_t sx = (SCREEN_W / 2 - SAUCER_W / 2) + ((cos256(ang) * 72) >> 8);
    int16_t sy = (ay + 12) + ((sin256(ang) * 20) >> 8);
    uint16_t sau_ptr = (uint16_t)(XRAM_IMG_SAUCER + (((title_ticks >> 2) & 3u) * SAUCER_FRAME_BYTES));
    spr_place(1, sau_ptr, PAL_SAUCER, MODE5_SIZE(SAUCER_W, SAUCER_H), MODE5_4BPP, sx, sy);

    if (slot_mode == 0) {
        if (in_pressed & IN_UP) {
            uint8_t old = title_sel;
            title_sel = (title_sel == 0) ? (TITLE_NITEMS - 1) : (uint8_t)(title_sel - 1);
            sfx_player(SFX_BLIP, SFX_PRI_LOW);
            title_draw_item(old);
            title_draw_item(title_sel);
        } else if (in_pressed & IN_DOWN) {
            uint8_t old = title_sel;
            title_sel = (uint8_t)((title_sel + 1) % TITLE_NITEMS);
            sfx_player(SFX_BLIP, SFX_PRI_LOW);
            title_draw_item(old);
            title_draw_item(title_sel);
        } else if (in_pressed & (IN_A | IN_START)) {
            sfx_player(SFX_DOCK, SFX_PRI_EVENT);
            if (title_sel == 0) {
                probe_all_slots();
                slot_mode = 1;
                slot_sel = 0;
                slot_confirm_erase = false;
                title_draw_menu();
            } else if (title_sel == 1) {
                probe_all_slots();
                slot_mode = 2;
                slot_sel = 0;
                slot_confirm_erase = false;
                title_draw_menu();
            } else if (title_sel == 2) {
                quick_run = true;
                voy_new(0xCA200001u ^ ((uint32_t)title_ticks << 8));
                g_mode = GM_SPACE;
                space_enter(1, true);
            } else if (title_sel == 3) {
                quick_run = true;
                voy_new(0xCA200002u ^ ((uint32_t)title_ticks << 8));
                g_mode = GM_PLANET;
                planet_enter(0, 1, 4, true, 0);
            }
        }
    } else {
        if (slot_confirm_erase) {
            if (in_pressed & IN_A) {
                save_erase(slot_sel);
                probe_all_slots();
                slot_confirm_erase = false;
                sfx_player(SFX_DENY, SFX_PRI_LOW);
                slot_draw_item(slot_sel);
                slot_draw_footer();
            } else if (in_pressed & (IN_B | IN_X | IN_SELECT)) {
                slot_confirm_erase = false;
                sfx_player(SFX_BLIP, SFX_PRI_LOW);
                slot_draw_footer();
            }
            return;
        }
        if (in_pressed & IN_UP) {
            uint8_t old = slot_sel;
            slot_sel = (slot_sel == 0) ? (SAVE_SLOTS - 1) : (uint8_t)(slot_sel - 1);
            sfx_player(SFX_BLIP, SFX_PRI_LOW);
            slot_draw_item(old);
            slot_draw_item(slot_sel);
        } else if (in_pressed & IN_DOWN) {
            uint8_t old = slot_sel;
            slot_sel = (uint8_t)((slot_sel + 1) % SAVE_SLOTS);
            sfx_player(SFX_BLIP, SFX_PRI_LOW);
            slot_draw_item(old);
            slot_draw_item(slot_sel);
        } else if (in_pressed & IN_B) {
            slot_mode = 0;
            sfx_player(SFX_BLIP, SFX_PRI_LOW);
            title_draw_menu();
        } else if (in_pressed & (IN_X | IN_SELECT)) {
            if (slot_info[slot_sel].valid) {
                slot_confirm_erase = true;
                sfx_player(SFX_BLIP, SFX_PRI_LOW);
                slot_draw_footer();
            } else {
                sfx_player(SFX_DENY, SFX_PRI_LOW);
            }
        } else if (in_pressed & IN_A) {
            g_save_slot = slot_sel;
            if (slot_mode == 1) {
                uint32_t seed = rng_state() ^ ((uint32_t)title_ticks << 16) ^ ((uint32_t)ria_vsync() << 8) ^ 0xA5A55A5Au;
                voy_new(seed);
                galaxy_generate(g_voy.seed);
                galaxy_init_voyage_stocks();
                save_write(g_save_slot);
                quick_run = false;
                g_mode = GM_MAP;
                map_enter();
            } else {
                if (save_read(g_save_slot)) {
                    galaxy_generate(g_voy.seed);
                    quick_run = false;
                    g_mode = GM_MAP;
                    map_enter();
                } else {
                    sfx_player(SFX_DENY, SFX_PRI_LOW);
                }
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* Main Loop                                                          */
/* ------------------------------------------------------------------ */

int main(void)
{
    video_init();
    input_init();
    music_init();
    sfx_init();
    rng_seed(0xCA206502u ^ (uint32_t)ria_vsync());

    enter_title();

    uint8_t vs = ria_vsync();
    while (1) {
        uint8_t now;
        while ((now = ria_vsync()) == vs)
            ;
        if ((uint8_t)(now - vs) > 1u)
            lag_frames++;
        vs = now;

        video_commit();
        music_update();
        sfx_update();
        input_update();
        g_frame++;
        msg_tick();

        switch (g_mode) {
        case GM_TITLE:
            video_twinkle_stars(g_frame);
            title_update();
            break;

        case GM_MAP: {
            map_action_t act = map_update();
            if (act == MAP_GO_SPACE) {
                space_from_warp = true;
                g_mode = GM_SPACE;
                space_enter(map_enc_wave, false);
            } else if (act == MAP_GO_PLANET) {
                uint8_t s = g_voy.sector;
                uint8_t w = (uint8_t)((g_gx.info[s] >> 2) & 7u);
                uint8_t lv = galaxy_planet_level(s);
                uint8_t pop = galaxy_planet_pop(s);
                bool rel = (s == g_voy.relic_sec && g_voy.relic_state == 0);
                g_mode = GM_PLANET;
                planet_enter(w, lv, pop, rel, s);
            } else if (act == MAP_GO_PORT) {
                g_mode = GM_PORT;
                port_enter();
            } else if (act == MAP_GO_ACHIEVE) {
                g_mode = GM_ACHIEVE;
                achieve_enter();
            } else if (act == MAP_GO_TITLE) {
                enter_title();
            }
            break;
        }

        case GM_PORT:
            if (port_update()) {
                if (galaxy_roll_encounter(g_voy.sector)) {
                    space_from_warp = false;
                    g_mode = GM_SPACE;
                    space_enter(map_enc_wave, false);
                } else {
                    g_mode = GM_MAP;
                    map_enter();
                }
            }
            break;

        case GM_ACHIEVE: {
            ach_action_t a = achieve_update();
            if (a != ACH_STAY) {
                g_mode = GM_MAP;
                map_enter_tab(a == ACH_PREV ? 2 : 0);
            }
            break;
        }

        case GM_SPACE: {
            space_result_t r = space_update();
            if (r == SPACE_ARK_LOST) {
                g_mode = GM_ESCAPEPOD;
                escape_enter();
            } else if (r == SPACE_CLEARED) {
                if (quick_run) {
                    enter_title();
                } else {
                    if (space_from_warp) {
                        g_voy.sector = map_dest_sector;
                        galaxy_visit(g_voy.sector);
                        space_from_warp = false;
                    }
                    ach_check_all();
                    save_write(g_save_slot);
                    g_mode = GM_MAP;
                    map_enter();
                }
            }
            break;
        }

        case GM_PLANET: {
            planet_result_t r = planet_update();
            if (r == PLANET_ARK_LOST) {
                g_mode = GM_ESCAPEPOD;
                escape_enter();
            } else if (r == PLANET_DONE) {
                if (quick_run) {
                    enter_title();
                } else {
                    ach_check_all();
                    save_write(g_save_slot);
                    if (galaxy_roll_encounter(g_voy.sector)) {
                        space_from_warp = false;
                        g_mode = GM_SPACE;
                        space_enter(map_enc_wave, false);
                    } else {
                        g_mode = GM_MAP;
                        map_enter();
                    }
                }
            }
            break;
        }

        case GM_ESCAPEPOD:
            if (escape_update()) {
                if (quick_run) {
                    enter_title();
                } else {
                    save_write(g_save_slot);
                    g_mode = GM_MAP;
                    map_enter();
                }
            }
            break;
        }
    }
}
