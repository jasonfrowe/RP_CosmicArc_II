/* RP6502 VGA display manager: modes, palettes, tilemaps, text, HUD, sprites */

#ifndef VIDEO_H
#define VIDEO_H

#include <stdbool.h>
#include <stdint.h>
#include "xram.h"

typedef enum {
    VID_MODE_NONE = 0,
    VID_MODE_MENU,   /* Plane 0 starfield + Plane 1 sprites + Plane 2 40x30 text */
    VID_MODE_CHART,  /* Plane 0 starfield + Plane 1 4bpp star chart tiles + Plane 2 40x30 text */
    VID_MODE_SPACE,  /* Plane 0 starfield + Plane 2 HUD/msg/sprites */
    VID_MODE_PLANET  /* Plane 0 sky + Plane 1 terrain + Plane 2 HUD/msg/sprites */
} vid_mode_t;

extern mode5_csprite_t spr[SPR_TOTAL_COUNT];

void video_init(void);
void video_set_mode(vid_mode_t mode, uint8_t world_idx);
void video_commit(void);
void video_twinkle_stars(uint8_t frame);

/* Planet scrolling (Plane 0 sky at 0.5x, Plane 1 terrain at 1x) */
void video_set_planet_scroll(int16_t cam_x);
void video_set_planet_palette(uint8_t world_idx);

/* Sprites */
void spr_hide_all(void);
void spr_hide(uint8_t idx);
void spr_place(uint8_t idx, uint16_t img_addr, uint8_t pal, uint8_t wh,
               uint8_t opt, int16_t x, int16_t y);

/* Fullscreen 40x30 Mode 1 text (VID_MODE_MENU) */
void txt_clear(uint8_t bg);
void txt_put(uint8_t col, uint8_t row, uint8_t ch, uint8_t fg, uint8_t bg);
void txt_str(uint8_t col, uint8_t row, const char *s, uint8_t fg, uint8_t bg);
void txt_center(uint8_t row, const char *s, uint8_t fg, uint8_t bg);
void txt_num(uint8_t col, uint8_t row, uint32_t val, uint8_t width, uint8_t fg, uint8_t bg);
void txt_box(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t fg, uint8_t bg);

/* Custom 4bpp HUD Tile IDs (matching tools/gen_art.py make_hud_tiles) */
#define HT_PANEL      0u
#define HT_TOP_RIM    1u
#define HT_BOT_RIM    2u
#define HT_DIV        3u
#define HT_BAR_EMPTY  4u
#define HT_BAR_L      5u
#define HT_BAR_R      6u
#define HT_BAR_GREEN  7u  /* 7..10 */
#define HT_BAR_GOLD   11u /* 11..14 */
#define HT_BAR_RED    15u /* 15..18 */
#define HT_BAR_CYAN   19u /* 19..22 */
#define HT_ICO_BOLT   23u
#define HT_ICO_SAUCER 24u
#define HT_ICO_BIO_LO 25u
#define HT_ICO_BIO_HI 26u
#define HT_ICO_CRATE  27u
#define HT_RAD_EMPTY  28u
#define HT_RAD_L      29u
#define HT_RAD_R      30u
#define HT_RAD_TOWER  31u
#define HT_RAD_ARK    32u
#define HT_RAD_BIO    33u
#define HT_RAD_SHU    34u
#define HT_RAD_RELIC  35u
#define HT_RAD_STORM  36u

/* Custom 4bpp Galactic Star Chart tile IDs (in tiles_hud.png) */
#define HT_CH_EMPTY_T 37u
#define HT_CH_H       38u
#define HT_CH_V       39u
#define HT_CH_DR      40u
#define HT_CH_DL      41u
#define HT_CH_X       42u
#define HT_CH_PORT    43u
#define HT_CH_PLANET  44u
#define HT_CH_NEBULA  45u
#define HT_CH_EMPTY   46u
#define HT_CH_SEEN    47u
#define HT_CH_ARK     48u

void chart_bg_clear(void);
void chart_bg_tile(uint8_t col, uint8_t row, uint8_t tile);
uint8_t chart_bg_get(uint8_t col, uint8_t row);

/* Top HUD (2 rows, y=0..15) and Bottom HUD (2 rows, y=224..239):
 * 4bpp graphical tile layer underneath + transparent Mode 1 text overlay */
void hud_top_clear(void);
void hud_top_tile(uint8_t col, uint8_t row, uint8_t tile);
void hud_top_put(uint8_t col, uint8_t row, uint8_t ch, uint8_t fg, uint8_t bg);
void hud_top_str(uint8_t col, uint8_t row, const char *s, uint8_t fg);
void hud_top_num(uint8_t col, uint8_t row, uint32_t val, uint8_t width, uint8_t fg);
void hud_top_bar(uint8_t col, uint8_t row, uint8_t cells, uint8_t val, uint8_t maxv, bool is_cyan);

void hud_bot_clear(void);
void hud_bot_tile(uint8_t col, uint8_t row, uint8_t tile);
void hud_bot_put(uint8_t col, uint8_t row, uint8_t ch, uint8_t fg, uint8_t bg);
void hud_bot_str(uint8_t col, uint8_t row, const char *s, uint8_t fg);
void hud_bot_bar(uint8_t col, uint8_t row, uint8_t cells, uint8_t val, uint8_t maxv, bool is_cyan);

/* Center message overlay (4 rows at MSG_Y) */
void msg_clear(void);
void msg_center(uint8_t row, const char *s, uint8_t fg);
void msg_flash(const char *s, uint8_t fg, uint8_t frames);
void msg_tick(void);

#endif /* VIDEO_H */
