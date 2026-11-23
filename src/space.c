#include <string.h>
#include "game.h"
#include "input.h"
#include "math_fixed.h"
#include "music.h"
#include "sfx.h"
#include "space.h"
#include "video.h"
#include "xram.h"

/* ------------------------------------------------------------------ */
/* Arena & Balance Constants (320x240 square pixels, 12.4 fixed point)*/
/* ------------------------------------------------------------------ */

#define FP_SHIFT    4
#define TO_FP(v)    ((int16_t)((int16_t)(v) << FP_SHIFT))
#define FROM_FP(v)  ((int16_t)((v) >> FP_SHIFT))

#define PF_TOP      16
#define PF_BOT      236
#define ARK_CX      160
#define ARK_CY      128
#define ARK_HX0     (ARK_CX - 28)
#define ARK_HX1     (ARK_CX + 28)
#define ARK_HY0     (ARK_CY - 12)
#define ARK_HY1     (ARK_CY + 12)
#define BAY_X       ARK_CX
#define BAY_Y       (ARK_CY + 18)

#define MAX_ROCKS   10
#define MAX_BOLTS   6
#define MAX_SHOTS   8
#define MAX_PIRATES 6
#define MAX_PODS    3
#define MAX_BOOMS   4

#define SHOT_COST   1
#define KILL_PAY    1
#define FIRE_GAP    8
#define BOLT_SPEED  5
#define BOLT_LIFE   38

#define FT_TETH_MAX 96
#define FT_LAUNCH   24
#define FT_DRAIN    8
#define FT_CHARGE   2
#define FT_SPEED    34  /* 2.125 px/frame in 12.4 */
#define FT_SHOT_SPD 64  /* 4.0 px/frame */
#define FT_GAP      10
#define FT_SHLIFE   36
#define FT_HOME_SPD 36
#define FT_REBUILD  240
#define TU_GAP      40
#define TU_SPEED    56

#define R_RAID      84
#define R_GUN       98
#define R_AVOID     44
#define PSHOT_DMG   6
#define PSHOT_FDMG  16
#define MISS_DMG    12
#define MISS_FDMG   16
#define RAM_FDMG    24
#define POD_PAY     16

/* Rock types */
#define RK_FREE 0
#define RK_L    1
#define RK_M    2
#define RK_S    3

/* Fighter states */
#define FT_DOCK   0
#define FT_OUT    1
#define FT_HOMING 2
#define FT_DEAD   3

/* Shot owners */
#define SH_FREE    0
#define SH_FIGHTER 1
#define SH_TURRET  2
#define SH_PIRATE  3

/* Pirate types */
#define PR_FREE 0
#define PR_RAID 1
#define PR_GUN  2
#define PR_MISS 3

/* Sub-states */
#define SS_ARRIVE 0
#define SS_START  1
#define SS_PLAY   2
#define SS_CLEAR  3
#define SS_DEPART 4
#define SS_LOST   5

/* ------------------------------------------------------------------ */
/* Entities                                                           */
/* ------------------------------------------------------------------ */

typedef struct {
    uint8_t type;
    uint8_t lane;
    uint8_t hp;
    uint8_t anim;
    int16_t x, y;     /* 12.4 fixed point center */
    int16_t vx, vy;
} rock_t;

typedef struct {
    uint8_t life;
    uint8_t dir;
    int16_t x, y;     /* integer center */
    int8_t  dx, dy;
} bolt_t;

typedef struct {
    uint8_t type;
    uint8_t life;
    int16_t x, y;     /* 12.4 fixed point */
    int16_t vx, vy;
} shot_t;

typedef struct {
    uint8_t type;
    uint8_t hp;
    uint8_t ang;
    uint8_t orbit;
    uint8_t timer;
    int16_t x, y;     /* 12.4 fixed point */
    int16_t vx, vy;
} pirate_t;

typedef struct {
    uint16_t life;
    int16_t x, y;     /* 12.4 fixed point */
    int16_t vx, vy;
} pod_t;

typedef struct {
    uint8_t timer;    /* 16..1 */
    int16_t x, y;
} boom_t;

/* ------------------------------------------------------------------ */
/* Module State                                                       */
/* ------------------------------------------------------------------ */

#define rocks  ((rock_t *)(g_arena))
#define bolts  ((bolt_t *)(g_arena + 128))
#define shots  ((shot_t *)(g_arena + 176))
static pirate_t pirates[MAX_PIRATES];
static pod_t    pods[MAX_PODS];
static boom_t   booms[MAX_BOOMS];

static uint8_t  s_state;
static uint8_t  s_wave;
static bool     s_quick;
static bool     s_ark_hit;
static int16_t  s_ark_y;
static uint16_t s_timer;
static uint8_t  s_aim;       /* 0..7 */
static uint8_t  s_fire_cool;

static uint8_t  s_rocks_left;
static uint8_t  s_rock_tm;
static uint8_t  s_raiders_left;
static uint8_t  s_guns_left;
static uint16_t s_pirate_tm;
static uint16_t s_pr_window;
static bool     s_pr_started;
static bool     s_final_spawned;
static bool     s_pirate_music;

/* Fighter */
static uint8_t  ft_st;
static int16_t  ft_x, ft_y, ft_vx, ft_vy;
static uint8_t  ft_ang;
static uint8_t  ft_cool;
static uint8_t  ft_teth;
static uint8_t  ft_drain_tm;
static uint16_t ft_rebuild_tm;
static uint8_t  tu_cool;

/* 8-direction unit vectors (0=Up, clockwise) */
static const int8_t  dir_dx[8] = {  0,  1,  1,  1,  0, -1, -1, -1 };
static const int8_t  dir_dy[8] = { -1, -1,  0,  1,  1,  1,  0, -1 };
static const uint8_t dir_ang[8] = { 192, 224, 0, 32, 64, 96, 128, 160 };

/* Spawn coords for each of the 8 lanes */
static const int16_t lane_sx[8] = { 160, 270, 314, 270, 160,  50,   6,  50 };
static const int16_t lane_sy[8] = {  22,  22, 128, 234, 234, 234, 128,  22 };
/* Speed scale (/16) so every lane arrives in equal time */
static const uint8_t lane_scale[8] = { 16, 22, 20, 22, 16, 22, 20, 22 };

static const uint8_t rock_dmg[4] = { 0, 16, 12, 8 };
static const uint8_t rock_rad[4] = { 0, 11, 7, 5 };

/* ------------------------------------------------------------------ */
/* Helpers                                                            */
/* ------------------------------------------------------------------ */

