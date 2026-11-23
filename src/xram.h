/* RP6502 XRAM Layout for Cosmic Arc II */

#ifndef XRAM_H
#define XRAM_H

#include <rp6502.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "gen/assets.h"

/* ------------------------------------------------------------------ */
/* Screen & Hardware Constants                                        */
/* ------------------------------------------------------------------ */

#define SCREEN_W 320
#define SCREEN_H 240

#define KEYBOARD_NO_KEY 0
#define KEYBOARD_PRESSED(keys, code) ((keys)[(code) >> 3] & (1 << ((code) & 7)))
#define xreg_ria_keyboard(...) xreg(0, 0, 0, __VA_ARGS__)

typedef struct {
    uint8_t keys[32];
} keyboard_t;

#define GAMEPAD_PLAYERS 4
#define GAMEPAD_DPAD_UP    0x01
#define GAMEPAD_DPAD_DOWN  0x02
#define GAMEPAD_DPAD_LEFT  0x04
#define GAMEPAD_DPAD_RIGHT 0x08
#define GAMEPAD_FEAT_CONNECTED 0x80

#define GAMEPAD_BTN0_A  0x01
#define GAMEPAD_BTN0_B  0x02
#define GAMEPAD_BTN0_C  0x04
#define GAMEPAD_BTN0_X  0x08
#define GAMEPAD_BTN0_Y  0x10
#define GAMEPAD_BTN0_Z  0x20
#define GAMEPAD_BTN0_L1 0x40
#define GAMEPAD_BTN0_R1 0x80

#define GAMEPAD_BTN1_SELECT 0x04
#define GAMEPAD_BTN1_START  0x08

#define xreg_ria_gamepad(...) xreg(0, 0, 2, __VA_ARGS__)

typedef struct {
    uint8_t dpad;
    uint8_t sticks;
    uint8_t btn0;
    uint8_t btn1;
    int8_t lx, ly, rx, ry;
    uint8_t l2, r2;
} gamepad_player_t;

typedef struct {
    gamepad_player_t player[GAMEPAD_PLAYERS];
} gamepad_t;

/* ------------------------------------------------------------------ */
/* RIA OPL2 Soundcard                                                 */
/* ------------------------------------------------------------------ */

#define xreg_ria_opl(...) xreg(0, 1, 1, __VA_ARGS__)

typedef struct {
    uint8_t reg[256];
} opl_t;

/* ------------------------------------------------------------------ */
/* VGA Modes and Configuration Structs                                */
/* ------------------------------------------------------------------ */

#define xreg_vga_canvas(...) xreg(1, 0, 0, __VA_ARGS__)
#define CANVAS_320X240 1

#define xreg_vga_mode1(...) xreg(1, 0, 1, 1, __VA_ARGS__)
#define MODE1_4BPP 0x02
#define MODE1_8X8  0x00

typedef struct {
    bool x_wrap;
    bool y_wrap;
    int16_t x_pos_px;
    int16_t y_pos_px;
    int16_t width_chars;
    int16_t height_chars;
    uint16_t xram_data_ptr;
    uint16_t xram_palette_ptr;
    uint16_t xram_font_ptr;
} mode1_config_t;

typedef struct {
    uint8_t glyph_code;
    uint8_t bg_fg_index;
} mode1_4bpp_data_t;

#define xreg_vga_mode2(...) xreg(1, 0, 1, 2, __VA_ARGS__)
#define MODE2_4BPP 0x02
#define MODE2_8X8  0x00

typedef struct {
    bool x_wrap;
    bool y_wrap;
    int16_t x_pos_px;
    int16_t y_pos_px;
    int16_t width_tiles;
    int16_t height_tiles;
    uint16_t xram_data_ptr;
    uint16_t xram_palette_ptr;
    uint16_t xram_tile_ptr;
} mode2_config_t;

#define xreg_vga_mode5(...) xreg(1, 0, 1, 5, __VA_ARGS__)
#define MODE5_4BPP   0x02
#define MODE5_CUSTOM 0x38
#define MODE5_HFLIP  0x10
#define MODE5_VFLIP  0x20

