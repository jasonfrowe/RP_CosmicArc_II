#include "math_fixed.h"

/* ------------------------------------------------------------------ */
/* 32-bit xorshift RNG (matches AstroCart MathUnit $043E)             */
/* ------------------------------------------------------------------ */

static uint32_t rng_x = 1u;

void rng_seed(uint32_t seed)
{
    rng_x = seed ? seed : 1u;
}

uint32_t rng_state(void)
{
    return rng_x;
}

uint8_t rng_u8(void)
{
    uint32_t x = rng_x;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_x = x;
    return (uint8_t)x;
}

uint16_t rng_u16(void)
{
    uint8_t lo = rng_u8();
    uint8_t hi = rng_u8();
    return (uint16_t)lo | ((uint16_t)hi << 8);
}

/* ------------------------------------------------------------------ */
/* Quarter-wave sine table (65 entries, 0..256 for 0..pi/2)           */
/* ------------------------------------------------------------------ */

static const uint16_t sin_q[65] = {
      0,   6,  13,  19,  25,  31,  38,  44,
     50,  56,  62,  68,  74,  80,  86,  92,
     98, 104, 109, 115, 121, 126, 132, 137,
    142, 147, 152, 157, 162, 167, 172, 177,
    181, 185, 190, 194, 198, 202, 206, 209,
    213, 216, 220, 223, 226, 229, 231, 234,
    237, 239, 241, 243, 245, 247, 248, 250,
    251, 252, 253, 254, 255, 255, 256, 256,
    256
};

int16_t sin256(uint8_t ang)
{
    uint8_t q = ang >> 6;
    uint8_t i = ang & 63;
    switch (q) {
    case 0: return (int16_t)sin_q[i];
    case 1: return (int16_t)sin_q[64 - i];
    case 2: return -(int16_t)sin_q[i];
    default: return -(int16_t)sin_q[64 - i];
    }
}

int16_t cos256(uint8_t ang)
{
    return sin256((uint8_t)(ang + 64u));
}

void polar_to_rect(int16_t speed, uint8_t ang, int16_t *vx, int16_t *vy)
{
    *vx = (int16_t)(((int32_t)speed * (int32_t)cos256(ang)) >> 8);
    *vy = (int16_t)(((int32_t)speed * (int32_t)sin256(ang)) >> 8);
}

/* ------------------------------------------------------------------ */
/* Fast octant atan2 returning 0..255 binary angle                    */
/* ------------------------------------------------------------------ */

/* atan(t / 32) in binary angle units (0..32 = 0..45 deg) */
static const uint8_t atan_tbl[33] = {
     0,  1,  3,  4,  5,  6,  8,  9,
    10, 11, 12, 14, 15, 16, 17, 18,
    19, 20, 21, 22, 23, 24, 25, 25,
    26, 27, 28, 29, 29, 30, 31, 31,
    32
};

uint8_t atan2_u8(int16_t dy, int16_t dx)
{
    if (dx == 0 && dy == 0)
        return 0;

    uint16_t ax = (dx < 0) ? (uint16_t)(-dx) : (uint16_t)dx;
    uint16_t ay = (dy < 0) ? (uint16_t)(-dy) : (uint16_t)dy;

    /* Shrink large values so (min << 5) fits in uint16_t */
    while ((ax | ay) >= 1024u) {
        ax >>= 1;
        ay >>= 1;
    }

    uint8_t a;
    if (ax >= ay) {
        uint8_t idx = (ax == 0) ? 0 : (uint8_t)((ay << 5) / ax);
        if (idx > 32) idx = 32;
        a = atan_tbl[idx];
    } else {
        uint8_t idx = (uint8_t)((ax << 5) / ay);
        if (idx > 32) idx = 32;
        a = (uint8_t)(64u - atan_tbl[idx]);
    }

    if (dx < 0)
        a = (uint8_t)(128u - a);
    if (dy < 0)
        a = (uint8_t)(0u - a);
    return a;
}

/* ------------------------------------------------------------------ */
/* Fast Euclidean distance (alpha-max-plus-beta-min, < 4% error)      */
/* ------------------------------------------------------------------ */

uint16_t hypot_u16(int16_t dx, int16_t dy)
{
    uint16_t ax = (dx < 0) ? (uint16_t)(-dx) : (uint16_t)dx;
    uint16_t ay = (dy < 0) ? (uint16_t)(-dy) : (uint16_t)dy;
    uint16_t mx = (ax > ay) ? ax : ay;
    uint16_t mn = (ax > ay) ? ay : ax;
    /* mx + 3/8 * mn is within ~3.5% of exact sqrt(dx^2 + dy^2) and ultra-fast */
    return (uint16_t)(mx + ((mn * 3u) >> 3));
}

uint8_t turn_toward(uint8_t cur, uint8_t target, uint8_t max_step)
{
    int8_t diff = (int8_t)(target - cur);
    if (diff > (int8_t)max_step)
        return (uint8_t)(cur + max_step);
    if (diff < -(int8_t)max_step)
        return (uint8_t)(cur - max_step);
    return target;
}
