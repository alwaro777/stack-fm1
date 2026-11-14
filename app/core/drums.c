/* SPDX-License-Identifier: GPL-3.0-or-later
 * STACK — the drum engine: modelled analogue voices (oscillators, noise, envelopes, filters, saturation), no samples.
 * Fixed point, 32-bit products only (dsp.h). The recipes follow the classic circuits in spirit, not their schematics:
 *   kicks   a sine with a falling pitch envelope, a noise click, a drive into tanh (gabber: far into it)
 *   snare   two detuned sines (the drum's body) under high-passed noise (the wires); SNAP balances them
 *   clap    band-passed noise under three quick bursts, then a tail
 *   hats    six square waves at inharmonic ratios, high-passed twice: the metallic cluster; open is choked by closed
 *   rim, tom, cowbell: short pitched voices
 *   shaker  high-passed noise with a soft attack (the beads' swing)
 *   ride    the hats' cluster a sixth higher with a bell partial on top, ringing long
 *   crash   noise and the cluster, a bright burst then a long wash
 * Params (0..127): TUNE (64 = the preset's pitch, ±24 semitones), DECAY, TONE (model's colour), SNAP (noise / click),
 * DRIVE. */
#include "engine.h"

#define BURST 353u               /* clap: 8 ms between bursts */

/* the six hat oscillators (Hz x10) */
static const uint16_t HAT_HZ10[6] = {2053, 3044, 3696, 5227, 5400, 8000};

static int32_t tune256(const track_t *t, uint32_t note)   /* the TUNE knob plus the step's note, 1/256 semitone */
{
    return ((int32_t)t->p[PR_TUNE] - 64) * 96 + ((int32_t)note - 60) * 256;   /* 64 steps = 24 semitones */
}

static uint32_t ratio_q12(int32_t shift256)           /* 2^(shift/12) in Q12, |shift| clamped to 3 octaves */
{
    uint32_t a = TAB_NOTE_INC[60] >> 8;
    uint32_t b = dsp_note_inc(60 * 256 + clampi(shift256, -36 * 256, 36 * 256)) >> 8;
    return b * 4096u / a;                             /* b <= 8 * a < 800000: b * 4096 < 2^32 */
}