#define MODE5_SIZE(w, h) (uint8_t)(((((h) / 4) - 1) << 4) | (((w) / 4) - 1))

typedef struct {
    int16_t x_pos_px;
    int16_t y_pos_px;
    uint16_t xram_sprite_ptr;
    uint16_t palette_ptr;
    uint8_t width_height;
    uint8_t options;
} mode5_csprite_t;

/* ------------------------------------------------------------------ */
/* Layer & Sprite Dimensions                                          */
/* ------------------------------------------------------------------ */

#define SPR_TOTAL_COUNT 64

/* Fullscreen 40x30 Mode 1 text buffer (used for menus, map, chart, port, achievements) */
#define TXT_COLS 40
#define TXT_ROWS 30

/* Combat / Planet HUD bars (top 2 rows = 16px, bottom 2 rows = 16px, msg 4 rows) */
#define HUD_TOP_ROWS 2
#define HUD_BOT_ROWS 2
#define MSG_ROWS     4
#define MSG_Y        48

/* Space starfield tilemap: 40x30 (8x8 tiles = 320x240) */
#define SPACE_MAP_COLS 40
#define SPACE_MAP_ROWS 30

/* Planet sky tilemap (plane 0): 64x26 (512x208, wraps at 0.5x scroll) */
#define PSKY_MAP_COLS 64
#define PSKY_MAP_ROWS 26

/* Planet ground/towers tilemap (plane 1): 128x26 (1024x208, wraps at 1x scroll!) */
#define PGND_MAP_COLS 128
#define PGND_MAP_ROWS 26

#define SFX_DATA_MAX 2048

typedef struct {
    uint16_t c[16];
} palette_t;

/* ------------------------------------------------------------------ */
/* Full XRAM Layout                                                   */
/* ------------------------------------------------------------------ */

typedef struct {
    /* OPL2 registers MUST be page-aligned at 0x0000 */
    opl_t opl;

    keyboard_t keyboard;
    gamepad_t gamepad;

    /* Video configuration structs (even-aligned) */
    mode2_config_t bg0_config;
    mode2_config_t bg1_config;
    mode1_config_t txt_config;
    mode1_config_t hud_top_config;
    mode1_config_t hud_bot_config;
    mode1_config_t msg_config;

    /* Hardware sprites */
    mode5_csprite_t spr[SPR_TOTAL_COUNT];

    /* Palettes */
    palette_t palettes[PAL_COUNT];

    /* Text buffers (double-buffered fullscreen txt in XRAM for flicker-free menus) */
    mode1_4bpp_data_t txt[2][TXT_ROWS][TXT_COLS];
    mode1_4bpp_data_t hud_top[HUD_TOP_ROWS][TXT_COLS];
    mode1_4bpp_data_t hud_bot[HUD_BOT_ROWS][TXT_COLS];
    mode1_4bpp_data_t msg[MSG_ROWS][TXT_COLS];

    /* Sprite & tile graphics loaded by ROM */
    uint8_t img_ark[ARK_TOTAL_BYTES];
    uint8_t img_rock_l[ROCK_L_TOTAL_BYTES];
    uint8_t img_rock_m[ROCK_M_TOTAL_BYTES];
    uint8_t img_rock_s[ROCK_S_TOTAL_BYTES];
    uint8_t img_saucer[SAUCER_TOTAL_BYTES];
    uint8_t img_pirates[PIRATES_TOTAL_BYTES];
    uint8_t img_shots[SHOTS_TOTAL_BYTES];
    uint8_t img_boom[BOOM_TOTAL_BYTES];
    uint8_t img_creatures[CREATURES_TOTAL_BYTES];
    uint8_t img_cargo[CARGO_TOTAL_BYTES];
    uint8_t img_beam[BEAM_TOTAL_BYTES];
    uint8_t img_gun[GUN_TOTAL_BYTES];
    uint8_t img_pulse[PULSE_TOTAL_BYTES];
    uint8_t img_tunnel[TUNNEL_TOTAL_BYTES];

    uint8_t tiles_space[TILES_SPACE_TOTAL_BYTES];
    uint8_t tiles_planet[TILES_PLANET_TOTAL_BYTES];

    /* Tilemaps (filled dynamically in XRAM) */
    uint8_t map_space[SPACE_MAP_ROWS][SPACE_MAP_COLS];
    uint8_t map_psky[PSKY_MAP_ROWS][PSKY_MAP_COLS];
    uint8_t map_pgnd[PGND_MAP_ROWS][PGND_MAP_COLS];

    /* OPL2 SFX blob loaded by ROM */
    uint8_t sfx_data[SFX_DATA_MAX];
} xram_layout_t;

