#ifndef INPUT_H
#define INPUT_H

#include <stdint.h>

#define IN_UP     0x0001u
#define IN_DOWN   0x0002u
#define IN_LEFT   0x0004u
#define IN_RIGHT  0x0008u
#define IN_A      0x0010u /* Confirm / Primary (Gamepad A, Z/Enter/Space) */
#define IN_B      0x0020u /* Cancel / Back     (Gamepad B, X/Esc/Bksp)    */
#define IN_X      0x0040u /* Secondary Action  (Gamepad X/Y/C, C/K)       */
#define IN_L1     0x0080u /* Prev Page / Tab   (Gamepad L1, Q/[)          */
#define IN_R1     0x0100u /* Next Page / Tab   (Gamepad R1, E/]/Tab)      */
#define IN_START  0x0200u /* Start / Menu      (Gamepad START, P)         */
#define IN_SELECT 0x0400u /* Select / Aux      (Gamepad SELECT, Del)      */

/* Gameplay aliases */
#define IN_FIRE   IN_A
#define IN_BTN2   (IN_B | IN_X)

extern uint16_t in_held;
extern uint16_t in_pressed; /* Newly pressed this frame */

void input_init(void);
void input_update(void);

/* 8-way direction: 0=up, 1=UR, 2=R, 3=DR, 4=D, 5=DL, 6=L, 7=UL, 8=centered */
uint8_t in_joy8(void);

#endif /* INPUT_H */
