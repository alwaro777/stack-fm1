/* SPDX-License-Identifier: GPL-3.0-or-later
 * STACK — the master bus: the send effects, the pump, the compressor and the DJ filter.
 *   delay    a dub ping-pong: the sends go into the left line, each line's echo feeds the other, through a tone
 *            low-pass, a gentle high-pass and tanh in the loop (so it can run away and growl, never explode). Time in
 *            16ths of the tempo; 0.557 s at most (24576 frames a side)
 *   reverb   Freeverb's shape (Jezar's public-domain design): four damped combs and two all-passes a side, the right
 *            side's lengths 23 samples longer for width
 *   pump     the sidechain: every kick ducks the other tracks, which come back over FX_PUMP_REL
 *   comp     a peak compressor on the sum: above -18 dB of the mix, AMOUNT blends towards a 4:1 slope, with make-up
 *   DJ       one knob: under 64 a low-pass closing, over 64 a high-pass opening, 60..68 off
 * Neutral settings change nothing, bit for bit. Fixed point, 32-bit products (dsp.h). */
#include "core.h"
#include "engine.h"

#define DLY_LEN 24576u           /* 0.557 s a side: a dub 3/16 from 100 BPM up (the pool's room goes to the patterns) */
static int16_t dly_l[DLY_LEN] STK_BIG, dly_r[DLY_LEN] STK_BIG;
static uint32_t dly_w;
static int32_t dly_lp_l, dly_lp_r, dly_hp_l, dly_hp_r;

static const uint16_t COMB_LEN[4] = {1116, 1188, 1277, 1356};
static const uint16_t AP_LEN[2] = {556, 441};
#define SPREAD 23
#define REV_TOTAL (1116 + 1188 + 1277 + 1356 + 556 + 441 + 6 * SPREAD)
static int16_t rev_l[REV_TOTAL] STK_BIG, rev_r[REV_TOTAL] STK_BIG;
static uint16_t rev_pos_l[6], rev_pos_r[6];
static int32_t comb_fs_l[4], comb_fs_r[4];

static svf_t djf_l, djf_r;
static int32_t pump_env;         /* Q16 */
static uint16_t pump_k;
static uint8_t pump_n;
static int32_t pump_target, pump_step;
static int32_t comp_env, comp_gain = 32768;

const uint8_t DLY_STEPS[6] = {1, 2, 3, 4, 6, 8};
const char *const DLY_NAME[6] = {"1/16", "1/8", "3/16", "1/4", "3/8", "1/2"};

void fx_init(void)
{
    uint32_t i;
    for (i = 0; i < DLY_LEN; i++)
        dly_l[i] = dly_r[i] = 0;
    for (i = 0; i < REV_TOTAL; i++)
        rev_l[i] = rev_r[i] = 0;
    for (i = 0; i < 6; i++)
        rev_pos_l[i] = rev_pos_r[i] = 0;
    for (i = 0; i < 4; i++)
        comb_fs_l[i] = comb_fs_r[i] = 0;
    dly_w = 0;
    dly_lp_l = dly_lp_r = dly_hp_l = dly_hp_r = 0;
    djf_l.ic1 = djf_l.ic2 = djf_r.ic1 = djf_r.ic2 = 0;
    pump_env = 0;
    pump_n = 0;
    comp_env = 0;
    comp_gain = 32768;
}

/* ------------------------------------------------------------------ pump --- */
void fx_pump_kick(void)
{
    pump_env = ENV_ONE;
    pump_n = 0;
}

/* the ducking gain per sample for the tracks the kicks push down, Q15; 0 = no pump at all (skip it) */
int fx_pump_fill(int32_t *duck, uint32_t n)
{
    uint32_t i, amt = g_proj.fx[FX_PUMP];
    int32_t depth = (int32_t)amt * 236;           /* Q15: up to 0.92 */
    if (!amt)
        return 0;
    pump_k = TAB_DECAY_K[clampi(50 + g_proj.fx[FX_PUMP_REL] / 2, 0, 127)];   /* ~0.1 .. 1.5 s to silence */
    for (i = 0; i < n; i++) {
        if (!pump_n) {
            pump_target = (int32_t)(((uint32_t)pump_env * pump_k) >> 16);
            pump_step = (pump_target - pump_env) / TAB_CTL;
            pump_n = TAB_CTL;
        }
        pump_n--;
        pump_env += pump_step;
        if (!pump_n)
            pump_env = pump_target;
        duck[i] = 32767 - ((depth * (clampi(pump_env, 0, ENV_ONE) >> 1)) >> 15);
    }
    return 1;
}

