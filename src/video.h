/* RP6502 VGA display manager: modes, palettes, tilemaps, text, HUD, sprites */

#ifndef VIDEO_H
#define VIDEO_H

#include <stdbool.h>
#include <stdint.h>
#include "xram.h"

typedef enum {
    VID_MODE_NONE = 0,
    VID_MODE_MENU,   /* Plane 0 starfield + Plane 1 sprites + Plane 2 40x30 text */
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

/* Top HUD (2 rows, y=0..15) and Bottom HUD (2 rows, y=224..239) */
void hud_top_clear(void);
void hud_top_put(uint8_t col, uint8_t row, uint8_t ch, uint8_t fg, uint8_t bg);
void hud_top_str(uint8_t col, uint8_t row, const char *s, uint8_t fg);
void hud_top_num(uint8_t col, uint8_t row, uint32_t val, uint8_t width, uint8_t fg);
void hud_top_bar(uint8_t col, uint8_t row, uint8_t cells, uint8_t val, uint8_t maxv,
                 uint8_t fg_hi, uint8_t fg_lo);

void hud_bot_clear(void);
void hud_bot_put(uint8_t col, uint8_t row, uint8_t ch, uint8_t fg, uint8_t bg);
void hud_bot_str(uint8_t col, uint8_t row, const char *s, uint8_t fg);
void hud_bot_bar(uint8_t col, uint8_t row, uint8_t cells, uint8_t val, uint8_t maxv,
                 uint8_t fg_hi, uint8_t fg_lo);

/* Center message overlay (4 rows at MSG_Y) */
void msg_clear(void);
void msg_center(uint8_t row, const char *s, uint8_t fg);
void msg_flash(const char *s, uint8_t fg, uint8_t frames);
void msg_tick(void);

#endif /* VIDEO_H */