static void spawn_boom(int16_t x, int16_t y)
{
    for (uint8_t i = 0; i < MAX_BOOMS; i++) {
        if (booms[i].timer == 0) {
            booms[i].timer = 16;
            booms[i].x = x;
            booms[i].y = y;
            return;
        }
    }
}

static void ark_damage(uint8_t dmg)
{
    s_ark_hit = true;
    sfx_enemy(SFX_ARK_HIT, SFX_PRI_HIT);
    if (g_voy.energy <= dmg) {
        g_voy.energy = 0;
        s_state = SS_LOST;
        s_timer = 40;
        spawn_boom(ARK_CX - 14, ARK_CY - 4);
        spawn_boom(ARK_CX + 14, ARK_CY + 4);
        sfx_enemy(SFX_BOOM, SFX_PRI_TOP);
        msg_center(1, "ARK DESTROYED!", C_RED);
    } else {
        g_voy.energy -= dmg;
    }
}

static void energy_pay(uint8_t amt)
{
    uint8_t mx = voy_hull_max();
    uint16_t e = (uint16_t)g_voy.energy + amt;
    g_voy.energy = (e > mx) ? mx : (uint8_t)e;
}

static int16_t rock_base_speed(void)
{
    int16_t sp = (int16_t)(8 + (int16_t)s_wave);
    if (sp > 12) sp = 12; /* capped from wave 4 (~0.75-1.0 px/frame) */
    return sp;
}

static void set_rock_vel(rock_t *r, int16_t spd)
{
    int16_t sc = (int16_t)((spd * (int16_t)lane_scale[r->lane]) >> 4);
    if (sc < 4) sc = 4;
    int16_t vx = (int16_t)(-dir_dx[r->lane]) * sc;
    int16_t vy = (int16_t)(-dir_dy[r->lane]) * sc;
    if (dir_dx[r->lane] != 0 && dir_dy[r->lane] != 0) {
        vx = (int16_t)(((int32_t)vx * 181) >> 8);
        vy = (int16_t)(((int32_t)vy * 181) >> 8);
    }
    r->vx = vx;
    r->vy = vy;
}

static bool spawn_rock(uint8_t type, uint8_t lane, int16_t xfp, int16_t yfp, int16_t spd)
{
    for (uint8_t i = 0; i < MAX_ROCKS; i++) {
        if (rocks[i].type == RK_FREE) {
            rocks[i].type = type;
            rocks[i].lane = lane;
            rocks[i].hp = 2;
            rocks[i].anim = rng_u8();
            rocks[i].x = xfp;
            rocks[i].y = yfp;
            set_rock_vel(&rocks[i], spd);
            return true;
        }
    }
    return false;
}

static void hit_rock(uint8_t idx, uint8_t dmg, bool pays_energy)
{
    rock_t *r = &rocks[idx];
    if (r->hp > dmg) {
        r->hp -= dmg;
        sfx_enemy(SFX_ROCK_HIT, SFX_PRI_SHOT);
        return;
    }

    uint8_t t = r->type;
    uint8_t ln = r->lane;
    int16_t rx = r->x, ry = r->y;
    r->type = RK_FREE;

    spawn_boom(FROM_FP(rx), FROM_FP(ry));
    sfx_enemy(SFX_ROCK_HIT, SFX_PRI_HIT);
    if (pays_energy)
        energy_pay(KILL_PAY);

    if (t == RK_L || t == RK_M) {
        uint8_t nt = (uint8_t)(t + 1);
        int16_t sp = rock_base_speed() + (int16_t)(nt - RK_L) * 3;
        spawn_rock(nt, ln, rx, ry, sp);
        spawn_rock(nt, ln, rx, ry, (int16_t)(sp + 4));
    }
}

static void spawn_shot(uint8_t type, int16_t x, int16_t y, int16_t vx, int16_t vy, uint8_t life)
{
    for (uint8_t i = 0; i < MAX_SHOTS; i++) {
        if (shots[i].type == SH_FREE) {
            shots[i].type = type;
            shots[i].life = life;
            shots[i].x = TO_FP(x);
            shots[i].y = TO_FP(y);
            shots[i].vx = vx;
            shots[i].vy = vy;
            return;
        }
    }
}

static void spawn_pod(int16_t x, int16_t y)
{
    for (uint8_t i = 0; i < MAX_PODS; i++) {
        if (pods[i].life == 0) {
            pods[i].life = 400;
            pods[i].x = TO_FP(x);
            pods[i].y = TO_FP(y);
            polar_to_rect(4, rng_u8(), &pods[i].vx, &pods[i].vy);
            return;
        }
    }
}

static bool spawn_pirate(uint8_t type)
{
    for (uint8_t i = 0; i < MAX_PIRATES; i++) {
        if (pirates[i].type == PR_FREE) {
            pirate_t *p = &pirates[i];
            p->type = type;
            p->hp = (type == PR_RAID) ? 2 : (type == PR_GUN ? 5 : 1);
            uint8_t edge = rng_u8() & 3u;
            int16_t sx, sy;
            if (edge == 0) { sx = 16 + (int16_t)rng_u8(); sy = PF_TOP + 8; }
            else if (edge == 1) { sx = 16 + (int16_t)rng_u8(); sy = PF_BOT - 8; }
            else if (edge == 2) { sx = 10; sy = PF_TOP + 12 + (int16_t)(rng_u8() % 180u); }
            else { sx = SCREEN_W - 10; sy = PF_TOP + 12 + (int16_t)(rng_u8() % 180u); }
            p->x = TO_FP(sx);
            p->y = TO_FP(sy);
            p->ang = atan2_u8(ARK_CY - sy, ARK_CX - sx);
            p->orbit = (type == PR_GUN) ? (uint8_t)((rng_u8() & 0xE0u) + 16u) : p->ang;
            p->timer = (type == PR_GUN) ? 60 : (uint8_t)(40 + (rng_u8() & 63u));
            p->vx = p->vy = 0;
            return true;
        }
    }
    return false;
}

static void kill_pirate(uint8_t idx)
{
    pirate_t *p = &pirates[idx];
    int16_t px = FROM_FP(p->x), py = FROM_FP(p->y);
    uint8_t t = p->type;
    p->type = PR_FREE;
    spawn_boom(px, py);
    sfx_enemy(SFX_BOOM, SFX_PRI_HIT);

    if (t == PR_RAID || t == PR_GUN) {
        g_voy.kills++;
        g_voy.credits += (t == PR_RAID) ? 10u : 25u;
        if ((rng_u8() & 3u) == 0)
            spawn_pod(px, py);
    }
}

