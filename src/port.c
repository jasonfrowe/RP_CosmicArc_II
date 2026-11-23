#include <string.h>
#include "galaxy.h"
#include "game.h"
#include "input.h"
#include "music.h"
#include "port.h"
#include "save.h"
#include "sfx.h"
#include "video.h"

static uint8_t pt_tab = 0;   /* 0=Commodities, 1=Bio-Bounty, 2=Shipyard, 3=Bank */
static uint8_t pt_ntabs = 3;
static uint8_t pt_sel = 0;
static uint8_t pt_pidx = 0;
static uint8_t pt_tier = 0;
static uint8_t pt_cls = 0;
static char    pt_msg[38];

static const char *const COMM_NAMES[3] = { "FUEL ORE", "ORGANICS", "EQUIPMENT" };
static const uint16_t    COMM_BASE[3]  = { 20, 30, 50 };
static const uint8_t     TIER_BUY_K[4] = { 75, 84, 94, 103 };
static const uint8_t     EN_PRICE[4]   = { 4, 5, 6, 8 };

static uint8_t *comm_qty_ptr(uint8_t c)
{
    if (c == 0) return &g_voy.ore;
    if (c == 1) return &g_voy.org;
    return &g_voy.eqp;
}

static uint16_t comm_price(uint8_t c)
{
    uint8_t st = g_voy.port_stock[pt_pidx][c];
    uint16_t k;
    if (pt_cls & (1u << c)) {
        /* Port sells (maker): K = 60 - stock */
        k = (st < 60) ? (uint16_t)(60u - st) : 1u;
    } else {
        /* Port buys (taker): K = TIER_BUY_K[tier] - bought */
        uint8_t base_k = TIER_BUY_K[pt_tier];
        k = (base_k > st) ? (uint16_t)(base_k - st) : 15u;
    }
    return (uint16_t)((COMM_BASE[c] * k) / 60u);
}

static uint16_t bio_bounty(uint8_t sp)
{
    uint16_t base = (uint16_t)(((30u + 8u * sp) * (2u + pt_tier)) / 2u);
    uint8_t glut = g_voy.port_glut[pt_pidx];
    if (glut > 4) glut = 4;
    return (uint16_t)(base - (base * glut) / 8u);
}

static uint8_t tab_lines(void)
{
    if (pt_tab == 0) return 4; /* 3 commodities + Leave */
    if (pt_tab == 1) return 7; /* 6 species + Leave */
    if (pt_tab == 2) return 4; /* Energy, Hull, Holds + Leave */
    return 5;                  /* Dep 100, Dep All, Wdr 100, Wdr All + Leave */
}

static void set_msg(const char *m)
{
    strncpy(pt_msg, m, sizeof(pt_msg) - 1);
    pt_msg[sizeof(pt_msg) - 1] = '\0';
}