/* ----------------------------------------------------------------- reverb --- */
static inline int32_t comb(int16_t *buf, uint32_t len, uint16_t *pos, int32_t *fs, int32_t x, int32_t fb,
                           int32_t damp)
{
    int32_t y = buf[*pos];
    *fs += ((y - *fs) * damp) >> 15;             /* the damping: a one-pole low-pass in the loop */
    buf[*pos] = (int16_t)sat16(x + ((*fs * fb) >> 15));
    if (++*pos >= len)
        *pos = 0;
    return y;
}

static inline int32_t allpass(int16_t *buf, uint32_t len, uint16_t *pos, int32_t x)
{
    int32_t b = buf[*pos];
    buf[*pos] = (int16_t)sat16(x + (b >> 1));
    if (++*pos >= len)
        *pos = 0;
    return b - x;
}

static int32_t reverb_side(int16_t *buf, uint16_t *pos, int32_t *fs, int32_t x, int32_t fb, int32_t damp, int spread)
{
    int32_t acc = 0, k;
    int16_t *p = buf;
    for (k = 0; k < 4; k++) {
        uint32_t len = COMB_LEN[k] + (spread ? SPREAD : 0u);
        acc += comb(p, len, &pos[k], &fs[k], x, fb, damp);
        p += len;
    }
    acc >>= 2;
    for (k = 0; k < 2; k++) {
        uint32_t len = AP_LEN[k] + (spread ? SPREAD : 0u);
        acc = allpass(p, len, &pos[4 + k], clampi(acc, -32767, 32767));
        p += len;
    }
    return acc;
}