static void fighter_hurt(uint8_t dmg)
{
    if (ft_st != FT_OUT) return;
    if (ft_teth <= dmg) {
        ft_teth = 0;
        ft_st = FT_DEAD;
        ft_rebuild_tm = FT_REBUILD;
        spawn_boom(FROM_FP(ft_x), FROM_FP(ft_y));
        sfx_enemy(SFX_BOOM, SFX_PRI_TOP);
        msg_flash("FIGHTER LOST - REBUILDING", C_ORANGE, 90);
    } else {
        ft_teth -= dmg;
        sfx_enemy(SFX_ARK_HIT, SFX_PRI_HIT);
    }
}

/* ------------------------------------------------------------------ */
/* HUD                                                                */
/* ------------------------------------------------------------------ */

static void space_hud_draw(void)
{
    hud_top_clear();

    /* Row 0: Credits | Wave | Graphical 4bpp Energy Bar | Graphical 4bpp Fighter Tether Bar */
    hud_top_str(1, 0, "$", C_GOLD);
    hud_top_num(2, 0, g_voy.credits, 5, C_WHITE);
    hud_top_tile(8, 0, HT_DIV);

    hud_top_str(9, 0, "W", C_CYAN);
    hud_top_num(10, 0, s_wave, 2, C_WHITE);
    hud_top_tile(12, 0, HT_DIV);

    /* Ark Energy gauge (lightning icon + beveled 4bpp bar) */
    hud_top_tile(13, 0, HT_ICO_BOLT);
    hud_top_tile(14, 0, HT_BAR_L);
    hud_top_bar(15, 0, 10, g_voy.energy, voy_hull_max(), false);
    hud_top_tile(25, 0, HT_BAR_R);
    hud_top_tile(26, 0, HT_DIV);

    /* Fighter Tether gauge (mini saucer icon + beveled 4bpp cyan bar) */
    hud_top_tile(27, 0, HT_ICO_SAUCER);
    if (ft_st == FT_DEAD) {
        hud_top_str(29, 0, "REBUILDING", (g_frame & 8u) ? C_RED : C_ORANGE);
    } else {
        hud_top_tile(28, 0, HT_BAR_L);
        hud_top_bar(29, 0, 9, ft_teth, FT_TETH_MAX, true);
        hud_top_tile(38, 0, HT_BAR_R);
    }

    /* Row 1: Clean status strip over beveled cyan-rimmed cockpit bar */
    if (ft_st == FT_OUT) {
        hud_top_str(4, 1, "FIGHTER ACTIVE", C_CYAN);
        hud_top_str(24, 1, "[X] RECALL", C_GOLD);
    } else if (ft_st == FT_HOMING) {
        hud_top_str(10, 1, "FIGHTER RETURNING...", C_GOLD);
    } else if (s_pr_window > 0) {
        hud_top_str(3, 1, "PIRATES!", (g_frame & 8u) ? C_RED : C_GOLD);
        hud_top_str(18, 1, "[X] LAUNCH FIGHTER", C_CYAN);
    } else {
        hud_top_str(4, 1, "ARK DEFENSE", C_GREY);
        hud_top_str(18, 1, "[X] LAUNCH FIGHTER", C_GOLD);
    }
}

/* ------------------------------------------------------------------ */
/* Public Entry                                                       */
/* ------------------------------------------------------------------ */

void space_enter(uint8_t wave, bool quick_mode)
{
    memset(g_arena, 0, sizeof(g_arena));
    memset(pirates, 0, sizeof(pirates));
    memset(pods, 0, sizeof(pods));
    memset(booms, 0, sizeof(booms));

    s_wave = (wave < 1) ? 1 : (wave > 10 ? 10 : wave);
    s_quick = quick_mode;
    s_ark_hit = false;
    s_state = SS_ARRIVE;
    s_ark_y = -24;
    s_timer = 0;
    s_aim = 0;
    s_fire_cool = 0;

    uint8_t rc = (uint8_t)(3 + s_wave);
    if (rc > 12) rc = 12;
    s_rocks_left = rc;
    s_rock_tm = 35;

    uint8_t raiders = (uint8_t)(1 + s_wave);
    if (raiders > 6) raiders = 6;
    s_raiders_left = raiders;
    s_guns_left = (s_wave >= 6) ? 3 : (s_wave >= 4 ? 2 : (s_wave >= 2 ? 1 : 0));
    s_pirate_tm = 180;
    s_pr_window = 0;
    s_pr_started = false;
    s_final_spawned = false;
    s_pirate_music = false;

    ft_st = FT_DOCK;
    ft_teth = FT_TETH_MAX;
    ft_drain_tm = FT_DRAIN;
    ft_cool = 0;
    tu_cool = 0;
    ft_ang = 192;

    video_set_mode(VID_MODE_SPACE, 0);
    space_hud_draw();
    music_play("ROM:Space.vgm");
}

/* ------------------------------------------------------------------ */
/* Update Subsystems                                                  */
/* ------------------------------------------------------------------ */

static void update_ark_weapons(void)
{
    uint8_t j = in_joy8();
    if (j < 8) s_aim = j;
    if (s_fire_cool > 0) s_fire_cool--;

    if (ft_st == FT_DOCK || ft_st == FT_DEAD) {
        if ((in_held & IN_FIRE) && s_fire_cool == 0 && g_voy.energy > 0) {
            for (uint8_t i = 0; i < MAX_BOLTS; i++) {
                if (bolts[i].life == 0) {
                    bolts[i].life = BOLT_LIFE;
                    bolts[i].dir = s_aim;
                    bolts[i].dx = (int8_t)(dir_dx[s_aim] * BOLT_SPEED);
                    bolts[i].dy = (int8_t)(dir_dy[s_aim] * BOLT_SPEED);
                    bolts[i].x = ARK_CX + dir_dx[s_aim] * 18;
                    bolts[i].y = ARK_CY + dir_dy[s_aim] * 12;
                    s_fire_cool = FIRE_GAP;
                    if (g_voy.energy > SHOT_COST) g_voy.energy -= SHOT_COST;
                    else g_voy.energy = 0;
                    sfx_player(SFX_LASER, SFX_PRI_SHOT);
                    break;
                }
            }
        }
    } else {
        if (tu_cool > 0) {
            tu_cool--;
        } else if (g_voy.energy > SHOT_COST) {
            uint16_t best_d = 0xFFFFu;
            int16_t bx = 0, by = 0;
            for (uint8_t i = 0; i < MAX_PIRATES; i++) {
                if (pirates[i].type == PR_FREE) continue;
                int16_t dx = FROM_FP(pirates[i].x) - ARK_CX;
                int16_t dy = FROM_FP(pirates[i].y) - ARK_CY;
                uint16_t d = hypot_u16(dx, dy);
                if (d < best_d) { best_d = d; bx = dx; by = dy; }
            }
            for (uint8_t i = 0; i < MAX_ROCKS; i++) {
                if (rocks[i].type == RK_FREE) continue;
                int16_t dx = FROM_FP(rocks[i].x) - ARK_CX;
                int16_t dy = FROM_FP(rocks[i].y) - ARK_CY;
                uint16_t d = hypot_u16(dx, dy);
                if (d < best_d) { best_d = d; bx = dx; by = dy; }
            }
            if (best_d < 0xFFFFu) {
                uint8_t a = atan2_u8(by, bx);
                uint8_t d8 = (uint8_t)(((a + 16u) >> 5) & 7u);
                uint8_t qang = (uint8_t)(d8 << 5);
                int16_t vx, vy;
                polar_to_rect(TU_SPEED, qang, &vx, &vy);
                spawn_shot(SH_TURRET, ARK_CX, ARK_CY, vx, vy, 45);
                g_voy.energy -= SHOT_COST;
                tu_cool = TU_GAP;
                sfx_player(SFX_LASER, SFX_PRI_LOW);
            }
        }
    }
}