void drum_trigger(dvoice_t *v, const track_t *t, uint32_t note, uint32_t vel, uint32_t flags)
{
    int32_t sh = tune256(t, note);
    uint32_t i, decay = t->p[PR_DECAY], tone = t->p[PR_TONE];
    v->model = t->model;
    v->active = 1;
    v->t = 0;
    v->vel = (int32_t)vel * 258;
    if (flags & STEP_ACCENT)
        v->vel = clampi(v->vel + v->vel / 3, 0, 32767);
    v->drive = t->p[PR_DRIVE];
    v->snap = t->p[PR_SNAP];
    v->tone = (int32_t)tone;
    if (!v->rnd)
        v->rnd = 0x9E3779B9u;
    for (i = 0; i < 4; i++)
        v->z[i] = 0;
    denv_start(&v->amp, ENV_ONE, decay);
    switch (v->model) {
    case DM_KICK808:
    case DM_KICK909:
    case DM_GABBER: {
        /* base pitch: 50 / 55 / 60 Hz (MIDI 31.3 / 33 / 34.5), the sweep starts 3x / 5.5x / 7x above */
        static const int32_t BASE[3] = {8013, 8448, 8832};
        static const int32_t START[3] = {4057, 7564, 8645};   /* + semitones*256 above base: x3, x5.5, x7 */
        uint32_t m = v->model - DM_KICK808;
        v->base_inc = (int32_t)dsp_note_inc(BASE[m] + sh);
        v->sweep = (int32_t)dsp_note_inc(BASE[m] + START[m] + sh) - v->base_inc;
        denv_start(&v->aux, ENV_ONE, 25u + (tone >> 2));         /* pitch drop: 10 .. 77 ms */
        v->ph[0] = 0x40000000u;                                  /* start at the sine's peak: a harder thump */
        break;
    }
    case DM_SNARE:
        v->inc[0] = dsp_note_inc(54 * 256 + 10 + sh);            /* ~180 Hz */
        v->inc[1] = dsp_note_inc(64 * 256 + 37 + sh);            /* ~330 Hz */
        v->ph[0] = v->ph[1] = 0;
        denv_start(&v->aux, ENV_ONE, 40u + (tone >> 2));         /* the body: 20 .. 100 ms */
        break;
    case DM_CLAP:
        denv_start(&v->amp, ENV_ONE, decay);                     /* (used for the tail, from the third burst) */
        break;
    case DM_RIDE:
    case DM_CRASH:
    case DM_HAT_C:
    case DM_HAT_O: {
        uint32_t r;
        if (v->model == DM_RIDE || v->model == DM_CRASH)   /* the same cluster, higher */
            sh += v->model == DM_RIDE ? 9 * 256 : 5 * 256;
        r = ratio_q12(sh);
        v->ph_bell = 0;
        for (i = 0; i < 6; i++)
            v->inc[i] = (((uint32_t)HAT_HZ10[i] * 9739u) >> 12) * r;   /* Hz x10 -> inc, then x ratio */
        if (v->model == DM_RIDE)
            denv_start(&v->aux, ENV_ONE, 70u);                   /* the bell's ring */
        if (v->model == DM_CRASH)
            denv_start(&v->aux, ENV_ONE, 40u);                   /* the burst */
        break;
    }
    case DM_SHAKER:
        break;
    case DM_RIM:
        v->inc[0] = dsp_note_inc(92 * 256 + 128 + sh);           /* ~1.7 kHz */
        v->ph[0] = 0;
        break;
    case DM_TOM:
        v->base_inc = (int32_t)dsp_note_inc(45 * 256 + sh);      /* ~110 Hz */
        v->sweep = (int32_t)dsp_note_inc(45 * 256 + 7 * 256 + sh) - v->base_inc;   /* a fifth above */
        denv_start(&v->aux, ENV_ONE, 45u);
        v->ph[0] = 0;
        break;
    case DM_COWBELL:
        v->inc[0] = dsp_note_inc(73 * 256 + 4 + sh);             /* ~540 Hz */
        v->inc[1] = dsp_note_inc(79 * 256 + 205 + sh);           /* ~800 Hz */
        denv_start(&v->aux, ENV_ONE, 20u);                       /* the strike */
        break;
    }
}

void drum_choke(dvoice_t *v)     /* an open hat cut short by a closed one */
{
    v->amp.k = TAB_DECAY_K[12];
}

/* drive into tanh: pre-gain 1x .. 4x (gabber 8x .. 40x) */
static inline int32_t drive_sat(int32_t x, int32_t drive, int gabber)
{
    int32_t g = gabber ? 32768 + drive * 1290 : 4096 + drive * 96;   /* Q12 */
    return dsp_tanh((clampi(x, -32767, 32767) * (g >> 2)) >> 10);    /* < 2^15 * 2^16 = 2^31 */
}

