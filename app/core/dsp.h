/* SPDX-License-Identifier: GPL-3.0-or-later
 * STACK — fixed-point DSP building blocks, all static inline, 32-bit arithmetic only.
 *
 * Why no 64-bit multiplies: felucca-bsp docs/CPU.md records that on the FM-1's CPU a 64-bit multiply among 16-bit
 * loads can leave a register a little wrong. Every product here is kept inside 32 bits by its operands' ranges.
 *
 * Conventions: audio Q15 in int32 (headroom above ±32767 inside a voice is allowed; the mixer saturates); phase
 * uint32 (2^32 = one cycle); gains and coefficients Q15 unless named otherwise; envelopes Q16 (65536 = 1.0). */
#ifndef STK_DSP_H
#define STK_DSP_H
#include <stdint.h>
#include "tables.h"

#define Q15_ONE 32767
#define ENV_ONE 65536

static inline int32_t sat16(int32_t x) { return x > 32767 ? 32767 : x < -32767 ? -32767 : x; }
static inline int32_t clampi(int32_t x, int32_t lo, int32_t hi) { return x < lo ? lo : x > hi ? hi : x; }

/* sine, Q15, linear interpolation (the 24-bit fraction cut to 8 bits: (Δ < 805) * 256 fits easily) */
static inline int32_t dsp_sin(uint32_t ph)
{
    uint32_t i = ph >> 24;
    int32_t a = TAB_SINE[i], b = TAB_SINE[i + 1];
    return a + (((b - a) * (int32_t)((ph >> 16) & 0xFFu)) >> 8);
}

/* white noise: xorshift32, one state per voice; Q15 */
static inline int32_t dsp_noise(uint32_t *s)
{
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return (int32_t)(x >> 16) - 32768;
}

/* phase increment for a pitch in 1/256 semitone (MIDI note << 8), linear between semitones (< 1 cent off) */
static inline uint32_t dsp_note_inc(int32_t note256)
{
    uint32_t n, f, a, b;
    note256 = clampi(note256, 0, 127 << 8);
    n = (uint32_t)note256 >> 8;
    f = (uint32_t)note256 & 0xFFu;
    a = TAB_NOTE_INC[n];
    b = TAB_NOTE_INC[n + 1];
    return a + (((b - a) >> 8) * f);      /* (b - a) >> 8 < 2^24, * f < 2^32 */
}

/* polyBLEP correction for a falling edge at the phase wrap, Q15 (phase and increment as uint32) */
static inline int32_t dsp_blep(uint32_t ph, uint32_t inc)
{
    uint32_t d = (inc >> 16) | 1u;
    if (ph < inc) {                               /* just after the wrap: x = ph / inc */
        int32_t x = (int32_t)((ph >> 1) / d);
        int32_t u = 32768 - clampi(x, 0, 32768);
        return -((u * u) >> 15);
    }
    if (ph > ~inc) {                              /* just before it: r = distance to the wrap */
        int32_t x = (int32_t)(((~ph + 1u) >> 1) / d);
        int32_t u = 32768 - clampi(x, 0, 32768);
        return (u * u) >> 15;
    }
    return 0;
}

static inline int32_t dsp_saw(uint32_t ph, uint32_t inc)   /* rising saw, Q15 */
{
    return (int32_t)(ph >> 16) - 32768 - dsp_blep(ph, inc);
}

/* one-pole coefficient (TPT) for a cutoff in 1/256 semitone, Q15 */
static inline int32_t dsp_onepole_g(int32_t note256)
{
    uint32_t n, f;
    int32_t a, b;
    note256 = clampi(note256, 0, 127 << 8);
    n = (uint32_t)note256 >> 8;
    f = (uint32_t)note256 & 0xFFu;
    a = TAB_ONEPOLE_G[n];
    b = TAB_ONEPOLE_G[n + 1];
    return a + (((b - a) * (int32_t)f) >> 8);
}