static void port_draw(void)
{
    txt_clear(C_TRANS);
    txt_box(0, 0, 40, 4, C_CYAN, C_PANEL);
    txt_str(2, 1, "STARPORT - SECTOR ", C_GOLD, C_PANEL);
    txt_num(20, 1, (uint32_t)(g_voy.sector + 1), 2, C_WHITE, C_PANEL);
    txt_str(25, 1, "CR:$", C_GOLD, C_PANEL);
    txt_num(29, 1, g_voy.credits, 6, C_WHITE, C_PANEL);

    /* Tabs */
    static const char *const tnames[4] = { "TRADE", "BOUNTY", "YARD", "BANK" };
    for (uint8_t i = 0; i < pt_ntabs; i++) {
        uint8_t tx = (uint8_t)(2 + i * 9);
        bool act = (i == pt_tab);
        txt_str(tx, 2, act ? "[" : " ", C_GOLD, C_PANEL);
        txt_str(tx + 1, 2, tnames[i], act ? C_WHITE : C_DIM, act ? C_FRAME : C_PANEL);
        txt_str(tx + 1 + (uint8_t)strlen(tnames[i]), 2, act ? "]" : " ", C_GOLD, C_PANEL);
    }

    txt_box(0, 5, 40, 16, C_FRAME, C_PANEL);

    if (pt_tab == 0) {
        txt_str(2, 6, "GOOD       ACT  PRICE  STK  YOURS", C_CYAN, C_PANEL);
        for (uint8_t c = 0; c < 3; c++) {
            uint8_t r = (uint8_t)(8 + c * 2);
            bool sel = (pt_sel == c);
            uint8_t bg = sel ? C_FRAME : C_PANEL;
            bool sells = (pt_cls & (1u << c)) != 0;
            uint8_t st = g_voy.port_stock[pt_pidx][c];
            uint8_t avail = sells ? st : (uint8_t)(PT_FULL - st);
            uint16_t pr = comm_price(c);

            if (sel) for (uint8_t x = 1; x < 39; x++) txt_put(x, r, ' ', C_WHITE, bg);
            txt_str(1, r, sel ? ">" : " ", C_GOLD, bg);
            txt_str(2, r, COMM_NAMES[c], C_WHITE, bg);
            txt_str(13, r, sells ? "BUY " : "SELL", sells ? C_GREEN : C_GOLD, bg);
            txt_str(18, r, "$", C_GOLD, bg);
            txt_num(19, r, pr, 3, C_WHITE, bg);
            txt_num(25, r, avail, 2, avail ? C_CYAN : C_RED, bg);
            txt_num(31, r, *comm_qty_ptr(c), 2, C_WHITE, bg);
        }
        /* Relic rumour */
        if (g_voy.relic_state == 0) {
            uint8_t hint = (g_voy.relic_sec > 0) ? g_voy.relic_sec : 60;
            txt_str(2, 16, "RUMOUR: RELIC NEAR SECTOR ", C_MAGENTA, C_PANEL);
            txt_num(28, 16, hint, 2, C_WHITE, C_PANEL);
        } else if (g_voy.relic_state == 2) {
            txt_str(2, 16, "THE OUTER-RIM RELIC IS SAFE!", C_GREEN, C_PANEL);
        }
    } else if (pt_tab == 1) {
        txt_str(2, 6, "SPECIES   STASIS  KEPT  BOUNTY", C_CYAN, C_PANEL);
        for (uint8_t i = 0; i < NUM_SPECIES; i++) {
            uint8_t r = (uint8_t)(8 + i);
            bool sel = (pt_sel == i);
            uint8_t bg = sel ? C_FRAME : C_PANEL;
            if (sel) for (uint8_t x = 1; x < 39; x++) txt_put(x, r, ' ', C_WHITE, bg);
            txt_str(1, r, sel ? ">" : " ", C_GOLD, bg);
            txt_str(2, r, SPECIES_NAMES[i], C_WHITE, bg);
            txt_num(13, r, g_voy.stasis[i], 2, g_voy.stasis[i] ? C_GREEN : C_DIM, bg);
            txt_num(20, r, g_voy.keep[i], 1, g_voy.keep[i] >= 2 ? C_GOLD : C_CYAN, bg);
            txt_str(21, r, "/2", C_DIM, bg);
            txt_str(26, r, "$", C_GOLD, bg);
            txt_num(27, r, bio_bounty(i), 3, C_WHITE, bg);
        }
        txt_str(2, 16, "[Z] SELL 1   [LEFT] KEEP FOR MANIFEST", C_GREY, C_PANEL);
    } else if (pt_tab == 2) {
        uint8_t need = (uint8_t)(voy_hull_max() - g_voy.energy);
        uint16_t ecost = (uint16_t)need * EN_PRICE[pt_tier];
        uint16_t hcost = (uint16_t)(1000u + (uint16_t)g_voy.u_hull * 200u);
        uint16_t ccost = (g_voy.u_holds == 0) ? 3000u : 6000u;

        for (uint8_t i = 0; i < 3; i++) {
            uint8_t r = (uint8_t)(8 + i * 2);
            bool sel = (pt_sel == i);
            uint8_t bg = sel ? C_FRAME : C_PANEL;
            if (sel) for (uint8_t x = 1; x < 39; x++) txt_put(x, r, ' ', C_WHITE, bg);
            txt_str(1, r, sel ? ">" : " ", C_GOLD, bg);
            if (i == 0) {
                txt_str(2, r, "RECHARGE ENERGY", C_GREEN, bg);
                if (need == 0) txt_str(24, r, "FULL", C_DIM, bg);
                else { txt_str(23, r, "$", C_GOLD, bg); txt_num(24, r, ecost, 4, C_WHITE, bg); }
            } else if (i == 1) {
                txt_str(2, r, "HULL +5 MAX EN", C_CYAN, bg);
                if (g_voy.u_hull >= EN_UPS_MAX) txt_str(24, r, "MAXED", C_GOLD, bg);
                else { txt_str(23, r, "$", C_GOLD, bg); txt_num(24, r, hcost, 4, C_WHITE, bg); }
            } else {
                txt_str(2, r, "CARGO HOLDS +5", C_CYAN, bg);
                if (g_voy.u_holds >= 2) txt_str(24, r, "MAXED", C_GOLD, bg);
                else { txt_str(23, r, "$", C_GOLD, bg); txt_num(24, r, ccost, 4, C_WHITE, bg); }
            }
        }
        txt_str(2, 16, "HULL: ", C_GREY, C_PANEL);
        txt_num(8, 16, g_voy.energy, 3, C_GREEN, C_PANEL);
        txt_put(11, 16, '/', C_DIM, C_PANEL);
        txt_num(12, 16, voy_hull_max(), 3, C_WHITE, C_PANEL);
        txt_str(19, 16, "HOLDS: ", C_GREY, C_PANEL);
        txt_num(26, 16, voy_holds_used(), 2, C_CYAN, C_PANEL);
        txt_put(28, 16, '/', C_DIM, C_PANEL);
        txt_num(29, 16, voy_holds_max(), 2, C_WHITE, C_PANEL);
    } else {
        txt_str(2, 6, "SAFE SPACE BANK (KEPT IF ARK LOST)", C_GOLD, C_PANEL);
        txt_str(2, 8, "SAVINGS BALANCE: $", C_GREY, C_PANEL);
        txt_num(20, 8, g_voy.bank, 6, C_GREEN, C_PANEL);
        static const char *const blbl[4] = {
            "DEPOSIT $100", "DEPOSIT ALL", "WITHDRAW $100", "WITHDRAW ALL"
        };
        for (uint8_t i = 0; i < 4; i++) {
            uint8_t r = (uint8_t)(10 + i);
            bool sel = (pt_sel == i);
            uint8_t bg = sel ? C_FRAME : C_PANEL;
            if (sel) for (uint8_t x = 1; x < 39; x++) txt_put(x, r, ' ', C_WHITE, bg);
            txt_str(1, r, sel ? ">" : " ", C_GOLD, bg);
            txt_str(2, r, blbl[i], C_WHITE, bg);
        }
    }

    /* Leave Port line */
    uint8_t lidx = (uint8_t)(tab_lines() - 1);
    bool lsel = (pt_sel == lidx);
    uint8_t lbg = lsel ? C_FRAME : C_PANEL;
    if (lsel) for (uint8_t x = 1; x < 39; x++) txt_put(x, 19, ' ', C_WHITE, lbg);
    txt_str(1, 19, lsel ? ">" : " ", C_GOLD, lbg);
    txt_str(2, 19, "LEAVE STARPORT", C_ORANGE, lbg);

    /* Status / Message box */
    txt_box(0, 22, 40, 5, C_CYAN, C_PANEL);
    if (pt_msg[0])
        txt_str(2, 24, pt_msg, C_GOLD, C_PANEL);
    else
        txt_str(2, 24, "GAME AUTO-SAVED ON DOCKING", C_GREEN, C_PANEL);

    txt_center(29, "[Z] ACT   [X] NEXT TAB   [ESC] LEAVE", C_DIM, C_TRANS);
}