static void update_fighter(void)
{
    if (ft_st == FT_DEAD) {
        if (ft_rebuild_tm > 0 && --ft_rebuild_tm == 0) {
            ft_st = FT_DOCK;
            ft_teth = FT_TETH_MAX;
            sfx_player(SFX_DOCK, SFX_PRI_EVENT);
        }
        return;
    }

    if (ft_st == FT_DOCK) {
        if (ft_teth < FT_TETH_MAX) {
            if (--ft_drain_tm == 0) {
                ft_drain_tm = FT_CHARGE;
                ft_teth++;
            }
        }
        if ((in_pressed & IN_BTN2) && ft_teth >= FT_LAUNCH) {
            ft_st = FT_OUT;
            ft_x = TO_FP(BAY_X);
            ft_y = TO_FP(BAY_Y);
            ft_vx = 0;
            ft_vy = 16;
            ft_ang = 64;
            ft_drain_tm = FT_DRAIN;
            sfx_player(SFX_LAUNCH, SFX_PRI_EVENT);
        }
        return;
    }

    if (ft_st == FT_HOMING) {
        int16_t dx = BAY_X - FROM_FP(ft_x);
        int16_t dy = BAY_Y - FROM_FP(ft_y);
        if (hypot_u16(dx, dy) < 8) {
            ft_st = FT_DOCK;
            ft_drain_tm = FT_CHARGE;
            sfx_player(SFX_DOCK, SFX_PRI_EVENT);
            return;
        }
        uint8_t a = atan2_u8(dy, dx);
        polar_to_rect(FT_HOME_SPD, a, &ft_vx, &ft_vy);
        ft_x += ft_vx;
        ft_y += ft_vy;
        return;
    }

    /* FT_OUT */
    if ((in_pressed & IN_BTN2) || ft_teth == 0) {
        ft_st = FT_HOMING;
        sfx_player(SFX_LAUNCH, SFX_PRI_LOW);
        return;
    }

    if (--ft_drain_tm == 0) {
        ft_drain_tm = FT_DRAIN;
        if (ft_teth > 0) ft_teth--;
        if (ft_teth == 0) {
            ft_st = FT_HOMING;
            return;
        }
    }

    uint8_t j = in_joy8();
    int16_t tvx = 0, tvy = 0;
    if (j < 8) {
        ft_ang = dir_ang[j];
        polar_to_rect(FT_SPEED, ft_ang, &tvx, &tvy);
    }
    ft_vx += (tvx - ft_vx) >> 3;
    ft_vy += (tvy - ft_vy) >> 3;
    ft_x += ft_vx;
    ft_y += ft_vy;

    int16_t px = FROM_FP(ft_x), py = FROM_FP(ft_y);
    if (px < 0) ft_x = TO_FP(SCREEN_W - 1);
    else if (px >= SCREEN_W) ft_x = 0;
    if (py < PF_TOP) ft_y = TO_FP(PF_BOT - 1);
    else if (py >= PF_BOT) ft_y = TO_FP(PF_TOP);

    if (ft_cool > 0) ft_cool--;
    if ((in_held & IN_FIRE) && ft_cool == 0) {
        int16_t svx, svy;
        polar_to_rect(FT_SHOT_SPD, ft_ang, &svx, &svy);
        int16_t ox = -(sin256(ft_ang) * 4) >> 8;
        int16_t oy =  (cos256(ft_ang) * 4) >> 8;
        int16_t fx = FROM_FP(ft_x), fy = FROM_FP(ft_y);
        spawn_shot(SH_FIGHTER, fx + ox, fy + oy, svx + (ft_vx >> 1), svy + (ft_vy >> 1), FT_SHLIFE);
        spawn_shot(SH_FIGHTER, fx - ox, fy - oy, svx + (ft_vx >> 1), svy + (ft_vy >> 1), FT_SHLIFE);
        ft_cool = FT_GAP;
        sfx_player(SFX_BLASTER, SFX_PRI_SHOT);
    }
}