/* one-pole low-pass step (TPT form: unconditionally stable for G in 0..1). x within ±65535 (Q15 with 6 dB of
 * headroom): |x - z| <= 131071, so (diff >> 1) * g <= 65535 * 32767 < 2^31 */
static inline int32_t dsp_lp1(int32_t *z, int32_t x, int32_t g)
{
    int32_t d = clampi(x - *z, -131071, 131071);
    int32_t v = ((d >> 1) * g) >> 14;
    int32_t y = v + *z;
    *z = y + v;
    return y;
}

/* tanh saturation for x in Q15 with headroom: input range ±4.0 (±131072), Q15 out */
static inline int32_t dsp_tanh(int32_t x)
{
    uint32_t u, i, f;
    x = clampi(x, -131071, 131071);
    u = (uint32_t)(x + 131072);           /* 0 .. 262143 */
    i = u >> 10;                          /* 0 .. 255 */
    f = u & 1023u;
    return TAB_TANH[i] + (((TAB_TANH[i + 1] - TAB_TANH[i]) * (int32_t)f) >> 10);
}

/* exponential decay envelope stepped at the control rate and interpolated per sample */
typedef struct {
    int32_t level;                /* Q16, now */
    int32_t target;               /* where this control block ends, exactly (the per-sample steps are rounded) */
    int32_t step;                 /* per-sample increment towards it */
    uint16_t k;                   /* Q16 multiplier per control block */
    uint8_t n;                    /* samples left in this block */
} denv_t;

static inline void denv_start(denv_t *e, int32_t level, uint32_t decay_param)
{
    e->level = level;
    e->target = level;
    e->k = TAB_DECAY_K[decay_param > 127u ? 127u : decay_param];
    e->n = 0;
    e->step = 0;
}

static inline int32_t denv_tick(denv_t *e)    /* one sample; Q16 */
{
    if (!e->n) {
        int32_t next;
        e->level = e->target;             /* land exactly: the rounded steps would leave a remainder for ever */
        next = (int32_t)(((uint32_t)e->level * e->k) >> 16);   /* level <= 2^16, k < 2^16 */
        if (next < 64)
            next = 0;                     /* -60 dB under full: done */
        e->target = next;
        e->step = (next - e->level) / TAB_CTL;
        e->n = TAB_CTL;
    }
    e->n--;
    e->level += e->step;
    return e->level < 0 ? 0 : e->level;
}

/* apply a Q16 envelope (0..65536) to a sample within ±2^17: env cut to 13 bits (-78 dB steps), product < 2^30 */
static inline int32_t env_apply(int32_t x, int32_t env)
{
    return (x * (env >> 3)) >> 13;
}

/* Q15 gain on a sample within ±2^16: product < 2^31 */
static inline int32_t gain15(int32_t x, int32_t g)
{
    return (clampi(x, -65535, 65535) * g) >> 15;
}

/* ------------------------------------------------------- state-variable filter --- */
/* TPT state-variable filter (Zavalishin's form): stable at any cutoff and resonance, low-, band- and high-pass from
 * one structure. Coefficients at the control rate (svf_set), one sample at a time (svf_run). States kept within
 * ±65535, coefficients Q14 <= 1, so every product stays under 2^30. */
typedef struct {
    int32_t ic1, ic2;            /* the two integrators' states */
    int32_t a1, a2, a3, k;       /* Q14, Q14, Q14; k Q12 (2 = no resonance .. ~0.05 = screaming) */
} svf_t;

enum { SVF_LP, SVF_BP, SVF_HP };

static inline void svf_set(svf_t *f, int32_t note256, int32_t reso127)
{
    uint32_t n, fr;
    int32_t g, den;
    note256 = clampi(note256, 0, 127 << 8);
    n = (uint32_t)note256 >> 8;
    fr = (uint32_t)note256 & 0xFFu;
    g = TAB_SVF_G[n] + (((TAB_SVF_G[n + 1] - TAB_SVF_G[n]) * (int32_t)fr) >> 8);   /* Q12, <= 25800 */
    f->k = 8192 - clampi(reso127, 0, 127) * 63;                                     /* Q12: 2.0 .. 0.05 */
    den = 4096 + ((g * ((g + f->k) >> 2)) >> 10);                                   /* Q12: 1 + g (g + k) */
    f->a1 = (int32_t)((1u << 26) / (uint32_t)den);                                   /* Q14 */
    f->a2 = (g * f->a1) >> 12;
    f->a3 = (g * f->a2) >> 12;
}

