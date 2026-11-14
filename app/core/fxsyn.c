/* SPDX-License-Identifier: GPL-3.0-or-later
 * STACK — the FX engine: the gestures a rave set needs between the drums. One voice per track, held by the gate.
 * DECAY is the gesture's length (2 ms .. 8 s); the amplitude follows ATTACK SUSTAIN RELEASE.
 *   RISER    noise through a band-pass rising four octaves, with a rising saw under it (MOD), louder as it goes
 *   DOWN     the reverse: bright and loud falling into the floor
 *   SIREN    the dub siren: a square (or sine, WAVE) swept by a triangle LFO (TONE = speed, MOD = depth)
 *   LASER    a pitch falling four octaves (fast: a laser; slow: a bomb)
 *   SWEEP    resonant filtered noise up and down over the length
 *   IMPACT   a sub boom falling in pitch with a burst of noise on top, driven
 *   VINYL    crackle (TONE = density) over hiss, as long as the gate */
#include "engine.h"

#define FX_CTL 32u

void fx_trigger(fxvoice_t *v, const track_t *t, uint32_t note, uint32_t vel)
{
    v->model = t->model;
    v->active = 1;
    v->gate = 1;
    v->t = 0;
    v->len = (uint32_t)TAB_DECAY_MS[t->p[PR_DECAY] & 127] * 44u + 1u;
    v->note256 = ((int32_t)note << 8) + ((int32_t)t->p[PR_TUNE] - 64) * 96;
    v->vel = (int32_t)vel * 258;
    v->ph[0] = v->ph[1] = 0;
    v->f.ic1 = v->f.ic2 = 0;
    v->z[0] = v->z[1] = 0;
    if (!v->rnd)
        v->rnd = 0x1F123BB5u;
    adsr_gate(&v->amp, t->p[PR_ATTACK], 127, t->p[PR_SUSTAIN] ? t->p[PR_SUSTAIN] : 127, t->p[PR_RELEASE]);
    v->amp.level = 0;
    if (v->model == FXM_LASER || v->model == FXM_IMPACT)  /* one-shots: the gate does not hold them */
        adsr_gate(&v->amp, 0, t->p[PR_DECAY], 0, t->p[PR_DECAY]);
}

void fx_release(fxvoice_t *v)
{
    v->gate = 0;
    if (v->model != FXM_LASER && v->model != FXM_IMPACT)
        adsr_release(&v->amp);
}

void fx_render(fxvoice_t *v, const track_t *t, int32_t *out, uint32_t n)
{
    uint32_t i;
    int32_t pr = 0, inc = 0, mod = t->p[PR_MOD];
    int32_t reso = t->p[PR_RESO], drive = 4096 + (int32_t)t->p[PR_DRIVE] * 120;
    if (!v->active)
        return;
    for (i = 0; i < n; i++) {
        int32_t x = 0, a, nz;
        if (!(i % FX_CTL)) {                     /* control rate: where the gesture is (pr, Q12), the filter, pitch */
            int32_t cut = 60 * 256, p256 = v->note256;
            pr = (int32_t)((v->t < v->len ? v->t : v->len) / ((v->len >> 12) + 1u));
            pr = clampi(pr, 0, 4096);
            switch (v->model) {
            case FXM_RISER: cut = 50 * 256 + pr * 3; p256 += ((pr - 4096) * 24 * 256) >> 12; break;   /* filter +4 oct */
            case FXM_DOWN: cut = 110 * 256 - pr * 15 / 4; p256 += -(pr * 24 * 256 >> 12); break;   /* filter -5 oct */
            case FXM_SWEEP: {
                int32_t tri = pr < 2048 ? pr * 2 : (4096 - pr) * 2;
                cut = 45 * 256 + tri * 15 / 8;                      /* up 5 octaves and back */
                break;
            }
            case FXM_SIREN: {
                uint32_t lfo_inc = 0x00200000u + (uint32_t)t->p[PR_TONE] * 0x00060000u;   /* ~0.3 .. 12 Hz */
                int32_t tri;
                v->ph[1] += lfo_inc;
                tri = (int32_t)(v->ph[1] >> 16) - 32768;
                tri = tri < 0 ? -tri : tri;                       /* 0 .. 32768 */
                p256 += ((tri - 16384) >> 6) * mod * 3 >> 5;     /* +-12 semitones at full depth */
                break;
            }
            case FXM_LASER: p256 += ((4096 - pr) * ((4096 - pr) >> 6) >> 6) * 48 * 256 >> 12; break;   /* 4 oct */
            case FXM_IMPACT: p256 = 28 * 256 + (((4096 - pr) >> 4) * ((4096 - pr) >> 4) >> 4) * 24 * 256 / 4096; break;
            case FXM_VINYL: cut = 100 * 256; break;
            }
            inc = (int32_t)dsp_note_inc(p256);
            svf_set(&v->f, cut, v->model == FXM_SWEEP ? reso : reso >> 1);
        }
        a = adsr_tick(&v->amp);
        nz = dsp_noise(&v->rnd);
        v->ph[0] += (uint32_t)inc;
        switch (v->model) {
        case FXM_RISER:
            x = svf_run(&v->f, nz, SVF_BP) * 2 + ((dsp_saw(v->ph[0], (uint32_t)inc) * mod) >> 8);
            x = (x * (pr >> 1)) >> 11;                            /* louder as it rises */
            break;
        case FXM_DOWN:
            x = svf_run(&v->f, nz, SVF_LP) + ((dsp_saw(v->ph[0], (uint32_t)inc) * mod) >> 8);
            x = (x * ((4096 - pr) >> 1)) >> 11;
            break;
        case FXM_SWEEP:
            x = svf_run(&v->f, nz >> 1, SVF_BP) * 2;
            break;
        case FXM_SIREN:
            x = t->p[PR_WAVE] >= 64 ? dsp_sin(v->ph[0])
                                    : (dsp_saw(v->ph[0], (uint32_t)inc) - dsp_saw(v->ph[0] + 0x80000000u, (uint32_t)inc)) >> 1;
            break;
        case FXM_LASER:
            x = (dsp_sin(v->ph[0]) + ((int32_t)(v->ph[0] >> 31) * 16000 - 8000)) >> 1;
            break;
        case FXM_IMPACT:
            x = dsp_sin(v->ph[0]);
            if (v->t < 1300u)                                     /* the hit: 30 ms of noise */
                x += svf_run(&v->f, nz, SVF_LP) * (int32_t)(1300u - v->t) / 1300;
            break;
        case FXM_VINYL: {
            uint32_t r = (uint32_t)nz & 0xFFFFu;
            int32_t dens = 2 + (int32_t)t->p[PR_TONE] / 4;
            x = dsp_lp1(&v->z[0], nz, 4000) >> 3;                 /* the hiss */
            if (r < (uint32_t)dens)                               /* a crackle */
                v->z[1] = (nz & 1) ? 30000 : -30000;
            x += v->z[1];
            v->z[1] = (v->z[1] * 7) >> 3;                         /* each click a few samples long */
            break;
        }
        }
        x = dsp_tanh((clampi(x, -32767, 32767) * (drive >> 2)) >> 10);
        out[i] += env_apply(gain15(x, v->vel), a);
        v->t++;
    }
    if (v->amp.stage == ADSR_OFF || ((v->model == FXM_LASER || v->model == FXM_IMPACT) && v->amp.level == 0 &&
                                     v->amp.stage != ADSR_ATTACK))
        v->active = 0;
}