void port_enter(void)
{
    uint8_t s = g_voy.sector;
    pt_pidx = galaxy_port_index(s);
    pt_tier = (uint8_t)((g_gx.info[s] >> 6) & 3u);
    pt_cls  = (uint8_t)((g_gx.info[s] >> 2) & 7u);
    pt_ntabs = (pt_tier == TIER_SAFE) ? 4 : 3;
    pt_tab = 0;
    pt_sel = 0;
    pt_msg[0] = '\0';

    /* Deliver Relic automatically if aboard! */
    if (g_voy.relic_state == 1) {
        g_voy.relic_state = 2;
        g_voy.credits += 2000u;
        ach_check_all();
        set_msg("RELIC DELIVERED! +$2000 CREDITS!");
        sfx_player(SFX_FANFARE, SFX_PRI_TOP);
    }

    /* Auto-save on docking */
    save_write(g_save_slot);

    video_set_mode(VID_MODE_MENU, 0);
    port_draw();
    music_play("ROM:Map.vgm");
}

bool port_update(void)
{
    video_twinkle_stars(g_frame);
    uint8_t n = tab_lines();

    if (in_pressed & IN_UP) {
        pt_sel = (pt_sel == 0) ? (uint8_t)(n - 1) : (uint8_t)(pt_sel - 1);
        sfx_player(SFX_BLIP, SFX_PRI_LOW);
        port_draw();
    } else if (in_pressed & IN_DOWN) {
        pt_sel = (uint8_t)((pt_sel + 1) % n);
        sfx_player(SFX_BLIP, SFX_PRI_LOW);
        port_draw();
    } else if (in_pressed & IN_BTN2) {
        pt_tab = (uint8_t)((pt_tab + 1) % pt_ntabs);
        pt_sel = 0;
        pt_msg[0] = '\0';
        sfx_player(SFX_BLIP, SFX_PRI_LOW);
        port_draw();
    } else if (in_pressed & IN_START) {
        save_write(g_save_slot);
        sfx_player(SFX_LAUNCH, SFX_PRI_EVENT);
        return true;
    } else if (pt_tab == 1 && (in_pressed & IN_LEFT) && pt_sel < NUM_SPECIES) {
        /* Keep 1 specimen for manifest */
        if (g_voy.stasis[pt_sel] == 0) {
            set_msg("NONE IN STASIS");
            sfx_player(SFX_DENY, SFX_PRI_LOW);
        } else if (g_voy.keep[pt_sel] >= 2) {
            set_msg("MANIFEST PAIR ALREADY COMPLETE");
            sfx_player(SFX_DENY, SFX_PRI_LOW);
        } else {
            g_voy.stasis[pt_sel]--;
            g_voy.keep[pt_sel]++;
            ach_check_all();
            set_msg("SPECIMEN KEPT IN ARK MANIFEST!");
            sfx_player(SFX_TRADE, SFX_PRI_EVENT);
        }
        port_draw();
    } else if (in_pressed & IN_FIRE) {
        if (pt_sel == n - 1) {
            save_write(g_save_slot);
            sfx_player(SFX_LAUNCH, SFX_PRI_EVENT);
            return true;
        }
        if (pt_tab == 0) {
            uint8_t c = pt_sel;
            bool sells = (pt_cls & (1u << c)) != 0;
            uint16_t pr = comm_price(c);
            uint8_t *qty = comm_qty_ptr(c);
            if (sells) {
                if (g_voy.port_stock[pt_pidx][c] == 0) {
                    set_msg("SOLD OUT AT THIS PORT");
                    sfx_player(SFX_DENY, SFX_PRI_LOW);
                } else if (!voy_has_hold_room()) {
                    set_msg("CARGO HOLDS FULL");
                    sfx_player(SFX_DENY, SFX_PRI_LOW);
                } else if (g_voy.credits < pr) {
                    set_msg("NOT ENOUGH CREDITS");
                    sfx_player(SFX_DENY, SFX_PRI_LOW);
                } else {
                    g_voy.credits -= pr;
                    (*qty)++;
                    g_voy.port_stock[pt_pidx][c]--;
                    ach_check_all();
                    set_msg("PURCHASED 1 UNIT");
                    sfx_player(SFX_TRADE, SFX_PRI_EVENT);
                }
            } else {
                if (*qty == 0) {
                    set_msg("NONE IN CARGO HOLDS");
                    sfx_player(SFX_DENY, SFX_PRI_LOW);
                } else if (g_voy.port_stock[pt_pidx][c] >= PT_FULL) {
                    set_msg("PORT WANTS NO MORE");
                    sfx_player(SFX_DENY, SFX_PRI_LOW);
                } else {
                    (*qty)--;
                    g_voy.credits += pr;
                    g_voy.port_stock[pt_pidx][c]++;
                    if (g_voy.sold_goods < 255) g_voy.sold_goods++;
                    ach_check_all();
                    set_msg("SOLD 1 UNIT");
                    sfx_player(SFX_TRADE, SFX_PRI_EVENT);
                }
            }
        } else if (pt_tab == 1) {
            uint8_t sp = pt_sel;
            if (g_voy.stasis[sp] == 0) {
                set_msg("NONE OF THAT SPECIES IN STASIS");
                sfx_player(SFX_DENY, SFX_PRI_LOW);
            } else {
                uint16_t pay = bio_bounty(sp);
                g_voy.stasis[sp]--;
                g_voy.credits += pay;
                if (g_voy.port_glut[pt_pidx] < 8) g_voy.port_glut[pt_pidx]++;
                ach_check_all();
                set_msg("BOUNTY COLLECTED!");
                sfx_player(SFX_TRADE, SFX_PRI_EVENT);
            }
        } else if (pt_tab == 2) {
            if (pt_sel == 0) {
                uint8_t mx = voy_hull_max();
                if (g_voy.energy >= mx) {
                    set_msg("ENERGY ALREADY FULL");
                    sfx_player(SFX_DENY, SFX_PRI_LOW);
                } else {
                    uint8_t rate = EN_PRICE[pt_tier];
                    uint8_t need = (uint8_t)(mx - g_voy.energy);
                    uint8_t can = (uint8_t)(g_voy.credits / rate);
                    if (can == 0) {
                        set_msg("NOT ENOUGH CREDITS");
                        sfx_player(SFX_DENY, SFX_PRI_LOW);
                    } else {
                        uint8_t buy = (can < need) ? can : need;
                        g_voy.energy += buy;
                        g_voy.credits -= (uint32_t)buy * rate;
                        set_msg("ARK ENERGY RECHARGED");
                        sfx_player(SFX_TRADE, SFX_PRI_EVENT);
                    }
                }
            } else if (pt_sel == 1) {
                uint16_t cost = (uint16_t)(1000u + (uint16_t)g_voy.u_hull * 200u);
                if (g_voy.u_hull >= EN_UPS_MAX) {
                    set_msg("HULL ALREADY MAXED");
                    sfx_player(SFX_DENY, SFX_PRI_LOW);
                } else if (g_voy.credits < cost) {
                    set_msg("NOT ENOUGH CREDITS");
                    sfx_player(SFX_DENY, SFX_PRI_LOW);
                } else {
                    g_voy.credits -= cost;
                    g_voy.u_hull++;
                    g_voy.energy += EN_STEP;
                    ach_check_all();
                    set_msg("HULL UPGRADED (+5 MAX EN)");
                    sfx_player(SFX_FANFARE, SFX_PRI_EVENT);
                }
            } else if (pt_sel == 2) {
                uint16_t cost = (g_voy.u_holds == 0) ? 3000u : 6000u;
                if (g_voy.u_holds >= 2) {
                    set_msg("CARGO HOLDS ALREADY MAXED");
                    sfx_player(SFX_DENY, SFX_PRI_LOW);
                } else if (g_voy.credits < cost) {
                    set_msg("NOT ENOUGH CREDITS");
                    sfx_player(SFX_DENY, SFX_PRI_LOW);
                } else {
                    g_voy.credits -= cost;
                    g_voy.u_holds++;
                    ach_check_all();
                    set_msg("CARGO HOLDS EXPANDED (+5)");
                    sfx_player(SFX_FANFARE, SFX_PRI_EVENT);
                }
            }
        } else if (pt_tab == 3) {
            if (pt_sel == 0 && g_voy.credits >= 100u) {
                g_voy.credits -= 100u; g_voy.bank += 100u;
                set_msg("DEPOSITED $100"); sfx_player(SFX_TRADE, SFX_PRI_EVENT);
            } else if (pt_sel == 1 && g_voy.credits > 0) {
                g_voy.bank += g_voy.credits; g_voy.credits = 0;
                set_msg("DEPOSITED ALL CREDITS"); sfx_player(SFX_TRADE, SFX_PRI_EVENT);
            } else if (pt_sel == 2 && g_voy.bank >= 100u) {
                g_voy.bank -= 100u; g_voy.credits += 100u;
                set_msg("WITHDREW $100"); sfx_player(SFX_TRADE, SFX_PRI_EVENT);
            } else if (pt_sel == 3 && g_voy.bank > 0) {
                g_voy.credits += g_voy.bank; g_voy.bank = 0;
                set_msg("WITHDREW ALL SAVINGS"); sfx_player(SFX_TRADE, SFX_PRI_EVENT);
            } else {
                set_msg("INSUFFICIENT FUNDS"); sfx_player(SFX_DENY, SFX_PRI_LOW);
            }
            ach_check_all();
        }
        port_draw();
    }
    return false;
}
