#include "fixmath.h"
#include <math.h>

int32_t rot_mat[64][9];
int32_t pitch_mat[64][9];      /* 0x48a7b0 */
int32_t recip_tab[256];        /* 0x482080: recip_tab[k-1] = FixDiv(1.0, k), k = 1..255 */
int32_t dir_vec[64][3];
int32_t g_dt;
int32_t g_tick;

/* Constants from .rdata: 0x43d010 = 1/65536, 0x43d018 = 65536, 0x43d020 = 256/2pi, 0x43d038 = 2pi/256.
   The x87 code truncates via __ftol (0x438a88). */
static const double K_INV65536 = 1.52587890625e-05, K_65536 = 65536.0;
static const double K_TO_STEPS = 40.74366543152521, K_TO_RAD = 0.02454369260617026;

int32_t cos_fixed(int32_t a) { return (int32_t)(cos((double)a * K_INV65536 * K_TO_RAD) * K_65536); }
int32_t sin_fixed(int32_t a) { return (int32_t)(sin((double)a * K_INV65536 * K_TO_RAD) * K_65536); }

int32_t atan2_fixed(int32_t x, int32_t y)
{
    double v = atan2((double)y * K_INV65536, (double)x * K_INV65536) * K_TO_STEPS;
    if (v < 0.0) v += 256.0;
    return (int32_t)(v * K_65536);
}

uint32_t angle_between(const int32_t *from, const int32_t *to)
{
    return (uint32_t)((atan2_fixed(from[0] - to[0], from[1] - to[1]) >> 2) - 0x100000) & ANG_MASK;
}

void vec_mul_mat3(int32_t *out, const int32_t *v, const int32_t *m)
{
    int32_t x = fix_mul(v[1], m[3]) + fix_mul(v[2], m[6]) + fix_mul(v[0], m[0]);
    int32_t y = fix_mul(v[2], m[7]) + fix_mul(v[1], m[4]) + fix_mul(v[0], m[1]);
    int32_t z = fix_mul(v[1], m[5]) + fix_mul(v[2], m[8]) + fix_mul(v[0], m[2]);
    out[0] = x; out[1] = y; out[2] = z;
}

void mat_transform_verts(int32_t (*out)[3], const int32_t (*in)[3], const int32_t *m, int n)
{
    for (int i = 0; i < n; i++) {
        int32_t x = in[i][0], y = in[i][1], z = in[i][2];
        out[i][0] = fix_mul(x, m[0]) + fix_mul(y, m[3]) + fix_mul(z, m[6]);
        out[i][1] = fix_mul(x, m[1]) + fix_mul(y, m[4]) + fix_mul(z, m[7]);
        out[i][2] = fix_mul(x, m[2]) + fix_mul(y, m[5]) + fix_mul(z, m[8]);
    }
}

int32_t fix_div(int32_t a, int32_t b)   /* FixDiv 0x438390: x87 (a/65536)/(b/65536)*65536, __ftol */
{
    if (b == 0) return 0;
    double r = ((double)a * K_INV65536) / ((double)b * K_INV65536) * K_65536;
    return (int32_t)(uint32_t)(int64_t)r;
}
int32_t fix_sqrt(int32_t a) { return (int32_t)(sqrt((double)a * K_INV65536) * K_65536); }   /* FixSqrt 0x438580 */
int32_t isqrt_fixed(int32_t a) { return (int32_t)sqrt((double)a); }                          /* IsqrtFixed 0x438550 */

void mat_mul3(int32_t *o, const int32_t *a, const int32_t *b)   /* Mat_Mul3x3 0x438460: o = a*b */
{
    int32_t t[9];
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++)
            t[r * 3 + c] = fix_mul(a[r * 3], b[c]) + fix_mul(a[r * 3 + 1], b[3 + c]) + fix_mul(a[r * 3 + 2], b[6 + c]);
    for (int i = 0; i < 9; i++) o[i] = t[i];
}

void mat_rot_x(int32_t *m, int32_t a)   /* FUN_00408400 on the zero-initialised 0x48b0b0 */
{
    for (int i = 0; i < 9; i++) m[i] = 0;
    m[0] = 0x10000;
    m[4] = m[8] = cos_fixed(a);
    m[5] = sin_fixed(a);
    m[7] = -m[5];
}

void fixmath_init(void)
{
    for (int k = 1; k < 256; k++) recip_tab[k - 1] = fix_div(0x10000, k << 16);
    for (int k = 0; k < 64; k++) {
        int32_t pa = k * 0x40000, pc = cos_fixed(pa), ps = sin_fixed(pa);
        int32_t *p = pitch_mat[k];
        p[0] = 0x10000; p[1] = 0; p[2] = 0; p[3] = 0; p[4] = pc; p[5] = ps; p[6] = 0; p[7] = -ps; p[8] = pc;
    }
    for (int k = 0; k < 64; k++) {
        int32_t a = k * 0x40000, c = cos_fixed(a), s = sin_fixed(a);
        int32_t *m = rot_mat[k];
        m[0] = c; m[1] = s; m[2] = 0;
        m[3] = -s; m[4] = c; m[5] = 0;
        m[6] = 0; m[7] = 0; m[8] = 0x10000;
        const int32_t up[3] = { 0, -0x10000, 0 };
        vec_mul_mat3(dir_vec[k], up, m);
    }
}

bool turn_towards(uint32_t *ang, uint32_t target, int32_t rate)
{
    uint32_t a = *ang, d = (target - a) & ANG_MASK;
    if (d == 0) return true;
    if (((0u - d) & ANG_MASK) < d) {           /* shorter to turn negative */
        a = (a - (uint32_t)(rate * g_dt)) & ANG_MASK;
        d = (target - a) & ANG_MASK;
        if (((0u - d) & ANG_MASK) > d) a = target;   /* overshot */
    } else {
        a = (a + (uint32_t)(rate * g_dt)) & ANG_MASK;
        d = (target - a) & ANG_MASK;
        if (d > ((0u - d) & ANG_MASK)) a = target;
    }
    *ang = a;
    return target == a;
}

int32_t approach(int32_t v, int32_t target, int32_t step)
{
    if (v < target) { v += step; if (v > target) return target; }
    else if (v > target) { v -= step; if (v < target) return target; }
    return v;
}

int32_t approach_dt(int32_t v, int32_t target, int32_t up, int32_t down)
{
    if (v < target) { v += g_dt * up; if (v > target) return target; }
    else if (v > target) { v -= down * g_dt; if (v < target) return target; }
    return v;
}

int32_t dist_sq_px(const int32_t *a, const int32_t *b)
{
    int32_t x = (a[0] - b[0]) >> 16, y = (a[1] - b[1]) >> 16;
    return y * y + x * x;
}

static uint32_t holdrand = 1;
void rng_seed(uint32_t s) { holdrand = s; }
uint32_t rng_state(void) { return holdrand; }
int rng_rand(void) { holdrand = holdrand * 214013u + 2531011u; return (int)((holdrand >> 16) & 0x7fff); }
/* RandRange 0x4335f0: ((rand() << 17) split into 16-bit halves) * n, i.e. (uint32)(2r * n) >> 16 (wraps for large n) */
int32_t rand_range(int32_t n) { return (int32_t)(((uint32_t)(rng_rand() & 0x7fff) * 2u * (uint32_t)n) >> 16); }
