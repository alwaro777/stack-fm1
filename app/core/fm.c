/* SPDX-License-Identifier: GPL-3.0-or-later
 * STACK — FM: e-piano, bells, mallets, FM bass. Pool voices (voices.c). Our own small FM, not the closed DX7 binary:
 * two operator pairs (a modulator into a carrier, each), the first modulator with feedback, summed.
 *   pair A   the body: ratio per model, index = MOD x the index envelope (falls with DECAY, faster than the sound)
 *   pair B   the colour: the e-piano's tine, the bell's upper partial, the mallet's knock; level = TONE; DETUNE
 *            pulls it off pitch for a chorus
 *   ADSR     the amplitude (ATTACK DECAY SUSTAIN RELEASE)
 * Phase modulation in uint32 phase units: a modulator's Q15 output times the index (< 2^14) fits 32 bits, then x8 in
 * unsigned arithmetic, which wraps exactly as a phase should: about +-6 radians at full index. */
#include "engine.h"

typedef struct {
    uint8_t car_a, mod_a, car_b, mod_b;   /* frequency ratios, Q4 (16 = 1:1) */
    uint8_t b_index;                      /* pair B's index as a fraction of A's, /256 */
    int8_t mod_decay;                     /* the index envelope's decay, relative to DECAY */
    uint8_t feedback;                     /* pair A's modulator on itself, /256 */
} fmdef_t;

static const fmdef_t FM_DEF[NFMMODELS] = {
    {16, 16, 16, 224, 90, -22, 0},        /* e-piano: 1:1 body, a 1:14 tine that dies fast */
    {16, 56, 32, 112, 160, 0, 0},         /* bell: 1:3.5 inharmonic, an upper partial on 2:7 */
    {16, 64, 48, 16, 70, -30, 0},         /* mallet: 1:4 knock, a 3:1 overtone */
    {16, 16, 8, 16, 60, -12, 110},        /* FM bass: 1:1 with feedback, a sub on 1/2 */
};

void fm_start(pvoice_t *v, const track_t *t, int32_t note256, uint32_t vel)
{
    const fmdef_t *d = &FM_DEF[t->model < NFMMODELS ? t->model : 0];
    int32_t md = clampi((int32_t)t->p[PR_DECAY] + d->mod_decay, 0, 127);
    v->note256 = v->target256 = note256 + ((int32_t)t->p[PR_TUNE] - 64) * 96;
    v->vel = (int32_t)vel * 258;
    v->ph[0] = v->ph[1] = v->ph[2] = v->ph[3] = 0;
    v->fb = 0;
    adsr_gate(&v->amp, t->p[PR_ATTACK], t->p[PR_DECAY], t->p[PR_SUSTAIN], t->p[PR_RELEASE]);
    adsr_gate(&v->mod, 0, (uint32_t)md, 0, t->p[PR_RELEASE]);
    v->amp.level = v->mod.level = 0;
}

void fm_render(pvoice_t *v, const track_t *t, int32_t *out, uint32_t n)
{
    const fmdef_t *d = &FM_DEF[t->model < NFMMODELS ? t->model : 0];
    uint32_t base = dsp_note_inc(v->note256) >> 4;
    uint32_t base_b = dsp_note_inc(v->note256 + (int32_t)t->p[PR_DETUNE] / 2) >> 4;   /* up to ~25 cents */
    uint32_t inc_ca = base * d->car_a, inc_ma = base * d->mod_a;
    uint32_t inc_cb = base_b * d->car_b, inc_mb = base_b * d->mod_b;
    int32_t idx_max = (int32_t)t->p[PR_MOD] * 129;             /* < 2^14 */
    int32_t lvl_b = (int32_t)t->p[PR_TONE] * 258;
    int32_t drive = 4096 + (int32_t)t->p[PR_DRIVE] * 120;
    uint32_t i;
    for (i = 0; i < n; i++) {
        int32_t a = adsr_tick(&v->amp), me = adsr_tick(&v->mod), ia, ib, ma, ca, mb, cb, x;
        ia = (idx_max * (me >> 4)) >> 12;                       /* < 2^14 */
        ib = (ia * d->b_index) >> 8;
        v->ph[0] += inc_ma;
        v->ph[1] += inc_ca;
        v->ph[2] += inc_mb;
        v->ph[3] += inc_cb;
        ma = dsp_sin(v->ph[0] + ((uint32_t)(v->fb * d->feedback) << 8));   /* up to ~1.3 rad */
        v->fb = ma;
        ca = dsp_sin(v->ph[1] + ((uint32_t)(ma * ia) << 3));
        mb = dsp_sin(v->ph[2]);
        cb = dsp_sin(v->ph[3] + ((uint32_t)(mb * ib) << 3));
        x = ((ca * 3) >> 2) + gain15(cb, lvl_b) / 2;
        x = dsp_tanh((clampi(x, -32767, 32767) * (drive >> 2)) >> 10);
        out[i] += env_apply(gain15(gain15(x, v->vel), 21000), a);
    }
}