static void update_pirates(void)
{
    uint8_t active_missiles = 0;
    for (uint8_t i = 0; i < MAX_PIRATES; i++)
        if (pirates[i].type == PR_MISS) active_missiles++;

    for (uint8_t i = 0; i < MAX_PIRATES; i++) {
        pirate_t *p = &pirates[i];
        if (p->type == PR_FREE) continue;

        int16_t px = FROM_FP(p->x), py = FROM_FP(p->y);
        int16_t dx_ark = ARK_CX - px;
        int16_t dy_ark = ARK_CY - py;

        uint8_t want = p->ang;
        uint8_t turn = 4;
        int16_t spd = (p->type == PR_MISS) ? 20 : 22;

        if (p->type == PR_MISS) {
            want = atan2_u8(dy_ark, dx_ark);
            if (++p->timer > 220) { p->type = PR_FREE; continue; }
        } else if (p->type == PR_RAID) {
            p->orbit += 1;
            int16_t tx = ARK_CX + ((cos256(p->orbit) * R_RAID) >> 8);
            int16_t ty = ARK_CY + ((sin256(p->orbit) * R_RAID) >> 8);
            want = atan2_u8(ty - py, tx - px);
            if (p->timer > 0) {
                p->timer--;
            } else {
                int16_t svx, svy;
                uint8_t aim = atan2_u8(dy_ark, dx_ark);
                polar_to_rect(36, aim, &svx, &svy);
                spawn_shot(SH_PIRATE, px, py, svx, svy, 85);
                sfx_enemy(SFX_PSHOT, SFX_PRI_LOW);
                p->timer = (uint8_t)(70 + (rng_u8() & 63u));
            }
        } else if (p->type == PR_GUN) {
            int16_t tx = ARK_CX + ((cos256(p->orbit) * R_GUN) >> 8);
            int16_t ty = ARK_CY + ((sin256(p->orbit) * R_GUN) >> 8);
            if (hypot_u16(tx - px, ty - py) > 8) {
                want = atan2_u8(ty - py, tx - px);
            } else {
                spd = 0;
                if (p->timer > 0) {
                    p->timer--;
                } else if (active_missiles < 3) {
                    for (uint8_t m = 0; m < MAX_PIRATES; m++) {
                        if (pirates[m].type == PR_FREE) {
                            pirates[m].type = PR_MISS;
                            pirates[m].hp = 1;
                            pirates[m].x = p->x;
                            pirates[m].y = p->y;
                            pirates[m].ang = atan2_u8(dy_ark, dx_ark);
                            pirates[m].timer = 0;
                            active_missiles++;
                            sfx_enemy(SFX_PSHOT, SFX_PRI_SHOT);
                            break;
                        }
                    }
                    p->timer = 100;
                }
            }
        }

        /* Rock avoidance (16-frame lookahead) */
        if (p->type != PR_MISS) {
            for (uint8_t r = 0; r < MAX_ROCKS; r++) {
                if (rocks[r].type == RK_FREE) continue;
                int16_t rx16 = FROM_FP(rocks[r].x + (rocks[r].vx << 4));
                int16_t ry16 = FROM_FP(rocks[r].y + (rocks[r].vy << 4));
                int16_t px16 = FROM_FP(p->x + (p->vx << 4));
                int16_t py16 = FROM_FP(p->y + (p->vy << 4));
                if (hypot_u16(px16 - rx16, py16 - ry16) < R_AVOID) {
                    want = atan2_u8(py16 - ry16, px16 - rx16);
                    turn = 8;
                    spd = 26;
                    break;
                }
            }
        }

        p->ang = turn_toward(p->ang, want, turn);
        polar_to_rect(spd, p->ang, &p->vx, &p->vy);
        p->x += p->vx;
        p->y += p->vy;

        px = FROM_FP(p->x); py = FROM_FP(p->y);
        if (px < 10) { p->x = TO_FP(10); px = 10; }
        if (px > SCREEN_W - 10) { p->x = TO_FP(SCREEN_W - 10); px = SCREEN_W - 10; }
        if (py < PF_TOP + 8) { p->y = TO_FP(PF_TOP + 8); py = PF_TOP + 8; }
        if (py > PF_BOT - 8) { p->y = TO_FP(PF_BOT - 8); py = PF_BOT - 8; }

        if (p->type == PR_MISS && px >= ARK_HX0 && px <= ARK_HX1 && py >= ARK_HY0 && py <= ARK_HY1) {
            p->type = PR_FREE;
            spawn_boom(px, py);
            ark_damage(MISS_DMG);
            continue;
        }

        if (ft_st == FT_OUT) {
            int16_t fx = FROM_FP(ft_x), fy = FROM_FP(ft_y);
            if (hypot_u16(px - fx, py - fy) < 12) {
                if (p->type == PR_MISS) {
                    p->type = PR_FREE;
                    spawn_boom(px, py);
                    fighter_hurt(MISS_FDMG);
                } else {
                    fighter_hurt(RAM_FDMG);
                    if (p->hp <= 2) kill_pirate(i);
                    else p->hp -= 2;
                }
            }
        }
    }
}

