#include <rp6502.h>
#include <string.h>
#include "math_fixed.h"
#define DEFINE_PALETTE_DATA
#include "gen/assets.h"
#include "video.h"

#define SPR_HIDDEN_Y (-64)

mode5_csprite_t spr[SPR_TOTAL_COUNT];

static mode1_4bpp_data_t hud_top_buf[HUD_TOP_ROWS][TXT_COLS];
static mode1_4bpp_data_t hud_bot_buf[HUD_BOT_ROWS][TXT_COLS];
static mode1_4bpp_data_t msg_buf[MSG_ROWS][TXT_COLS];

static bool hud_top_dirty, hud_bot_dirty, msg_dirty, spr_dirty;
static uint8_t msg_timer;
static mode2_config_t bg0_cfg, bg1_cfg;
static bool scroll_dirty;

static void put_cell(mode1_4bpp_data_t *c, uint8_t ch, uint8_t fg, uint8_t bg)
{
    c->glyph_code = ch;
    c->bg_fg_index = (uint8_t)(((bg & 0x0Fu) << 4) | (fg & 0x0Fu));
}

static void write_mode1_cfg(uint16_t addr, int16_t y, int16_t w, int16_t h, uint16_t data)
{
    mode1_config_t cfg = {
        .x_wrap = false,
        .y_wrap = false,
        .x_pos_px = 0,
        .y_pos_px = y,
        .width_chars = w,
        .height_chars = h,
        .xram_data_ptr = data,
        .xram_palette_ptr = PAL_ADDR(PAL_HUD),
        .xram_font_ptr = 0xFFFFu, /* Built-in CP437 8x8 font */
    };
    xram0_write(addr, &cfg, sizeof(cfg));
}

static void init_starfield_map(void)
{
    /* Deterministic pleasing starfield across 40x30 tiles */
    uint8_t row[SPACE_MAP_COLS];
    uint16_t s = 0x5A3Cu;
    for (uint8_t r = 0; r < SPACE_MAP_ROWS; r++) {
        for (uint8_t c = 0; c < SPACE_MAP_COLS; c++) {
            s ^= s << 7;
            s ^= s >> 9;
            s ^= s << 8;
            uint8_t v = (uint8_t)(s & 0x1Fu);
            if (v == 0) row[c] = 1;
            else if (v == 1) row[c] = 2;
            else if (v == 2) row[c] = 3;
            else if (v == 3) row[c] = 4;
            else if (v == 4) row[c] = 5;
            else if (v == 5) row[c] = 6;
            else if (v == 6 && ((r + c) & 3) == 0) row[c] = 7;
            else row[c] = 0;
        }
        xram0_write((uint16_t)(XRAM_MAP_SPACE + (uint16_t)r * SPACE_MAP_COLS), row, SPACE_MAP_COLS);
    }
}

void video_init(void)
{
    xreg_vga_canvas(CANVAS_320X240);

    /* Upload all 16-color palettes to XRAM */
    xram0_write(XRAM_PALETTES, game_palettes, sizeof(game_palettes));

    /* Build starfield tilemap once */
    init_starfield_map();

    /* Init text configs */
    write_mode1_cfg(XRAM_TXT_CONFIG, 0, TXT_COLS, TXT_ROWS, XRAM_TXT);
    write_mode1_cfg(XRAM_HUD_TOP_CONFIG, 0, TXT_COLS, HUD_TOP_ROWS, XRAM_HUD_TOP);
    write_mode1_cfg(XRAM_HUD_BOT_CONFIG, 224, TXT_COLS, HUD_BOT_ROWS, XRAM_HUD_BOT);
    write_mode1_cfg(XRAM_MSG_CONFIG, MSG_Y, TXT_COLS, MSG_ROWS, XRAM_MSG);

    spr_hide_all();
    txt_clear(C_TRANS);
    hud_top_clear();
    hud_bot_clear();
    msg_clear();
    video_commit();
}

