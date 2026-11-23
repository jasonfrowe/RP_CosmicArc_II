#ifndef INPUT_H
#define INPUT_H

#include <stdint.h>

#define IN_UP    0x0001u
#define IN_DOWN  0x0002u
#define IN_LEFT  0x0004u
#define IN_RIGHT 0x0008u
#define IN_FIRE  0x0010u
#define IN_BTN2  0x0020u
#define IN_START 0x0040u

extern uint16_t in_held;
extern uint16_t in_pressed; /* Newly pressed this frame */

void input_init(void);
void input_update(void);

/* 8-way direction: 0=up, 1=UR, 2=R, 3=DR, 4=D, 5=DL, 6=L, 7=UL, 8=centered */
uint8_t in_joy8(void);

#endif /* INPUT_H */