static void update_rocks_and_projectiles(void)
{
    for (uint8_t i = 0; i < MAX_BOLTS; i++) {
        bolt_t *b = &bolts[i];
        if (b->life == 0) continue;
        b->x += b->dx;
        b->y += b->dy;
        b->life--;
        if (b->x < 0 || b->x >= SCREEN_W || b->y < PF_TOP || b->y >= PF_BOT) {
            b->life = 0;
            continue;
        }
        bool hit = false;
        for (uint8_t r = 0; r < MAX_ROCKS; r++) {
            if (rocks[r].type == RK_FREE) continue;
            int16_t rx = FROM_FP(rocks[r].x), ry = FROM_FP(rocks[r].y);
            int16_t rad = (int16_t)rock_rad[rocks[r].type] + 3;
            if (b->x >= rx - rad && b->x <= rx + rad && b->y >= ry - rad && b->y <= ry + rad) {
                b->life = 0;
                hit_rock(r, 2, true);
                hit = true;
                break;
            }
        }
        if (hit) continue;
        for (uint8_t p = 0; p < MAX_PIRATES; p++) {
            if (pirates[p].type == PR_FREE) continue;
            int16_t px = FROM_FP(pirates[p].x), py = FROM_FP(pirates[p].y);
            if (b->x >= px - 9 && b->x <= px + 9 && b->y >= py - 9 && b->y <= py + 9) {
                b->life = 0;
                if (pirates[p].hp <= 2) {
                    kill_pirate(p);
                    energy_pay(KILL_PAY);
                } else {
                    pirates[p].hp -= 2;
                    sfx_enemy(SFX_ROCK_HIT, SFX_PRI_SHOT);
                }
                break;
            }
        }
    }

    for (uint8_t i = 0; i < MAX_SHOTS; i++) {
        shot_t *s = &shots[i];
        if (s->type == SH_FREE) continue;
        s->x += s->vx;
        s->y += s->vy;
        s->life--;
        int16_t sx = FROM_FP(s->x), sy = FROM_FP(s->y);
        if (s->life == 0 || sx < 0 || sx >= SCREEN_W || sy < PF_TOP || sy >= PF_BOT) {
            s->type = SH_FREE;
            continue;
        }
        if (s->type == SH_PIRATE) {
            if (sx >= ARK_HX0 && sx <= ARK_HX1 && sy >= ARK_HY0 && sy <= ARK_HY1) {
                s->type = SH_FREE;
                spawn_boom(sx, sy);
                ark_damage(PSHOT_DMG);
                continue;
            }
            if (ft_st == FT_OUT && hypot_u16(sx - FROM_FP(ft_x), sy - FROM_FP(ft_y)) < 10) {
                s->type = SH_FREE;
                fighter_hurt(PSHOT_FDMG);
                continue;
            }
        } else {
            uint8_t dmg = (s->type == SH_TURRET) ? 2 : 1;
            bool pays = (s->type == SH_TURRET);
            bool hit = false;
            for (uint8_t r = 0; r < MAX_ROCKS; r++) {
                if (rocks[r].type == RK_FREE) continue;
                int16_t rx = FROM_FP(rocks[r].x), ry = FROM_FP(rocks[r].y);
                int16_t rad = (int16_t)rock_rad[rocks[r].type] + 2;
                if (sx >= rx - rad && sx <= rx + rad && sy >= ry - rad && sy <= ry + rad) {
                    s->type = SH_FREE;
                    hit_rock(r, dmg, pays);
                    hit = true;
                    break;
                }
            }
            if (hit) continue;
            for (uint8_t p = 0; p < MAX_PIRATES; p++) {
                if (pirates[p].type == PR_FREE) continue;
                int16_t px = FROM_FP(pirates[p].x), py = FROM_FP(pirates[p].y);
                if (sx >= px - 9 && sx <= px + 9 && sy >= py - 9 && sy <= py + 9) {
                    s->type = SH_FREE;
                    if (pirates[p].hp <= dmg) {
                        kill_pirate(p);
                        if (pays) energy_pay(KILL_PAY);
                    } else {
                        pirates[p].hp -= dmg;
                        sfx_enemy(SFX_ROCK_HIT, SFX_PRI_SHOT);
                    }
                    break;
                }
            }
        }
    }

    for (uint8_t i = 0; i < MAX_ROCKS; i++) {
        rock_t *r = &rocks[i];
        if (r->type == RK_FREE) continue;
        r->x += r->vx;
        r->y += r->vy;
        r->anim++;
        int16_t rx = FROM_FP(r->x), ry = FROM_FP(r->y);

        if (rx >= ARK_HX0 && rx <= ARK_HX1 && ry >= ARK_HY0 && ry <= ARK_HY1) {
            uint8_t dmg = rock_dmg[r->type];
            r->type = RK_FREE;
            spawn_boom(rx, ry);
            ark_damage(dmg);
            continue;
        }

        if (ft_st == FT_OUT && hypot_u16(rx - FROM_FP(ft_x), ry - FROM_FP(ft_y)) < (uint16_t)(rock_rad[r->type] + 6)) {
            hit_rock(i, 2, false);
            fighter_hurt(FT_TETH_MAX);
            continue;
        }

        for (uint8_t p = 0; p < MAX_PIRATES; p++) {
            if (pirates[p].type == PR_FREE) continue;
            int16_t px = FROM_FP(pirates[p].x), py = FROM_FP(pirates[p].y);
            if (hypot_u16(rx - px, ry - py) < (uint16_t)(rock_rad[r->type] + 7)) {
                hit_rock(i, 2, false);
                if (pirates[p].hp <= 1) kill_pirate(p);
                else pirates[p].hp--;
                break;
            }
        }
    }

    for (uint8_t i = 0; i < MAX_PODS; i++) {
        if (pods[i].life == 0) continue;
        pods[i].life--;
        pods[i].x += pods[i].vx;
        pods[i].y += pods[i].vy;
        int16_t px = FROM_FP(pods[i].x), py = FROM_FP(pods[i].y);
        if (px < 4 || px > SCREEN_W - 4 || py < PF_TOP + 4 || py > PF_BOT - 4) {
            pods[i].life = 0;
            continue;
        }
        if (ft_st == FT_OUT && hypot_u16(px - FROM_FP(ft_x), py - FROM_FP(ft_y)) < 14) {
            pods[i].life = 0;
            energy_pay(POD_PAY);
            sfx_player(SFX_PICKUP, SFX_PRI_EVENT);
            msg_flash("+16 ENERGY SALVAGE", C_GREEN, 50);
        }
    }

    for (uint8_t i = 0; i < MAX_BOOMS; i++) {
        if (booms[i].timer > 0) booms[i].timer--;
    }
}

static void update_spawner(void)
{
    uint8_t live_rocks = 0, live_pirates = 0;
    for (uint8_t i = 0; i < MAX_ROCKS; i++) if (rocks[i].type != RK_FREE) live_rocks++;
    for (uint8_t i = 0; i < MAX_PIRATES; i++) if (pirates[i].type != PR_FREE) live_pirates++;

    if (s_raiders_left > 0 || s_guns_left > 0) {
        if (s_pirate_tm > 0) {
            s_pirate_tm--;
        } else {
            uint8_t want = (s_guns_left > 0 && (s_raiders_left == 0 || (rng_u8() & 1u))) ? PR_GUN : PR_RAID;
            if (spawn_pirate(want)) {
                live_pirates++;
                if (want == PR_GUN) s_guns_left--;
                else s_raiders_left--;
                s_pirate_tm = 120;
                if (!s_pr_started) {
                    s_pr_started = true;
                    s_pr_window = 1200;
                    msg_flash("PIRATES INCOMING!", C_RED, 90);
                    music_play("ROM:Pirates.vgm");
                    s_pirate_music = true;
                }
            }
        }
    }

    if (s_pr_window > 0) {
        s_pr_window--;
        if (live_pirates == 0 && s_raiders_left == 0 && s_guns_left == 0)
            s_pr_window = 0;
        if (s_pr_window == 0) {
            if (s_pirate_music) {
                music_play("ROM:Space.vgm");
                s_pirate_music = false;
            }
            if (!s_final_spawned) {
                s_final_spawned = true;
                uint8_t extra = (s_wave > 2) ? s_wave : 2;
                s_rocks_left += extra;
            }
        }
    }

    if (s_rocks_left > 0) {
        if (s_rock_tm > 0) {
            s_rock_tm--;
        } else {
            uint8_t ln = rng_u8() & 7u;
            uint8_t rt = (s_pr_window > 0) ? RK_S : RK_L;
            if (spawn_rock(rt, ln, TO_FP(lane_sx[ln]), TO_FP(lane_sy[ln]), rock_base_speed())) {
                live_rocks++;
                s_rocks_left--;
                uint8_t gap = (s_wave >= 7) ? 40 : (uint8_t)(110 - s_wave * 10);
                s_rock_tm = gap;
            }
        }
    }

    if (s_rocks_left == 0 && live_rocks == 0 &&
        s_raiders_left == 0 && s_guns_left == 0 && live_pirates == 0) {
        if (ft_st == FT_OUT) {
            ft_st = FT_HOMING;
            return;
        }
        if (ft_st == FT_HOMING)
            return;
        s_state = SS_CLEAR;
        s_timer = 75;
        if (!s_ark_hit) ach_set(ACH_UNTOUCHED);
        if (s_wave >= 10) ach_set(ACH_DEEPRAID);
        sfx_player(SFX_FANFARE, SFX_PRI_EVENT);
        msg_center(1, "WAVE CLEAR!", C_GREEN);
    }
}

/* ------------------------------------------------------------------ */
/* Render Sprites                                                     */
/* ------------------------------------------------------------------ */

/* Map 8-way binary angle (0=R, 32=DR, 64=D, 96=DL, 128=L, 160=UL, 192=U, 224=UR)
 * to 3 base sprite frames (0=R, 1=U, 2=UR) + Mode 5 HFLIP/VFLIP */
