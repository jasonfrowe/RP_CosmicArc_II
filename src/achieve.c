#include "achieve.h"
#include "galaxy.h"
#include "game.h"
#include "input.h"
#include "sfx.h"
#include "video.h"

const char *const ACH_NAMES[ACH_COUNT] = {
    "PAIRS",     "SURVEY",   "ACE",      "CHARTED",
    "TYCOON",    "REFIT",    "RELIC",    "UNTOUCHED",
    "SWEEP",     "STORMS",   "FAR SHORE","DEEP RAID",
    "MERCHANT",  "BANKER",   "LOADED",   "LIFEBOAT"
};

static const char *const ACH_DESC[ACH_COUNT] = {
    "KEEP A PAIR (2/2) OF ALL 6 SPECIES",
    "KEEP 1 OF EACH SPECIES BY STARDATE 150",
    "DESTROY 50 PIRATES IN DEEP SPACE",
    "VISIT ALL 64 SECTORS OF THE GALAXY",
    "CARRY 10,000 CREDITS ABOARD THE ARK",
    "MAX OUT HULL (200) AND HOLDS (20)",
    "DELIVER THE OUTER-RIM RELIC TO A PORT",
    "CLEAR AN ENCOUNTER WITHOUT A HIT",
    "CATCH ALL CREATURES, NO SHUTTLE LOST",
    "SHOOT DOWN 5 PLANETARY STORM ROCKS",
    "REACH SECTOR 64 IN THE OUTER RIM",
    "SURVIVE A WAVE-10 DEEP SPACE RAID",
    "SELL 50 COMMODITY UNITS AT PORTS",
    "SAVE 5,000 CREDITS IN THE BANK",
    "FILL ALL 20 CARGO HOLDS AT ONCE",
    "RETURN HOME IN THE ESCAPE POD"
};

static const uint8_t ACH_GLYPH[ACH_COUNT] = {
    2, 15, 24, 9,
    '$', 234, 4, 3,
    1, 19, 175, 21,
    157, 156, 254, 30
};

static uint8_t ac_sel = 0;

static void achieve_draw_card(uint8_t i)
{
    static const uint8_t col_fg[4] = { C_GOLD, C_CYAN, C_GREEN, C_MAGENTA };
    uint8_t gx = (uint8_t)(i & 3u);
    uint8_t gy = (uint8_t)(i >> 2);
    uint8_t x = (uint8_t)(1 + gx * 10);
    uint8_t y = (uint8_t)(4 + gy * 4);
    bool has = (g_voy.ach_bits & (uint16_t)(1u << i)) != 0;
    bool sel = (i == ac_sel);
    uint8_t bg = sel ? C_FRAME : C_PANEL;
    uint8_t fg = has ? col_fg[gx] : C_DIM;

    txt_box(x, y, 9, 3, sel ? C_WHITE : (has ? fg : C_DIM), bg);
    txt_put(x + 1, y + 1, ACH_GLYPH[i], has ? fg : C_DIM, bg);
    for (uint8_t k = 0; k < 6 && ACH_NAMES[i][k]; k++)
        txt_put((uint8_t)(x + 2 + k), y + 1, (uint8_t)ACH_NAMES[i][k],
                has ? C_WHITE : C_GREY, bg);
}