void video_set_planet_palette(uint8_t world_idx)
{
    if (world_idx >= 6) world_idx = 0;
    uint16_t pal = PAL_ADDR(PAL_WORLD_0 + world_idx);
    bg0_cfg.xram_palette_ptr = pal;
    bg1_cfg.xram_palette_ptr = pal;
    xram0_write(XRAM_BG0_CONFIG, &bg0_cfg, sizeof(bg0_cfg));
    xram0_write(XRAM_BG1_CONFIG, &bg1_cfg, sizeof(bg1_cfg));
}

void video_set_mode(vid_mode_t mode, uint8_t world_idx)
{
    /* Reset canvas to clear previous plane registrations cleanly */
    xreg_vga_canvas(CANVAS_320X240);
    spr_hide_all();
    msg_clear();

    if (mode == VID_MODE_MENU) {
        bg0_cfg.x_wrap = true;
        bg0_cfg.y_wrap = false;
        bg0_cfg.x_pos_px = 0;
        bg0_cfg.y_pos_px = 0;
        bg0_cfg.width_tiles = SPACE_MAP_COLS;
        bg0_cfg.height_tiles = SPACE_MAP_ROWS;
        bg0_cfg.xram_data_ptr = XRAM_MAP_SPACE;
        bg0_cfg.xram_palette_ptr = PAL_ADDR(PAL_SPACE_BG);
        bg0_cfg.xram_tile_ptr = XRAM_TILES_SPACE;
        xram0_write(XRAM_BG0_CONFIG, &bg0_cfg, sizeof(bg0_cfg));

        xreg_vga_mode2(MODE2_4BPP | MODE2_8X8, XRAM_BG0_CONFIG, 0, 0, SCREEN_H);
        xreg_vga_mode5(MODE5_CUSTOM, XRAM_SPR, SPR_TOTAL_COUNT, 1, 0, SCREEN_H);
        xreg_vga_mode1(MODE1_4BPP | MODE1_8X8, XRAM_TXT_CONFIG, 2, 0, SCREEN_H);
    } else if (mode == VID_MODE_SPACE) {
        bg0_cfg.x_wrap = true;
        bg0_cfg.y_wrap = false;
        bg0_cfg.x_pos_px = 0;
        bg0_cfg.y_pos_px = 0;
        bg0_cfg.width_tiles = SPACE_MAP_COLS;
        bg0_cfg.height_tiles = SPACE_MAP_ROWS;
        bg0_cfg.xram_data_ptr = XRAM_MAP_SPACE;
        bg0_cfg.xram_palette_ptr = PAL_ADDR(PAL_SPACE_BG);
        bg0_cfg.xram_tile_ptr = XRAM_TILES_SPACE;
        xram0_write(XRAM_BG0_CONFIG, &bg0_cfg, sizeof(bg0_cfg));

        xreg_vga_mode2(MODE2_4BPP | MODE2_8X8, XRAM_BG0_CONFIG, 0, 0, SCREEN_H);
        xreg_vga_mode5(MODE5_CUSTOM, XRAM_SPR, SPR_TOTAL_COUNT, 1, 16, SCREEN_H);
        xreg_vga_mode1(MODE1_4BPP | MODE1_8X8, XRAM_HUD_TOP_CONFIG, 2, 0, 16);
        xreg_vga_mode1(MODE1_4BPP | MODE1_8X8, XRAM_MSG_CONFIG, 2, MSG_Y, MSG_Y + MSG_ROWS * 8);
    } else if (mode == VID_MODE_PLANET) {
        if (world_idx >= 6) world_idx = 0;
        uint16_t pal = PAL_ADDR(PAL_WORLD_0 + world_idx);

        bg0_cfg.x_wrap = true;
        bg0_cfg.y_wrap = false;
        bg0_cfg.x_pos_px = 0;
        bg0_cfg.y_pos_px = 16;
        bg0_cfg.width_tiles = PSKY_MAP_COLS;
        bg0_cfg.height_tiles = PSKY_MAP_ROWS;
        bg0_cfg.xram_data_ptr = XRAM_MAP_PSKY;
        bg0_cfg.xram_palette_ptr = pal;
        bg0_cfg.xram_tile_ptr = XRAM_TILES_PLANET;
        xram0_write(XRAM_BG0_CONFIG, &bg0_cfg, sizeof(bg0_cfg));

        bg1_cfg.x_wrap = true;
        bg1_cfg.y_wrap = false;
        bg1_cfg.x_pos_px = 0;
        bg1_cfg.y_pos_px = 16;
        bg1_cfg.width_tiles = PGND_MAP_COLS;
        bg1_cfg.height_tiles = PGND_MAP_ROWS;
        bg1_cfg.xram_data_ptr = XRAM_MAP_PGND;
        bg1_cfg.xram_palette_ptr = pal;
        bg1_cfg.xram_tile_ptr = XRAM_TILES_PLANET;
        xram0_write(XRAM_BG1_CONFIG, &bg1_cfg, sizeof(bg1_cfg));

        xreg_vga_mode2(MODE2_4BPP | MODE2_8X8, XRAM_BG0_CONFIG, 0, 16, 224);
        xreg_vga_mode2(MODE2_4BPP | MODE2_8X8, XRAM_BG1_CONFIG, 1, 16, 224);
        xreg_vga_mode5(MODE5_CUSTOM, XRAM_SPR, SPR_TOTAL_COUNT, 2, 16, 224);
        xreg_vga_mode1(MODE1_4BPP | MODE1_8X8, XRAM_HUD_TOP_CONFIG, 2, 0, 16);
        xreg_vga_mode1(MODE1_4BPP | MODE1_8X8, XRAM_HUD_BOT_CONFIG, 2, 224, 240);
        xreg_vga_mode1(MODE1_4BPP | MODE1_8X8, XRAM_MSG_CONFIG, 2, MSG_Y, MSG_Y + MSG_ROWS * 8);
    }
    video_commit();
}