static void angle_to_frame_opt(uint8_t ang, uint8_t *frame, uint8_t *opt)
{
    uint8_t d = (uint8_t)(((ang + 16u) >> 5) & 7u);
    static const uint8_t f_tbl[8] = { 0, 2, 1, 2, 0, 2, 1, 2 };
    static const uint8_t o_tbl[8] = {
        MODE5_4BPP,                             /* 0: R  */
        MODE5_4BPP | MODE5_VFLIP,               /* 1: DR */
        MODE5_4BPP | MODE5_VFLIP,               /* 2: D  */
        MODE5_4BPP | MODE5_HFLIP | MODE5_VFLIP, /* 3: DL */
        MODE5_4BPP | MODE5_HFLIP,               /* 4: L  */
        MODE5_4BPP | MODE5_HFLIP,               /* 5: UL */
        MODE5_4BPP,                             /* 6: U  */
        MODE5_4BPP,                             /* 7: UR */
    };
    *frame = f_tbl[d];
    *opt = o_tbl[d];
}

static void space_render(void)
{
    uint8_t idx = 0;
    spr_hide_all();

    if (s_state != SS_LOST) {
        uint16_t ark_ptr = (uint16_t)(XRAM_IMG_ARK + (((g_frame >> 3) & 1u) * ARK_FRAME_BYTES));
        spr_place(idx++, ark_ptr, PAL_ARK, MODE5_SIZE(ARK_W, ARK_H), MODE5_4BPP,
                  ARK_CX - (int16_t)(ARK_W / 2), s_ark_y);
    }

    if (s_state == SS_PLAY && (ft_st == FT_DOCK || ft_st == FT_DEAD)) {
        int16_t ax = ARK_CX + dir_dx[s_aim] * 38 - 4;
        int16_t ay = ARK_CY + dir_dy[s_aim] * 26 - 4;
        spr_place(idx++, (uint16_t)(XRAM_IMG_SHOTS + 5u * SHOTS_FRAME_BYTES), PAL_ROCK,
                  MODE5_SIZE(8, 8), MODE5_4BPP, ax, ay);
    }

    if (ft_st == FT_OUT || ft_st == FT_HOMING) {
        uint16_t sp = (uint16_t)(XRAM_IMG_SAUCER + (((g_frame >> 2) & 3u) * SAUCER_FRAME_BYTES));
        spr_place(idx++, sp, PAL_SAUCER, MODE5_SIZE(SAUCER_W, SAUCER_H), MODE5_4BPP,
                  FROM_FP(ft_x) - (int16_t)(SAUCER_W / 2), FROM_FP(ft_y) - (int16_t)(SAUCER_H / 2));
    }

    /* Jagged tumbling 4-frame asteroids */
    for (uint8_t i = 0; i < MAX_ROCKS && idx < SPR_TOTAL_COUNT; i++) {
        rock_t *r = &rocks[i];
        if (r->type == RK_FREE) continue;
        uint8_t f = (r->anim >> 3) & 3u;
        int16_t rx = FROM_FP(r->x), ry = FROM_FP(r->y);
        if (r->type == RK_L)
            spr_place(idx++, (uint16_t)(XRAM_IMG_ROCK_L + f * ROCK_L_FRAME_BYTES), PAL_ROCK,
                      MODE5_SIZE(ROCK_L_W, ROCK_L_H), MODE5_4BPP, rx - 12, ry - 12);
        else if (r->type == RK_M)
            spr_place(idx++, (uint16_t)(XRAM_IMG_ROCK_M + f * ROCK_M_FRAME_BYTES), PAL_ROCK,
                      MODE5_SIZE(ROCK_M_W, ROCK_M_H), MODE5_4BPP, rx - 8, ry - 8);
        else
            spr_place(idx++, (uint16_t)(XRAM_IMG_ROCK_S + f * ROCK_S_FRAME_BYTES), PAL_ROCK,
                      MODE5_SIZE(ROCK_S_W, ROCK_S_H), MODE5_4BPP, rx - 6, ry - 6);
    }

    /* Directional 8-way Raiders, heavy Gunships, and directional 8-way Missiles */
    for (uint8_t i = 0; i < MAX_PIRATES && idx < SPR_TOTAL_COUNT; i++) {
        pirate_t *p = &pirates[i];
        if (p->type == PR_FREE) continue;
        int16_t px = FROM_FP(p->x), py = FROM_FP(p->y);
        if (p->type == PR_RAID) {
            uint8_t f, opt;
            angle_to_frame_opt(p->ang, &f, &opt);
            spr_place(idx++, (uint16_t)(XRAM_IMG_RAIDER + f * RAIDER_FRAME_BYTES), PAL_PIRATE,
                      MODE5_SIZE(RAIDER_W, RAIDER_H), opt, px - 8, py - 8);
        } else if (p->type == PR_GUN) {
            uint8_t f = (g_frame >> 3) & 1u;
            spr_place(idx++, (uint16_t)(XRAM_IMG_GUNSHIP + f * GUNSHIP_FRAME_BYTES), PAL_PIRATE,
                      MODE5_SIZE(GUNSHIP_W, GUNSHIP_H), MODE5_4BPP, px - 10, py - 10);
        } else {
            uint8_t f, opt;
            angle_to_frame_opt(p->ang, &f, &opt);
            spr_place(idx++, (uint16_t)(XRAM_IMG_MISSILE + f * MISSILE_FRAME_BYTES), PAL_PIRATE,
                      MODE5_SIZE(MISSILE_W, MISSILE_H), opt, px - 6, py - 6);
        }
    }

    /* Pulsing Salvage Pods */
    for (uint8_t i = 0; i < MAX_PODS && idx < SPR_TOTAL_COUNT; i++) {
        if (pods[i].life == 0) continue;
        uint8_t f = (g_frame >> 3) & 1u;
        spr_place(idx++, (uint16_t)(XRAM_IMG_POD + f * POD_FRAME_BYTES), PAL_PIRATE,
                  MODE5_SIZE(POD_W, POD_H), MODE5_4BPP, FROM_FP(pods[i].x) - 6, FROM_FP(pods[i].y) - 6);
    }

    for (uint8_t i = 0; i < MAX_BOLTS && idx < SPR_TOTAL_COUNT; i++) {
        if (bolts[i].life == 0) continue;
        uint8_t f = bolts[i].dir & 3u;
        spr_place(idx++, (uint16_t)(XRAM_IMG_SHOTS + f * SHOTS_FRAME_BYTES), PAL_ROCK,
                  MODE5_SIZE(8, 8), MODE5_4BPP, bolts[i].x - 4, bolts[i].y - 4);
    }

    for (uint8_t i = 0; i < MAX_SHOTS && idx < SPR_TOTAL_COUNT; i++) {
        if (shots[i].type == SH_FREE) continue;
        uint8_t pal = (shots[i].type == SH_PIRATE) ? PAL_PIRATE : PAL_SAUCER;
        spr_place(idx++, (uint16_t)(XRAM_IMG_SHOTS + 4u * SHOTS_FRAME_BYTES), pal,
                  MODE5_SIZE(8, 8), MODE5_4BPP, FROM_FP(shots[i].x) - 4, FROM_FP(shots[i].y) - 4);
    }

    for (uint8_t i = 0; i < MAX_BOOMS && idx < SPR_TOTAL_COUNT; i++) {
        if (booms[i].timer == 0) continue;
        uint8_t f = (uint8_t)((16u - booms[i].timer) >> 2);
        if (f > 3) f = 3;
        spr_place(idx++, (uint16_t)(XRAM_IMG_BOOM + f * BOOM_FRAME_BYTES), PAL_ROCK,
                  MODE5_SIZE(16, 16), MODE5_4BPP, booms[i].x - 8, booms[i].y - 8);
    }
}

