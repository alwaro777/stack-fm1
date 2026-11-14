/* SPDX-License-Identifier: GPL-3.0-or-later
 * STACK — the VA synth: leads, plucks, pads, strings, the reese, the hoover, chord stabs. Pool voices (voices.c).
 *   oscillators (WAVE)  SAW, SQUARE, SUPER (five detuned saws), PWM (a pulse swept by a slow LFO), SINE
 *   DETUNE              the supersaw's spread; on the others a second oscillator against the first (the reese,
 *                       an ensemble for pads), drifting with the LFO so a held chord shimmers like a chorus
 *   filter              a state-variable filter, TONE picks low-, band- or high-pass; CUTOFF + ENVMOD x the filter
 *                       envelope (which shares the ADSR's times but falls to zero) + half the keyboard
 *   amplitude           ADSR (ATTACK DECAY SUSTAIN RELEASE)
 *   models              POLY (4 voices), MONO (one, gliding), HOOVER (mono: supersaw + a sub an octave down,
 *                       swooping up from a fifth under the note), CHORD (4 voices: the track's chord on each note)
 * Fixed point, 32-bit products (dsp.h). */
#include "engine.h"
#include "core.h"

#define SY_CTL 32u
#define GLIDE_K 30               /* per control block, /512: ~60 ms */

void synth_start(pvoice_t *v, const track_t *t, int32_t note256, uint32_t vel, int legato)
{
    int32_t tune = ((int32_t)t->p[PR_TUNE] - 64) * 96;
    v->target256 = note256 + tune;
    if (legato)
        return;                                   /* a mono glide: the envelopes go on */
    v->note256 = v->target256;
    v->vel = (int32_t)vel * 258;
    v->ph[0] = 0;                                 /* the saws start apart, always the same way: no beating at onset */
    v->ph[1] = 0x33333333u;
    v->ph[2] = 0x66666666u;
    v->ph[3] = 0x99999999u;
    v->ph[4] = 0xCCCCCCCCu;
    v->ph[5] = 0;
    v->f.ic1 = v->f.ic2 = 0;
    v->pitch_env = t->model == SY_HOOVER ? ENV_ONE : 0;
    adsr_gate(&v->amp, t->p[PR_ATTACK], t->p[PR_DECAY], t->p[PR_SUSTAIN], t->p[PR_RELEASE]);
    adsr_gate(&v->mod, t->p[PR_ATTACK], t->p[PR_DECAY], 0, t->p[PR_RELEASE]);
    v->amp.level = v->mod.level = 0;
}

void synth_render(pvoice_t *v, const track_t *t, int32_t *out, uint32_t n)
{
    uint32_t i, k, wave = (uint32_t)t->p[PR_WAVE] * NOSCS / 128u, mode = t->p[PR_TONE] / 43u;
    uint32_t inc[6] = {0, 0, 0, 0, 0, 0};
    int32_t spread = (int32_t)t->p[PR_DETUNE] * 2;            /* up to ~1 semitone across the supersaw */
    int32_t drive = 4096 + (int32_t)t->p[PR_DRIVE] * 120;      /* Q12 */
    int32_t gain = t->model == SY_CHORD || t->model == SY_POLY ? 23000 : 27000;   /* chords leave room */
    int hoover = t->model == SY_HOOVER;
    uint32_t nsaw = core_shed_level >= 1 ? 3u : 5u;   /* under load: three saws, not five */
    uint32_t pw = 0x80000000u;
    for (i = 0; i < n; i++) {
        int32_t x, y, a, me;
        if (!(i % SY_CTL)) {                     /* control rate: glide, swoop, LFO, pitches, the filter */
            int32_t p256, cut, lfo;
            if (v->note256 != v->target256)
                v->note256 += ((v->target256 - v->note256) * GLIDE_K) >> 9;
            if (v->pitch_env)
                v->pitch_env = (int32_t)(((uint32_t)v->pitch_env * 63000u) >> 16);   /* ~25 ms falls */
            v->lfo += 0x00C00000u;                                /* ~1 Hz at the control rate */
            lfo = dsp_sin(v->lfo);
            p256 = v->note256 - ((v->pitch_env >> 4) * 7 * 256 >> 12);           /* hoover: a fifth below */
            inc[0] = dsp_note_inc(p256);
            if (wave == OSC_SUPER || hoover) {
                for (k = 1; k < 5; k++) {
                    static const int8_t SPREAD[5] = {0, -2, -1, 1, 2};
                    inc[k] = dsp_note_inc(p256 + SPREAD[k] * spread / 2);
                }
            } else {
                inc[1] = dsp_note_inc(p256 + spread / 4 + ((lfo * spread) >> 17));   /* drifting second osc */
            }
            inc[5] = inc[0] >> 1;                                 /* the sub an octave down */
            pw = 0x80000000u + (uint32_t)(lfo * 50000);           /* PWM: +-~20 % */
            me = v->mod.level;
            cut = 24 * 256 + (int32_t)t->p[PR_CUTOFF] * 205 + ((v->note256 - 60 * 256) >> 1)
                  + ((((int32_t)t->p[PR_ENVMOD] * 121) * (me >> 4)) >> 12);
            svf_set(&v->f, cut, t->p[PR_RESO]);
        }
        a = adsr_tick(&v->amp);
        adsr_tick(&v->mod);
        for (k = 0; k < 6; k++)
            v->ph[k] += inc[k];
        switch (wave) {
        case OSC_SQUARE:
            x = (dsp_saw(v->ph[0], inc[0]) - dsp_saw(v->ph[0] + 0x80000000u, inc[0])) >> 1;
            break;
        case OSC_SUPER:
            x = 0;
            for (k = 0; k < nsaw; k++)
                x += dsp_saw(v->ph[k], inc[k]);
            x = nsaw == 5u ? (x * 13) >> 6 : (x * 21) >> 6;       /* five saws at ~0.2 each (or three at ~0.33) */
            break;
        case OSC_PWM:
            x = (dsp_saw(v->ph[0], inc[0]) - dsp_saw(v->ph[0] + pw, inc[0])) >> 1;
            break;
        case OSC_SINE:
            x = dsp_sin(v->ph[0]);
            break;
        default:
            x = dsp_saw(v->ph[0], inc[0]);
        }
        if (wave != OSC_SUPER && !hoover && spread)               /* the second oscillator */
            x = (x + (wave == OSC_SINE ? dsp_sin(v->ph[1]) : dsp_saw(v->ph[1], inc[1]))) >> 1;
        if (hoover) {
            int32_t s = 0;
            for (k = 0; k < nsaw; k++)
                s += dsp_saw(v->ph[k], inc[k]);
            x = ((s * (nsaw == 5u ? 11 : 18)) >> 6) + (dsp_saw(v->ph[5], inc[5]) >> 1);
        }
        y = svf_run(&v->f, x, (int)mode);
        y = dsp_tanh((clampi(y, -32767, 32767) * (drive >> 2)) >> 10);
        out[i] += env_apply(gain15(gain15(y, v->vel), gain), a);
    }
}