void video_set_planet_scroll(int16_t cam_x)
{
    int16_t sx0 = (int16_t)(-(cam_x >> 1));
    int16_t sx1 = (int16_t)(-cam_x);
    if (sx0 != bg0_cfg.x_pos_px || sx1 != bg1_cfg.x_pos_px) {
        bg0_cfg.x_pos_px = sx0;
        bg1_cfg.x_pos_px = sx1;
        scroll_dirty = true;
    }
}

void video_twinkle_stars(uint8_t frame)
{
    if ((frame & 7u) != 0)
        return;
    /* Cycle twinkle indices 9..12 in PAL_SPACE_BG */
    static const uint16_t tw[4] = {
        0xFEBFu, /* bright blue-white */
        0x798Fu, /* mid blue */
        0x4A7Fu, /* warm gold */
        0x3926u, /* dim star */
    };
    uint8_t p = (frame >> 3) & 3u;
    uint16_t c[4];
    for (uint8_t i = 0; i < 4; i++)
        c[i] = tw[(i + p) & 3u];
    xram0_write((uint16_t)(PAL_ADDR(PAL_SPACE_BG) + 9u * 2u), c, sizeof(c));
}

void video_commit(void)
{
    if (spr_dirty) {
        xram0_write(XRAM_SPR, spr, sizeof(spr));
        spr_dirty = false;
    }
    if (scroll_dirty) {
        xram0_write((uint16_t)(XRAM_BG0_CONFIG + offsetof(mode2_config_t, x_pos_px)),
                    &bg0_cfg.x_pos_px, 2);
        xram0_write((uint16_t)(XRAM_BG1_CONFIG + offsetof(mode2_config_t, x_pos_px)),
                    &bg1_cfg.x_pos_px, 2);
        scroll_dirty = false;
    }
    if (hud_top_dirty) {
        xram0_write(XRAM_HUD_TOP, hud_top_buf, sizeof(hud_top_buf));
        hud_top_dirty = false;
    }
    if (hud_bot_dirty) {
        xram0_write(XRAM_HUD_BOT, hud_bot_buf, sizeof(hud_bot_buf));
        hud_bot_dirty = false;
    }
    if (msg_dirty) {
        xram0_write(XRAM_MSG, msg_buf, sizeof(msg_buf));
        msg_dirty = false;
    }
}

