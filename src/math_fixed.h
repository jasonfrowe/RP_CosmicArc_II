/* Fixed-point math & RNG (software replacement for AstroCart MathUnit) */

#ifndef MATH_FIXED_H
#define MATH_FIXED_H

#include <stdint.h>

/* 32-bit xorshift RNG (same algorithm as AstroCart $043E) */
void rng_seed(uint32_t seed);
uint32_t rng_state(void);
uint8_t rng_u8(void);
uint16_t rng_u16(void);

/* Binary angles: 0..255 = full turn, 0 = right (+X), 64 = down (+Y),
 * 128 = left (-X), 192 = up (-Y). */

/* Signed 8.8 cosine/sine in range [-256, +256] */
int16_t cos256(uint8_t ang);
int16_t sin256(uint8_t ang);

/* Polar to rectangular: speed (8.8) along ang -> (vx, vy) in 8.8 */
void polar_to_rect(int16_t speed, uint8_t ang, int16_t *vx, int16_t *vy);

/* Direction of (dx, dy) in 0..255 binary angle */
uint8_t atan2_u8(int16_t dy, int16_t dx);

/* Fast Euclidean length sqrt(dx^2 + dy^2) */
uint16_t hypot_u16(int16_t dx, int16_t dy);

/* Step angle cur toward target by at most max_step */
uint8_t turn_toward(uint8_t cur, uint8_t target, uint8_t max_step);

#endif /* MATH_FIXED_H */