#define XRAM_OPL            offsetof(xram_layout_t, opl)
_Static_assert((XRAM_OPL & 0xFF) == 0, "OPL2 must be on a page boundary");

#define XRAM_KEYBOARD       offsetof(xram_layout_t, keyboard)
#define XRAM_GAMEPAD        offsetof(xram_layout_t, gamepad)
#define XRAM_BG0_CONFIG     offsetof(xram_layout_t, bg0_config)
#define XRAM_BG1_CONFIG     offsetof(xram_layout_t, bg1_config)
#define XRAM_TXT_CONFIG     offsetof(xram_layout_t, txt_config)
#define XRAM_HUD_TOP_CONFIG offsetof(xram_layout_t, hud_top_config)
#define XRAM_HUD_BOT_CONFIG offsetof(xram_layout_t, hud_bot_config)
#define XRAM_MSG_CONFIG     offsetof(xram_layout_t, msg_config)
#define XRAM_SPR            offsetof(xram_layout_t, spr)
#define XRAM_PALETTES       offsetof(xram_layout_t, palettes)
#define XRAM_TXT            offsetof(xram_layout_t, txt)
#define XRAM_TXT_PAGE_BYTES (TXT_ROWS * TXT_COLS * sizeof(mode1_4bpp_data_t))
#define XRAM_HUD_TOP        offsetof(xram_layout_t, hud_top)
#define XRAM_HUD_BOT        offsetof(xram_layout_t, hud_bot)
#define XRAM_MSG            offsetof(xram_layout_t, msg)

#define XRAM_IMG_ARK        offsetof(xram_layout_t, img_ark)
#define XRAM_IMG_ROCK_L     offsetof(xram_layout_t, img_rock_l)
#define XRAM_IMG_ROCK_M     offsetof(xram_layout_t, img_rock_m)
#define XRAM_IMG_ROCK_S     offsetof(xram_layout_t, img_rock_s)
#define XRAM_IMG_SAUCER     offsetof(xram_layout_t, img_saucer)
#define XRAM_IMG_PIRATES    offsetof(xram_layout_t, img_pirates)
#define XRAM_IMG_SHOTS      offsetof(xram_layout_t, img_shots)
#define XRAM_IMG_BOOM       offsetof(xram_layout_t, img_boom)
#define XRAM_IMG_CREATURES  offsetof(xram_layout_t, img_creatures)
#define XRAM_IMG_CARGO      offsetof(xram_layout_t, img_cargo)
#define XRAM_IMG_BEAM       offsetof(xram_layout_t, img_beam)
#define XRAM_IMG_GUN        offsetof(xram_layout_t, img_gun)
#define XRAM_IMG_PULSE      offsetof(xram_layout_t, img_pulse)
#define XRAM_IMG_TUNNEL     offsetof(xram_layout_t, img_tunnel)

#define XRAM_TILES_SPACE    offsetof(xram_layout_t, tiles_space)
#define XRAM_TILES_PLANET   offsetof(xram_layout_t, tiles_planet)
#define XRAM_MAP_SPACE      offsetof(xram_layout_t, map_space)
#define XRAM_MAP_PSKY       offsetof(xram_layout_t, map_psky)
#define XRAM_MAP_PGND       offsetof(xram_layout_t, map_pgnd)
#define XRAM_SFX_DATA       offsetof(xram_layout_t, sfx_data)

#define PAL_ADDR(p) (uint16_t)(XRAM_PALETTES + (uint16_t)(p) * sizeof(palette_t))

_Static_assert(sizeof(xram_layout_t) <= 0xFF00u, "xram_layout_t exceeds XRAM budget");

#endif /* XRAM_H */