void drum_render(dvoice_t *v, const track_t *t, int32_t *out, uint32_t n)
{
    uint32_t i;
    int32_t vel = v->vel;
    (void)t;
    if (!v->active)
        return;
    switch (v->model) {
    case DM_KICK808:
    case DM_KICK909:
    case DM_GABBER: {
        int gab = v->model == DM_GABBER;
        int32_t click = v->snap * 200;                            /* noise click level */
        for (i = 0; i < n; i++) {
            int32_t pe = denv_tick(&v->aux), a = denv_tick(&v->amp), s;
            v->ph[0] += (uint32_t)(v->base_inc + (v->sweep >> 12) * (pe >> 4));   /* (2^13 * 2^12) */
            s = dsp_sin(v->ph[0]);
            if (v->t < 150u)                                      /* 3.4 ms of click, fading */
                s += ((dsp_noise(&v->rnd) >> 2) * (click >> 4) >> 11) * (int32_t)(150u - v->t) / 150;
            s = drive_sat(s, v->drive, gab);
            out[i] += gain15(env_apply(s, a), vel);
            v->t++;
        }
        break;
    }
    case DM_SNARE: {
        int32_t g = dsp_onepole_g(90 * 256 + v->tone * 48);       /* the wires' high-pass: 1.5 .. 6 kHz */
        int32_t glp = dsp_onepole_g(116 * 256 + v->tone * 24);    /* and their top: ~6.6 .. 13 kHz */
        int32_t mixn = v->snap * 258, mixt = 32767 - mixn;
        for (i = 0; i < n; i++) {
            int32_t body = denv_tick(&v->aux), a = denv_tick(&v->amp), x, nz, tn;
            v->ph[0] += v->inc[0];
            v->ph[1] += v->inc[1];
            tn = env_apply((dsp_sin(v->ph[0]) + dsp_sin(v->ph[1])) >> 1, body);
            x = dsp_noise(&v->rnd);
            x = dsp_lp1(&v->z[1], x, glp);                        /* the wires: noise between ~1.5 and ~7 kHz */
            nz = env_apply((x - dsp_lp1(&v->z[0], x, g)) * 2, a);
            x = gain15(tn, mixt) + gain15(nz, mixn);
            out[i] += gain15(drive_sat(x, v->drive, 0), vel);
        }
        if (v->amp.level == 0 && v->aux.level == 0)
            v->active = 0;
        return;
    }
    case DM_CLAP: {
        int32_t ghp = dsp_onepole_g(84 * 256), glp = dsp_onepole_g(96 * 256 + v->tone * 24);
        for (i = 0; i < n; i++) {
            int32_t x = dsp_noise(&v->rnd), e;
            x = x - dsp_lp1(&v->z[0], x, ghp);                    /* band-pass: high-pass then low-pass */
            x = dsp_lp1(&v->z[1], x, glp);
            if (v->t < 3u * BURST)
                e = (int32_t)((BURST - v->t % BURST) * (ENV_ONE / BURST));
            else
                e = denv_tick(&v->amp);
            out[i] += gain15(drive_sat(env_apply(x * 2, e), v->drive, 0), vel);
            v->t++;
        }
        if (v->t >= 3u * BURST && v->amp.level == 0)
            v->active = 0;
        return;
    }
    case DM_HAT_C:
    case DM_HAT_O: {
        int32_t g = dsp_onepole_g(104 * 256 + v->tone * 24), mixn = v->snap * 200;
        for (i = 0; i < n; i++) {
            int32_t a = denv_tick(&v->amp), x = 0, k;
            for (k = 0; k < 6; k++) {
                v->ph[k] += v->inc[k];
                x += (int32_t)(v->ph[k] >> 31) * 2 - 1;           /* square: ±1 */
            }
            x = x * 4500 + ((dsp_noise(&v->rnd) * (mixn >> 8)) >> 7);
            x = x - dsp_lp1(&v->z[0], x, g);                      /* high-pass twice */
            x = x - dsp_lp1(&v->z[1], x, g);
            x = clampi(x, -32767, 32767) * 4;                     /* what is left of the cluster is quiet: +12 dB */
            out[i] += gain15(env_apply(drive_sat(x, v->drive, 0), a), vel);
        }
        break;
    }
    case DM_SHAKER: {
        int32_t ghp = dsp_onepole_g(106 * 256 + v->tone * 16), glp = dsp_onepole_g(122 * 256);
        for (i = 0; i < n; i++) {
            int32_t a = denv_tick(&v->amp), x = dsp_noise(&v->rnd), att;
            x = dsp_lp1(&v->z[1], x - dsp_lp1(&v->z[0], x, ghp), glp);
            att = v->t < 260u ? (int32_t)v->t * 252 : 65535;      /* a 6 ms swell */
            x = env_apply(env_apply(x * 3, att), a);
            out[i] += gain15(drive_sat(x, v->drive, 0), vel);
            v->t++;
        }
        break;
    }
    case DM_RIDE:
    case DM_CRASH: {
        int32_t g = dsp_onepole_g(98 * 256 + v->tone * 16), mixn = v->snap * 200;
        int crash = v->model == DM_CRASH;
        uint32_t bell = dsp_note_inc(91 * 256 + 64);              /* ~3.1 kHz */
        for (i = 0; i < n; i++) {
            int32_t a = denv_tick(&v->amp), e2 = denv_tick(&v->aux), x = 0, k;
            for (k = 0; k < 6; k++) {
                v->ph[k] += v->inc[k];
                x += (int32_t)(v->ph[k] >> 31) * 2 - 1;
            }
            x = x * 3000 + ((dsp_noise(&v->rnd) * ((crash ? mixn + 12000 : mixn) >> 8)) >> 7);
            x = x - dsp_lp1(&v->z[0], x, g);
            x = clampi(x, -32767, 32767) * 2;
            if (!crash) {                                         /* the bell */
                v->ph_bell += bell;
                x += env_apply(dsp_sin(v->ph_bell) >> 1, e2);
            } else {
                x = env_apply(x, 32768 + (e2 >> 1));               /* the burst over the wash */
            }
            out[i] += gain15(env_apply(drive_sat(x, v->drive, 0), a), vel);
        }
        break;
    }
    case DM_RIM: {
        int32_t g = dsp_onepole_g(100 * 256);
        for (i = 0; i < n; i++) {
            int32_t a = denv_tick(&v->amp), x;
            v->ph[0] += v->inc[0];
            x = dsp_sin(v->ph[0]);
            if (v->t < 130u) {                                    /* the stick: 3 ms of bright noise */
                int32_t nz = dsp_noise(&v->rnd);
                x += (nz - dsp_lp1(&v->z[0], nz, g)) * (int32_t)(130u - v->t) / 130;
            }
            out[i] += gain15(env_apply(drive_sat(x, v->drive, 0), a), vel);
            v->t++;
        }
        break;
    }
    case DM_TOM: {
        int32_t mixn = v->snap * 100;
        for (i = 0; i < n; i++) {
            int32_t pe = denv_tick(&v->aux), a = denv_tick(&v->amp), x;
            v->ph[0] += (uint32_t)(v->base_inc + (v->sweep >> 12) * (pe >> 4));
            x = dsp_sin(v->ph[0]) + ((dsp_noise(&v->rnd) * (mixn >> 7)) >> 8);
            out[i] += gain15(env_apply(drive_sat(x, v->drive, 0), a), vel);
        }
        break;
    }
    case DM_COWBELL: {
        int32_t ghp = dsp_onepole_g(70 * 256), glp = dsp_onepole_g(98 * 256 + v->tone * 16);
        for (i = 0; i < n; i++) {
            int32_t a = denv_tick(&v->amp), st = denv_tick(&v->aux), x;
            v->ph[0] += v->inc[0];
            v->ph[1] += v->inc[1];
            x = ((int32_t)(v->ph[0] >> 31) + (int32_t)(v->ph[1] >> 31)) * 24000 - 24000;   /* two squares */
            x = x - dsp_lp1(&v->z[0], x, ghp);
            x = dsp_lp1(&v->z[1], x, glp);
            x = env_apply(x, (a >> 1) + (st >> 1));               /* the strike, then the ring */
            out[i] += gain15(drive_sat(x, v->drive, 0), vel);
        }
        if (v->amp.level == 0)
            v->active = 0;
        return;
    }
    }
    if (v->amp.level == 0)
        v->active = 0;
}