/* ------------------------------------------------------------------ */
/* Main Space Frame                                                   */
/* ------------------------------------------------------------------ */

space_result_t space_update(void)
{
    video_twinkle_stars(g_frame);

    switch (s_state) {
    case SS_ARRIVE:
        s_ark_y += 2;
        if (s_ark_y >= (int16_t)(ARK_CY - ARK_H / 2)) {
            s_ark_y = (int16_t)(ARK_CY - ARK_H / 2);
            s_state = SS_START;
            s_timer = 55;
            char buf[16] = "WAVE 00";
            buf[5] = (char)('0' + s_wave / 10);
            buf[6] = (char)('0' + s_wave % 10);
            msg_center(1, buf, C_GOLD);
        }
        break;

    case SS_START:
        if (--s_timer == 0) {
            msg_clear();
            s_state = SS_PLAY;
        }
        break;

    case SS_PLAY:
        update_ark_weapons();
        update_fighter();
        update_pirates();
        update_rocks_and_projectiles();
        update_spawner();
        break;

    case SS_CLEAR:
        /* Let final explosions finish animating cleanly */
        for (uint8_t i = 0; i < MAX_BOOMS; i++)
            if (booms[i].timer > 0) booms[i].timer--;
        memset(bolts, 0, sizeof(bolt_t) * MAX_BOLTS);
        memset(shots, 0, sizeof(shot_t) * MAX_SHOTS);
        memset(pods, 0, sizeof(pods));
        if (--s_timer == 0) {
            msg_clear();
            memset(rocks, 0, sizeof(rock_t) * MAX_ROCKS);
            memset(pirates, 0, sizeof(pirates));
            memset(booms, 0, sizeof(booms));
            s_state = SS_DEPART;
        }
        break;

    case SS_DEPART:
        for (uint8_t i = 0; i < MAX_BOOMS; i++)
            if (booms[i].timer > 0) booms[i].timer--;
        s_ark_y -= 2;
        if (s_ark_y < -(int16_t)ARK_H) {
            if (s_quick) {
                space_enter((uint8_t)(s_wave + 1), true);
                return SPACE_RUNNING;
            }
            return SPACE_CLEARED;
        }
        break;

    case SS_LOST:
        for (uint8_t i = 0; i < MAX_BOOMS; i++)
            if (booms[i].timer > 0) booms[i].timer--;
        if (s_timer > 0 && --s_timer == 0)
            return SPACE_ARK_LOST;
        break;
    }

    if ((g_frame & 3u) == 0)
        space_hud_draw();
    space_render();
    return SPACE_RUNNING;
}

/* ------------------------------------------------------------------ */
/* Escape Pod Sequence                                                */
/* ------------------------------------------------------------------ */

static uint16_t esc_timer;
static int16_t  esc_x, esc_y;

void escape_enter(void)
{
    esc_timer = 0;
    esc_x = ARK_CX;
    esc_y = ARK_CY;
    video_set_mode(VID_MODE_SPACE, 0);
    hud_top_clear();
    hud_top_str(10, 0, "EMERGENCY ESCAPE POD", C_RED);
    music_stop();
    sfx_enemy(SFX_BOOM, SFX_PRI_TOP);
    ach_set(ACH_LIFEBOAT);
}

bool escape_update(void)
{
    video_twinkle_stars(g_frame);
    esc_timer++;

    spr_hide_all();
    if (esc_timer < 40) {
        uint8_t f = (uint8_t)((esc_timer / 10u) & 3u);
        spr_place(0, (uint16_t)(XRAM_IMG_BOOM + f * BOOM_FRAME_BYTES), PAL_ROCK,
                  MODE5_SIZE(16, 16), MODE5_4BPP, ARK_CX - 18, ARK_CY - 8);
        spr_place(1, (uint16_t)(XRAM_IMG_BOOM + ((f + 1) & 3u) * BOOM_FRAME_BYTES), PAL_ROCK,
                  MODE5_SIZE(16, 16), MODE5_4BPP, ARK_CX + 4, ARK_CY - 4);
    }

    if (esc_timer > 15 && esc_x < SCREEN_W + 16) {
        esc_x += 2;
        if ((esc_timer & 1u) == 0) esc_y -= 1;
        uint16_t sp = (uint16_t)(XRAM_IMG_SAUCER + (((esc_timer >> 2) & 3u) * SAUCER_FRAME_BYTES));
        spr_place(2, sp, PAL_SAUCER, MODE5_SIZE(SAUCER_W, SAUCER_H), MODE5_4BPP,
                  esc_x - (int16_t)(SAUCER_W / 2), esc_y - (int16_t)(SAUCER_H / 2));
    }

    if (esc_timer == 60) {
        msg_center(0, "THE ARK WAS LOST!", C_RED);
        msg_center(1, "ESCAPE POD REACHED SECTOR 1", C_GOLD);
        msg_center(3, "PRESS FIRE FOR A NEW ARK", C_WHITE);
    }

    if (esc_timer > 60 && (in_pressed & (IN_FIRE | IN_START))) {
        g_voy.sector = 0;
        g_voy.u_hull = 0;
        g_voy.u_holds = 0;
        g_voy.energy = EN_BASE;
        g_voy.credits /= 10u;
        g_voy.ore = g_voy.org = g_voy.eqp = 0;
        memset(g_voy.stasis, 0, sizeof(g_voy.stasis));
        memset(g_voy.keep, 0, sizeof(g_voy.keep));
        if (g_voy.relic_state == 1) g_voy.relic_state = 0;
        return true;
    }
    return false;
}