/* ------------------------------------------------------------------ */
/* Sprites                                                            */
/* ------------------------------------------------------------------ */

void spr_hide_all(void)
{
    for (uint8_t i = 0; i < SPR_TOTAL_COUNT; i++) {
        spr[i].x_pos_px = 0;
        spr[i].y_pos_px = SPR_HIDDEN_Y;
        spr[i].xram_sprite_ptr = XRAM_IMG_ROCK_S;
        spr[i].palette_ptr = PAL_ADDR(PAL_ROCK);
        spr[i].width_height = MODE5_SIZE(8, 8);
        spr[i].options = MODE5_4BPP;
    }
    spr_dirty = true;
}

void spr_hide(uint8_t idx)
{
    if (idx < SPR_TOTAL_COUNT) {
        spr[idx].y_pos_px = SPR_HIDDEN_Y;
        spr_dirty = true;
    }
}

void spr_place(uint8_t idx, uint16_t img_addr, uint8_t pal, uint8_t wh,
               uint8_t opt, int16_t x, int16_t y)
{
    if (idx >= SPR_TOTAL_COUNT)
        return;
    mode5_csprite_t *s = &spr[idx];
    if (x <= -64 || x >= SCREEN_W || y <= -64 || y >= SCREEN_H) {
        s->y_pos_px = SPR_HIDDEN_Y;
    } else {
        s->x_pos_px = x;
        s->y_pos_px = y;
        s->xram_sprite_ptr = img_addr;
        s->palette_ptr = PAL_ADDR(pal);
        s->width_height = wh;
        s->options = opt;
    }
    spr_dirty = true;
}

/* ------------------------------------------------------------------ */
/* Number formatting helper                                           */
/* ------------------------------------------------------------------ */

static void fmt_u32(char *buf, uint32_t val, uint8_t width)
{
    buf[width] = '\0';
    for (int8_t i = (int8_t)width - 1; i >= 0; i--) {
        buf[i] = (char)('0' + (val % 10u));
        val /= 10u;
    }
}

/* ------------------------------------------------------------------ */
/* Fullscreen 40x30 text (VID_MODE_MENU) - direct incremental XRAM    */
/* ------------------------------------------------------------------ */

static mode1_4bpp_data_t txt_row_tmp[TXT_COLS];

void txt_clear(uint8_t bg)
{
    for (uint8_t c = 0; c < TXT_COLS; c++)
        put_cell(&txt_row_tmp[c], ' ', C_WHITE, bg);
    for (uint8_t r = 0; r < TXT_ROWS; r++)
        xram0_write((uint16_t)(XRAM_TXT + (uint16_t)r * TXT_COLS * 2u), txt_row_tmp, sizeof(txt_row_tmp));
}

void txt_put(uint8_t col, uint8_t row, uint8_t ch, uint8_t fg, uint8_t bg)
{
    if (col < TXT_COLS && row < TXT_ROWS) {
        uint16_t cell = (uint16_t)ch | ((uint16_t)(((bg & 0x0Fu) << 4) | (fg & 0x0Fu)) << 8);
        xram0_poke16((uint16_t)(XRAM_TXT + ((uint16_t)row * TXT_COLS + col) * 2u), cell);
    }
}

void txt_str(uint8_t col, uint8_t row, const char *s, uint8_t fg, uint8_t bg)
{
    if (row >= TXT_ROWS) return;
    uint8_t n = 0;
    uint8_t c0 = col;
    while (*s && col < TXT_COLS) {
        put_cell(&txt_row_tmp[n++], (uint8_t)*s++, fg, bg);
        col++;
    }
    if (n > 0) {
        xram0_write((uint16_t)(XRAM_TXT + ((uint16_t)row * TXT_COLS + c0) * 2u),
                    txt_row_tmp, (uint16_t)n * 2u);
    }
}

