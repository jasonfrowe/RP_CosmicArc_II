#include <rp6502.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "game.h"
#include "input.h"
#include "math_fixed.h"
#include "music.h"
#include "sfx.h"
#include "video.h"
#include "xram.h"

game_mode_t g_mode = GM_TITLE;
voyage_t    g_voy;
galaxy_t    g_gx;
uint8_t     g_frame = 0;
uint8_t     g_save_slot = 0;
uint8_t     g_pending_ach = 0xFFu;
volatile uint16_t lag_frames = 0;

const char *const WORLD_NAMES[NUM_WORLDS] = {
    "VERDA", "RUBRA", "GLACI", "UMBRA", "ARENA", "TOXIS"
};

const char *const SPECIES_NAMES[NUM_SPECIES] = {
    "WALKER", "HOPPER", "RUNNER", "DODGER", "GLIDER", "SKITTER"
};

/* ------------------------------------------------------------------ */
/* Voyage helpers                                                     */
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
    g_voy.magic = 0x53324143u; /* 'CA2S' */
    g_voy.seed = seed ? seed : 0xC051C202u;
    g_voy.sector = 0;
    g_voy.stardate = 1;
    g_voy.credits = 200;
    g_voy.energy = EN_BASE;
    g_pending_ach = 0xFFu;
}

/* ------------------------------------------------------------------ */
/* Title Screen                                                       */
/* ------------------------------------------------------------------ */

static uint8_t title_sel = 0;
static uint16_t title_ticks = 0;

static const char *const title_items[] = {
    "NEW VOYAGE",
    "CONTINUE VOYAGE",
    "QUICK DEEP SPACE",
    "QUICK PLANET RUN"
};
#define TITLE_NITEMS 4

static void title_draw_menu(void)
{
    txt_clear(C_TRANS);
    txt_center(3,  "==================================", C_CYAN, C_TRANS);
    txt_center(4,  "     C O S M I C   A R C   I I    ", C_GOLD, C_PANEL);
    txt_center(5,  "   ALPHA RO CLUSTER  -  RP6502    ", C_CYAN, C_TRANS);
    txt_center(6,  "==================================", C_CYAN, C_TRANS);

    for (uint8_t i = 0; i < TITLE_NITEMS; i++) {
        uint8_t row = (uint8_t)(16 + i * 2);
        bool sel = (i == title_sel);
        if (sel) {
            txt_str(10, row, "> ", C_GOLD, C_PANEL);
            txt_str(12, row, title_items[i], C_WHITE, C_PANEL);
            txt_str(28, row, " <", C_GOLD, C_PANEL);
        } else {
            txt_str(12, row, title_items[i], C_GREY, C_TRANS);
        }
    }

    txt_center(25, "ARROWS/WASD: MOVE   Z/SPACE: FIRE/BEAM", C_DIM, C_TRANS);
    txt_center(26, "X/K: FIGHTER/DROP/CHART   ENTER: START", C_DIM, C_TRANS);
    txt_center(28, "PICOCOMPUTER 6502  -  OPL2 FM AUDIO", C_TEAL, C_TRANS);
}

static void enter_title(void)
{
    g_mode = GM_TITLE;
    title_ticks = 0;
    video_set_mode(VID_MODE_MENU, 0);
    title_draw_menu();
    music_play("ROM:Title.vgm");
}

static void title_update(void)
{
    title_ticks++;

    /* Hover the Ark gently in the middle of the title screen */
    int16_t ay = 76 + (sin256((uint8_t)(title_ticks << 1)) >> 6);
    uint16_t ark_ptr = (uint16_t)(XRAM_IMG_ARK + (((title_ticks >> 4) & 1u) * ARK_FRAME_BYTES));
    spr_place(0, ark_ptr, PAL_ARK, MODE5_SIZE(ARK_W, ARK_H), MODE5_4BPP, (SCREEN_W - ARK_W) / 2, ay);

    /* Orbit a little saucer around the Ark */
    uint8_t ang = (uint8_t)(title_ticks * 2u);
    int16_t sx = (SCREEN_W / 2 - SAUCER_W / 2) + ((cos256(ang) * 72) >> 8);
    int16_t sy = (ay + 12) + ((sin256(ang) * 20) >> 8);
    uint16_t sau_ptr = (uint16_t)(XRAM_IMG_SAUCER + (((title_ticks >> 2) & 3u) * SAUCER_FRAME_BYTES));
    spr_place(1, sau_ptr, PAL_SAUCER, MODE5_SIZE(SAUCER_W, SAUCER_H), MODE5_4BPP, sx, sy);

    if (in_pressed & IN_UP) {
        title_sel = (title_sel == 0) ? (TITLE_NITEMS - 1) : (uint8_t)(title_sel - 1);
        sfx_player(SFX_BLIP, SFX_PRI_LOW);
        title_draw_menu();
    } else if (in_pressed & IN_DOWN) {
        title_sel = (uint8_t)((title_sel + 1) % TITLE_NITEMS);
        sfx_player(SFX_BLIP, SFX_PRI_LOW);
        title_draw_menu();
    } else if (in_pressed & (IN_FIRE | IN_START)) {
        sfx_player(SFX_DOCK, SFX_PRI_EVENT);
        /* Modes will be wired in Milestones 2-4 */
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

        if (g_mode == GM_TITLE) {
            video_twinkle_stars(g_frame);
            title_update();
        }
    }
}