/* ------------------------------------------------------------------- bus --- */
void fx_bus(int32_t *l, int32_t *r, const int32_t *sd, const int32_t *sr, uint32_t n, int32_t spt_q16,
            int any_dly, int any_rev)
{
    uint32_t i;
    const uint8_t *fx = g_proj.fx;
    /* delay */
    if (any_dly || fx[FX_DLY_LEVEL]) {
        uint32_t steps = DLY_STEPS[fx[FX_DLY_TIME] < 6 ? fx[FX_DLY_TIME] : 2];
        uint32_t d = (uint32_t)((spt_q16 >> 8) * 24 * (int32_t)steps) >> 8;   /* 16ths in samples */
        int32_t fb = (int32_t)fx[FX_DLY_FB] * 270;           /* Q15: up to 1.05 (tanh keeps it bounded) */
        int32_t g_lp = dsp_onepole_g(64 * 256 + (int32_t)fx[FX_DLY_TONE] * 120);
        int32_t g_hp = dsp_onepole_g(40 * 256);
        int32_t lvl = (int32_t)fx[FX_DLY_LEVEL] * 258;
        if (d < 1u)
            d = 1u;
        if (d > DLY_LEN - 1u)
            d = DLY_LEN - 1u;
        for (i = 0; i < n; i++) {
            uint32_t rd = dly_w >= d ? dly_w - d : dly_w + DLY_LEN - d;
            int32_t ol = dly_l[rd], or_ = dly_r[rd], in_l, in_r;
            in_l = dsp_lp1(&dly_lp_l, (or_ * fb) >> 15, g_lp);   /* the right echo comes back left, */
            in_r = dsp_lp1(&dly_lp_r, (ol * fb) >> 15, g_lp);    /* the left one right: ping-pong */
            in_l -= dsp_lp1(&dly_hp_l, in_l, g_hp);
            in_r -= dsp_lp1(&dly_hp_r, in_r, g_hp);
            in_l = dsp_tanh(clampi(in_l + clampi(sd[i], -32767, 32767), -65535, 65535));
            in_r = dsp_tanh(clampi(in_r, -65535, 65535));
            dly_l[dly_w] = (int16_t)in_l;
            dly_r[dly_w] = (int16_t)in_r;
            if (++dly_w >= DLY_LEN)
                dly_w = 0;
            l[i] += (ol * lvl) >> 15;
            r[i] += (or_ * lvl) >> 15;
        }
    }
    /* reverb (paused at the guard's last level) */
    if ((any_rev || fx[FX_REV_LEVEL]) && core_shed_level < 3) {
        int32_t fb = 22938 + (int32_t)fx[FX_REV_SIZE] * 72;  /* 0.70 .. 0.98 */
        int32_t damp = 32767 - (int32_t)fx[FX_REV_DAMP] * 200;
        int32_t lvl = (int32_t)fx[FX_REV_LEVEL] * 258;
        for (i = 0; i < n; i++) {
            int32_t x = clampi(sr[i], -32767, 32767) >> 2;
            int32_t yl = reverb_side(rev_l, rev_pos_l, comb_fs_l, x, fb, damp, 0);
            int32_t yr = reverb_side(rev_r, rev_pos_r, comb_fs_r, x, fb, damp, 1);
            l[i] += (clampi(yl, -65535, 65535) * (lvl >> 1)) >> 14;
            r[i] += (clampi(yr, -65535, 65535) * (lvl >> 1)) >> 14;
        }
    }
    /* the DJ filter */
    if (fx[FX_DJF] < 60 || fx[FX_DJF] > 68) {
        int lp = fx[FX_DJF] < 60;
        int32_t cut = lp ? 30 * 256 + (int32_t)fx[FX_DJF] * 97 * 256 / 60
                         : 20 * 256 + ((int32_t)fx[FX_DJF] - 68) * 90 * 256 / 59;
        svf_set(&djf_l, cut, 50);
        djf_r.a1 = djf_l.a1;
        djf_r.a2 = djf_l.a2;
        djf_r.a3 = djf_l.a3;
        djf_r.k = djf_l.k;
        for (i = 0; i < n; i++) {
            l[i] = svf_run(&djf_l, clampi(l[i], -32767, 32767), lp ? SVF_LP : SVF_HP);
            r[i] = svf_run(&djf_r, clampi(r[i], -32767, 32767), lp ? SVF_LP : SVF_HP);
        }
    }
    /* the compressor */
    if (fx[FX_COMP]) {
        int32_t amt = fx[FX_COMP], makeup = 32768 + amt * 129;   /* up to +6 dB back */
        for (i = 0; i < n; i++) {
            int32_t al = l[i] < 0 ? -l[i] : l[i], ar = r[i] < 0 ? -r[i] : r[i], pk = al > ar ? al : ar;
            pk = clampi(pk, 0, 131071);
            comp_env += pk > comp_env ? (pk - comp_env) >> 5 : -((comp_env - pk) >> 12);   /* ~0.7 ms / 90 ms */
            if (!(i & 31u)) {                    /* the gain, at the control rate */
                int32_t thr = 4096, target = 32768;
                if (comp_env > thr) {
                    int32_t want = thr + ((comp_env - thr) >> 2);  /* 4:1 */
                    int32_t g = (int32_t)(((uint32_t)want << 15) / (uint32_t)comp_env);
                    target = 32768 - ((32768 - g) * amt) / 127;
                }
                comp_gain += (target - comp_gain) >> 2;
            }
            l[i] = ((clampi(l[i], -131071, 131071) >> 2) * (comp_gain >> 1)) >> 12;   /* x g / 2^15 */
            r[i] = ((clampi(r[i], -131071, 131071) >> 2) * (comp_gain >> 1)) >> 12;
            l[i] = ((clampi(l[i], -65535, 65535) >> 1) * (makeup >> 1)) >> 13;
            r[i] = ((clampi(r[i], -65535, 65535) >> 1) * (makeup >> 1)) >> 13;
        }
    }
}