void txt_center(uint8_t row, const char *s, uint8_t fg, uint8_t bg)
{
    uint8_t len = (uint8_t)strlen(s);
    uint8_t col = (len < TXT_COLS) ? (uint8_t)((TXT_COLS - len) / 2u) : 0;
    txt_str(col, row, s, fg, bg);
}

void txt_num(uint8_t col, uint8_t row, uint32_t val, uint8_t width, uint8_t fg, uint8_t bg)
{
    char buf[12];
    fmt_u32(buf, val, width);
    txt_str(col, row, buf, fg, bg);
}

void txt_box(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t fg, uint8_t bg)
{
    if (w < 2 || h < 2) return;
    txt_put(x, y, 218, fg, bg);                 /* top-left */
    txt_put(x + w - 1, y, 191, fg, bg);         /* top-right */
    txt_put(x, y + h - 1, 192, fg, bg);         /* bot-left */
    txt_put(x + w - 1, y + h - 1, 217, fg, bg); /* bot-right */
    for (uint8_t c = 1; c < w - 1; c++) {
        txt_put(x + c, y, 196, fg, bg);
        txt_put(x + c, y + h - 1, 196, fg, bg);
    }
    for (uint8_t r = 1; r < h - 1; r++) {
        txt_put(x, y + r, 179, fg, bg);
        txt_put(x + w - 1, y + r, 179, fg, bg);
        for (uint8_t c = 1; c < w - 1; c++)
            txt_put(x + c, y + r, ' ', fg, bg);
    }
}

/* ------------------------------------------------------------------ */
/* HUD Top / Bottom & Message Banner                                  */
/* ------------------------------------------------------------------ */

void hud_top_clear(void)
{
    for (uint8_t r = 0; r < HUD_TOP_ROWS; r++)
        for (uint8_t c = 0; c < TXT_COLS; c++)
            put_cell(&hud_top_buf[r][c], ' ', C_WHITE, C_PANEL);
    hud_top_dirty = true;
}

void hud_top_put(uint8_t col, uint8_t row, uint8_t ch, uint8_t fg, uint8_t bg)
{
    if (col < TXT_COLS && row < HUD_TOP_ROWS) {
        put_cell(&hud_top_buf[row][col], ch, fg, bg);
        hud_top_dirty = true;
    }
}

void hud_top_str(uint8_t col, uint8_t row, const char *s, uint8_t fg)
{
    if (row >= HUD_TOP_ROWS) return;
    while (*s && col < TXT_COLS)
        put_cell(&hud_top_buf[row][col++], (uint8_t)*s++, fg, C_PANEL);
    hud_top_dirty = true;
}

void hud_top_num(uint8_t col, uint8_t row, uint32_t val, uint8_t width, uint8_t fg)
{
    char buf[12];
    fmt_u32(buf, val, width);
    hud_top_str(col, row, buf, fg);
}

static void draw_bar(mode1_4bpp_data_t *row_ptr, uint8_t col, uint8_t cells,
                     uint8_t val, uint8_t maxv, uint8_t fg_hi, uint8_t fg_lo)
{
    if (maxv == 0) maxv = 1;
    uint16_t filled = (uint16_t)val * cells * 2u / maxv;
    uint8_t fg = ((uint16_t)val * 4u <= maxv) ? fg_lo :
                 (((uint16_t)val * 2u <= maxv) ? C_GOLD : fg_hi);
    for (uint8_t i = 0; i < cells && (col + i) < TXT_COLS; i++) {
        uint8_t ch;
        uint8_t c = fg;
        if (filled >= 2) {
            ch = 219; /* full block */
            filled -= 2;
        } else if (filled == 1) {
            ch = 221; /* half block */
            filled = 0;
        } else {
            ch = 176; /* subtle recessed grid */
            c = C_DIM;
        }
        put_cell(&row_ptr[col + i], ch, c, C_FRAME);
    }
}

