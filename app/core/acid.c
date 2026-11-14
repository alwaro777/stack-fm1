/* SPDX-License-Identifier: GPL-3.0-or-later
 * STACK — the acid bass: one voice, a 303-style line. Fixed point, 32-bit products (dsp.h).
 *   oscillator  saw or square with polyBLEP corners (no harsh aliasing on the high notes), or a pure sine (SUB)
 *   filter      four one-pole low-passes in a loop with resonance; tanh at the loop's input keeps it musical when
 *               it screams; SLOPE >= 64 takes the third pole's output (18 dB) instead of the fourth (24 dB)
 *   envelopes   the filter's falls with DECAY; the amplitude holds while the gate is open (the sequencer's GATE)
 *   accent      louder, a brighter and shorter filter sweep (+ ACCENT)
 *   slide       a step with SLIDE ties into the next note, which glides there (~40 ms) without a new envelope
 * Params: CUTOFF, RESO, ENVMOD, DECAY, ACCENT, DRIVE, WAVE (saw | square | sine), SNAP = SLOPE, TUNE. */
#include "engine.h"

#define ACID_CTL 16u             /* control block: glide, cutoff and resonance step every 16 samples */
#define AMP_NORMAL 47000         /* Q16: an unaccented note sits ~3 dB under an accented one */

void acid_trigger(avoice_t *v, const track_t *t, uint32_t note, uint32_t vel, uint32_t flags, int legato)
{
    int32_t target = ((int32_t)note << 8) + ((int32_t)t->p[PR_TUNE] - 64) * 96;
    uint32_t decay = t->p[PR_DECAY];
    v->target256 = target;
    v->accent = (flags & STEP_ACCENT) != 0;
    v->gate = 1;
    v->amp_target = v->accent ? 65535 : AMP_NORMAL;
    v->amp_target = (int32_t)(((uint32_t)v->amp_target * (vel * 516u)) >> 16);   /* velocity, 127 -> ~1.0 */
    if (!v->rnd)
        v->rnd = 0x2545F491u;
    if (legato && v->active)
        return;                                   /* a slide: keep the envelopes, glide to the new note */
    v->note256 = target;
    v->active = 1;
    v->n = 0;
    denv_start(&v->fenv, ENV_ONE, v->accent && decay > 20u ? decay - 20u : decay);
}

void acid_release(avoice_t *v)
{
    v->gate = 0;
    v->amp_target = 0;
}

void acid_render(avoice_t *v, const track_t *t, int32_t *out, uint32_t n)
{
    uint32_t i, wave = t->p[PR_WAVE] / 43u;      /* 0 saw, 1 square, 2 sine */
    int s18 = t->p[PR_SNAP] >= 64;
    int32_t vel_drive = 4096 + (int32_t)t->p[PR_DRIVE] * 120;   /* Q12, 1x .. ~4.7x */
    if (!v->active)
        return;
    for (i = 0; i < n; i++) {
        int32_t x, u, y, fe;
        uint32_t inc;
        fe = denv_tick(&v->fenv);
        if (!v->n) {                              /* control rate */
            int32_t cut = 24 * 256 + (int32_t)t->p[PR_CUTOFF] * 192;               /* MIDI 24 .. 119 */
            int32_t envamt = (int32_t)t->p[PR_ENVMOD] * 121;                       /* up to 60 semitones */
            cut += (envamt * (fe >> 4)) >> 12;
            if (v->accent)
                cut += (((int32_t)t->p[PR_ACCENT] * 48) * (fe >> 4)) >> 12;        /* up to 24 more */
            v->fcut256 = cut;
            v->g = dsp_onepole_g(cut);
            v->k = (int32_t)t->p[PR_RESO] * 128;                                   /* Q12: 0 .. 3.97 */
            if (v->note256 != v->target256)       /* glide: ~40 ms time constant */
                v->note256 += ((v->target256 - v->note256) * 37) >> 9;
            v->n = ACID_CTL;
        }
        v->n--;
        inc = dsp_note_inc(v->note256);
        v->ph += inc;
        if (wave == 0)
            x = dsp_saw(v->ph, inc);
        else if (wave == 1)
            x = (dsp_saw(v->ph, inc) - dsp_saw(v->ph + 0x80000000u, inc)) >> 1;
        else
            x = dsp_sin(v->ph);
        /* the ladder: the input minus the resonance's feedback, through tanh, then four poles */
        u = dsp_tanh(((x * 26) >> 5) - ((v->k * (clampi(v->z[3], -65535, 65535) >> 2)) >> 10));
        y = dsp_lp1(&v->z[0], u, v->g);
        y = dsp_lp1(&v->z[1], y, v->g);
        y = dsp_lp1(&v->z[2], y, v->g);
        if (s18) {
            v->z[3] = y;                          /* (feedback from the third pole: 18 dB) */
        } else {
            y = dsp_lp1(&v->z[3], y, v->g);
        }
        y = (clampi(y, -65535, 65535) * (4096 + (v->k >> 1))) >> 12;   /* make up the resonance's loss */
        y = dsp_tanh((clampi(y, -32767, 32767) * (vel_drive >> 2)) >> 10);
        /* amplitude: quick attack, ~12 ms release */
        v->amp += (v->amp_target - v->amp) >> (v->amp_target > v->amp ? 6 : 9);
        out[i] += env_apply(y, v->amp);
    }
    if (!v->gate && v->amp < 16)
        v->active = 0;
}
