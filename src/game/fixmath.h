/* 16.16 fixed-point helpers, angle tables and the MSVC rand() used by the simulation.
   Original addresses in comments. Headings: full circle = 0x400000, 0 = north (-y), clockwise. */
#pragma once
#include <stdint.h>
#include <stdbool.h>

#define FIX_ONE      0x10000
#define ANG_MASK     0x3fffff
#define CELL_SIZE    0x200000      /* 32 px */
#define WORLD_SIZE   0x10000000    /* 128 cells */

/* FixMul 0x438300: imul + shrd 16 (low 32 bits of the 64-bit product >> 16). */
static inline int32_t fix_mul(int32_t a, int32_t b) { return (int32_t)(uint32_t)(((int64_t)a * b) >> 16); }

/* InitProjectionAndRotTables 0x4050f0 (rotation part): 64 yaw matrices at 0x481780 (3x3, row-major)
   and 64 unit heading vectors at 0x482480 = (0,-1,0) * M = (sin, -cos, 0). */
extern int32_t rot_mat[64][9];
extern int32_t dir_vec[64][3];
void fixmath_init(void);

int32_t cos_fixed(int32_t a);                   /* CosFixed 0x438360: a in 1/0x1000000 turns */
int32_t sin_fixed(int32_t a);                   /* SinFixed 0x438520 */
int32_t atan2_fixed(int32_t x, int32_t y);      /* Atan2Fixed 0x438310: atan2(y,x) in 0..0x1000000 */
uint32_t angle_between(const int32_t *from, const int32_t *to); /* AngleBetweenPoints 0x407c00 */
void vec_mul_mat3(int32_t *out, const int32_t *v, const int32_t *m); /* VecMulMat3 0x42bb00 */
void mat_transform_verts(int32_t (*out)[3], const int32_t (*in)[3], const int32_t *m, int n); /* 0x4383c0 */

/* Simulation clock: DAT_0045f3a8 (frame delta in 16 ms ticks) and DAT_0045f390 (tick counter). */
extern int32_t g_dt;
extern int32_t g_tick;

bool turn_towards(uint32_t *ang, uint32_t target, int32_t rate);  /* TurnTowardsAngle 0x423460 (rate*dt) */
int32_t approach(int32_t v, int32_t target, int32_t step);         /* ApproachValue 0x41ed80 */
int32_t approach_dt(int32_t v, int32_t target, int32_t up, int32_t down); /* 0x41edb0 (rates * dt) */
int32_t dist_sq_px(const int32_t *a, const int32_t *b);           /* DistSqPixels 0x41ed50 */

/* MSVC 4.x rand()/srand() (CRT, seed at 0x455b60) and RandRange 0x4335f0. */
void rng_seed(uint32_t s);
uint32_t rng_state(void);
int rng_rand(void);
int32_t rand_range(int32_t n);