void hud_top_bar(uint8_t col, uint8_t row, uint8_t cells, uint8_t val, uint8_t maxv,
                 uint8_t fg_hi, uint8_t fg_lo)
{
    if (row < HUD_TOP_ROWS) {
        draw_bar(hud_top_buf[row], col, cells, val, maxv, fg_hi, fg_lo);
        hud_top_dirty = true;
    }
}

void hud_bot_clear(void)
{
    for (uint8_t r = 0; r < HUD_BOT_ROWS; r++)
        for (uint8_t c = 0; c < TXT_COLS; c++)
            put_cell(&hud_bot_buf[r][c], ' ', C_WHITE, C_PANEL);
    hud_bot_dirty = true;
}

void hud_bot_put(uint8_t col, uint8_t row, uint8_t ch, uint8_t fg, uint8_t bg)
{
    if (col < TXT_COLS && row < HUD_BOT_ROWS) {
        put_cell(&hud_bot_buf[row][col], ch, fg, bg);
        hud_bot_dirty = true;
    }
}

void hud_bot_str(uint8_t col, uint8_t row, const char *s, uint8_t fg)
{
    if (row >= HUD_BOT_ROWS) return;
    while (*s && col < TXT_COLS)
        put_cell(&hud_bot_buf[row][col++], (uint8_t)*s++, fg, C_PANEL);
    hud_bot_dirty = true;
}

void hud_bot_bar(uint8_t col, uint8_t row, uint8_t cells, uint8_t val, uint8_t maxv,
                 uint8_t fg_hi, uint8_t fg_lo)
{
    if (row < HUD_BOT_ROWS) {
        draw_bar(hud_bot_buf[row], col, cells, val, maxv, fg_hi, fg_lo);
        hud_bot_dirty = true;
    }
}

void msg_clear(void)
{
    for (uint8_t r = 0; r < MSG_ROWS; r++)
        for (uint8_t c = 0; c < TXT_COLS; c++)
            put_cell(&msg_buf[r][c], ' ', C_WHITE, C_TRANS);
    msg_timer = 0;
    msg_dirty = true;
}

void msg_center(uint8_t row, const char *s, uint8_t fg)
{
    if (row >= MSG_ROWS) return;
    for (uint8_t c = 0; c < TXT_COLS; c++)
        put_cell(&msg_buf[row][c], ' ', C_WHITE, C_TRANS);
    uint8_t len = (uint8_t)strlen(s);
    if (len > 0) {
        uint8_t pad_len = (uint8_t)(len + 4u);
        uint8_t col = (pad_len < TXT_COLS) ? (uint8_t)((TXT_COLS - pad_len) / 2u) : 0;
        if (pad_len < TXT_COLS) {
            put_cell(&msg_buf[row][col], 17, C_CYAN, C_FRAME);
            put_cell(&msg_buf[row][col + 1], ' ', fg, C_PANEL);
            for (uint8_t i = 0; i < len; i++)
                put_cell(&msg_buf[row][col + 2 + i], (uint8_t)s[i], fg, C_PANEL);
            put_cell(&msg_buf[row][col + 2 + len], ' ', fg, C_PANEL);
            put_cell(&msg_buf[row][col + 3 + len], 16, C_CYAN, C_FRAME);
        } else {
            for (uint8_t i = 0; i < len && i < TXT_COLS; i++)
                put_cell(&msg_buf[row][i], (uint8_t)s[i], fg, C_PANEL);
        }
    }
    msg_dirty = true;
}

void msg_flash(const char *s, uint8_t fg, uint8_t frames)
{
    msg_center(1, s, fg);
    msg_timer = frames;
}

void msg_tick(void)
{
    if (msg_timer && --msg_timer == 0)
        msg_clear();
}