static void achieve_draw_desc(void)
{
    for (uint8_t r = 22; r <= 26; r++)
        txt_fill(1, r, 38, C_PANEL);

    bool has = (g_voy.ach_bits & (uint16_t)(1u << ac_sel)) != 0;
    txt_str(2, 22, ACH_NAMES[ac_sel], C_GOLD, C_PANEL);
    txt_str(15, 22, has ? "[EARNED]" : "[LOCKED]", has ? C_GREEN : C_DIM, C_PANEL);
    txt_str(1, 24, ACH_DESC[ac_sel], C_WHITE, C_PANEL);

    if (ac_sel == ACH_ACE) {
        txt_str(2, 26, "PIRATES DESTROYED: ", C_GREY, C_PANEL);
        txt_num(21, 26, g_voy.kills, 3, C_CYAN, C_PANEL);
        txt_str(24, 26, "/50", C_GREY, C_PANEL);
    } else if (ac_sel == ACH_CHARTED) {
        uint8_t v = 0;
        for (uint8_t i = 0; i < GX_SECTORS; i++) if (g_voy.visited[i] & 1u) v++;
        txt_str(2, 26, "SECTORS VISITED: ", C_GREY, C_PANEL);
        txt_num(19, 26, v, 2, C_CYAN, C_PANEL);
        txt_str(21, 26, "/64", C_GREY, C_PANEL);
    } else if (ac_sel == ACH_MERCHANT) {
        txt_str(2, 26, "GOODS SOLD: ", C_GREY, C_PANEL);
        txt_num(14, 26, g_voy.sold_goods, 3, C_CYAN, C_PANEL);
        txt_str(17, 26, "/50", C_GREY, C_PANEL);
    } else if (ac_sel == ACH_STORMS) {
        txt_str(2, 26, "STORM ROCKS SHOT: ", C_GREY, C_PANEL);
        txt_num(20, 26, g_voy.storms, 2, C_CYAN, C_PANEL);
        txt_str(22, 26, "/5", C_GREY, C_PANEL);
    } else if (ac_sel == ACH_PAIRS || ac_sel == ACH_SURVEY) {
        txt_str(2, 26, "PAIRS (OUT OF 2): ", C_GREY, C_PANEL);
        for (uint8_t i = 0; i < NUM_SPECIES; i++) {
            txt_put((uint8_t)(20 + i * 3), 26, (uint8_t)('0' + g_voy.keep[i]),
                    g_voy.keep[i] >= 2 ? C_GREEN : (g_voy.keep[i] ? C_GOLD : C_DIM), C_PANEL);
        }
    }
}

static void achieve_draw(void)
{
    txt_clear(C_TRANS);
    galaxy_draw_tabs(3);

    uint8_t earned_cnt = 0;
    for (uint8_t i = 0; i < ACH_COUNT; i++) {
        if (g_voy.ach_bits & (uint16_t)(1u << i)) earned_cnt++;
    }
    for (uint8_t i = 0; i < ACH_COUNT; i++)
        achieve_draw_card(i);

    txt_box(0, 21, 40, 7, C_CYAN, C_PANEL);
    txt_str(26, 21, " EARNED ", C_GREY, C_PANEL);
    txt_num(34, 21, earned_cnt, 2, C_GREEN, C_PANEL);
    txt_str(36, 21, "/16", C_GREY, C_PANEL);
    achieve_draw_desc();
    txt_center(29, "D-PAD:SELECT   B:BACK   L1/R1:PAGES", C_DIM, C_TRANS);
}

void achieve_enter(void)
{
    ach_check_all();
    video_set_mode(VID_MODE_MENU, 0);
    achieve_draw();
}

ach_action_t achieve_update(void)
{
    video_twinkle_stars(g_frame);
    uint8_t old = ac_sel;

    if (in_pressed & IN_LEFT)  ac_sel = (uint8_t)((ac_sel & 12u) | ((ac_sel + 3u) & 3u));
    if (in_pressed & IN_RIGHT) ac_sel = (uint8_t)((ac_sel & 12u) | ((ac_sel + 1u) & 3u));
    if (in_pressed & IN_UP)    ac_sel = (uint8_t)((ac_sel + 12u) & 15u);
    if (in_pressed & IN_DOWN)  ac_sel = (uint8_t)((ac_sel + 4u) & 15u);

    if (ac_sel != old) {
        sfx_player(SFX_BLIP, SFX_PRI_LOW);
        achieve_draw_card(old);
        achieve_draw_card(ac_sel);
        achieve_draw_desc();
    }
    if (in_pressed & IN_L1) {
        sfx_player(SFX_BLIP, SFX_PRI_LOW);
        return ACH_PREV;
    }
    if (in_pressed & IN_R1) {
        sfx_player(SFX_BLIP, SFX_PRI_LOW);
        return ACH_NEXT;
    }
    if (in_pressed & IN_B) {
        sfx_player(SFX_BLIP, SFX_PRI_LOW);
        return ACH_BACK;
    }
    return ACH_STAY;
}