static inline int32_t svf_run(svf_t *f, int32_t x, int mode)
{
    int32_t v1, v2, v3;
    x = clampi(x, -32767, 32767);
    v3 = clampi(x - f->ic2, -65535, 65535);
    v1 = ((f->a1 * f->ic1) >> 14) + ((f->a2 * v3) >> 14);
    v2 = f->ic2 + ((f->a2 * f->ic1) >> 14) + ((f->a3 * v3) >> 14);
    f->ic1 = clampi(2 * v1 - f->ic1, -65535, 65535);
    f->ic2 = clampi(2 * v2 - f->ic2, -65535, 65535);
    if (mode == SVF_LP)
        return v2;
    if (mode == SVF_BP)
        return v1;
    return x - ((f->k * clampi(v1, -65535, 65535)) >> 12) - v2;
}

/* ------------------------------------------------------------------- ADSR --- */
/* Attack: a straight ramp; decay to sustain and release to zero: exponential, stepped at the control rate. Q16. */
enum { ADSR_OFF, ADSR_ATTACK, ADSR_DECAY, ADSR_SUSTAIN, ADSR_RELEASE };
typedef struct {
    uint8_t stage, n;
    int32_t level, target, step;
    int32_t att_inc;             /* per sample, Q16 */
    int32_t sus;                 /* Q16 */
    uint16_t kd, kr;             /* per control block, Q16 */
} adsr_t;

static inline void adsr_gate(adsr_t *e, uint32_t a, uint32_t d, uint32_t s, uint32_t r)
{
    uint32_t att_samples = (uint32_t)TAB_DECAY_MS[a > 127u ? 127u : a] * 44u / 4u + 1u;   /* (ramps are quicker) */
    e->att_inc = (int32_t)(65536u / att_samples) + 1;
    e->kd = TAB_DECAY_K[d > 127u ? 127u : d];
    e->kr = TAB_DECAY_K[r > 127u ? 127u : r];
    e->sus = (int32_t)(s > 127u ? 127u : s) * 516;
    e->stage = ADSR_ATTACK;
    e->n = 0;
}

static inline void adsr_release(adsr_t *e)
{
    if (e->stage != ADSR_OFF) {
        e->stage = ADSR_RELEASE;
        e->n = 0;
    }
}

static inline int32_t adsr_tick(adsr_t *e)
{
    switch (e->stage) {
    case ADSR_ATTACK:
        e->level += e->att_inc;
        if (e->level >= ENV_ONE) {
            e->level = ENV_ONE;
            e->stage = ADSR_DECAY;
            e->n = 0;
        }
        return e->level;
    case ADSR_DECAY:
    case ADSR_SUSTAIN:
    case ADSR_RELEASE:
        if (!e->n) {
            int32_t floor = e->stage == ADSR_RELEASE ? 0 : e->sus;
            uint16_t k = e->stage == ADSR_RELEASE ? e->kr : e->kd;
            int32_t next = floor + (int32_t)(((uint32_t)(e->level - floor) * k) >> 16);
            if (e->stage == ADSR_RELEASE && next < 64)
                next = 0;
            e->target = next;
            e->step = (next - e->level) / TAB_CTL;
            e->n = TAB_CTL;
        }
        e->n--;
        e->level += e->step;
        if (!e->n) {
            e->level = e->target;
            if (e->stage == ADSR_RELEASE && !e->level)
                e->stage = ADSR_OFF;
        }
        return e->level < 0 ? 0 : e->level;
    default:
        return 0;
    }
}

#endif
