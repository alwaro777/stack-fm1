/* SPDX-License-Identifier: GPL-3.0-or-later
 * STACK — F1 audio core: a 4-voice test voice (sine + soft square) proving the audio path end to end.
 * The real engines (drums, acid, FM...) replace it from Phase 2; the event ring and the fixed-point conventions stay.
 *
 * Fixed point used throughout core/:
 *   phase   uint32, a full cycle = 2^32 (wraps for free)
 *   sample  Q15 in int32 (±32767)
 *   gain    Q15 (32768 = 1.0); envelope Q24 in uint32 for slow, smooth ramps
 *   output  signed 24-bit (Q23) in int32, as fm1-base's app_render expects */
#include "core.h"

/* ---------------------------------------------------------------- tables --- */
/* 256-point sine, Q15, plus a guard point for interpolation. Generated once at init (integer only: a parabolic
 * approximation refined by one Bhaskara-style correction is enough for a test tone; Phase 4 swaps in a
 * precomputed table). */
static int16_t sine_tab[257];

static int32_t sine_q15_from_index(uint32_t i)    /* i: 0..255 over a full cycle */
{
    /* Bhaskara I approximation of sin(x) on [0, pi]: 16x(pi - x) / (5 pi^2 - 4x(pi - x)), in integers with
     * x mapped to 0..128 for the half cycle. Max error ~0.0016: fine for a test tone. */
    uint32_t h = i & 127u;                         /* position in the half cycle, 0..127 */
    int64_t p = (int64_t)h * (int64_t)(128 - h);   /* x(pi - x), scaled */
    int64_t num = 16 * p;
    int64_t den = 5 * 128 * 128 - 4 * p;
    int32_t v = (int32_t)((num * 32767) / den);
    return i < 128u ? v : -v;
}

/* MIDI note -> phase increment per sample (2^32 / SR * f). One octave of increments, then shifts. */
static uint32_t note_inc_tab[12];

static uint32_t note_inc(uint32_t note)
{
    int32_t oct = (int32_t)note / 12 - 5;          /* the table holds octave 4 (MIDI 60..71) */
    uint32_t inc = note_inc_tab[note % 12u];
    return oct >= 0 ? inc << oct : inc >> -oct;
}

/* ------------------------------------------------------------------ state --- */
typedef struct {
    uint8_t note, gate;
    uint32_t phase, inc;
    uint32_t env;                /* Q24 amplitude 0..1 */
    int32_t vel_q15;
} tvoice_t;

static stk_evq_t evq;
static tvoice_t tv[STK_TEST_VOICES];
static uint32_t voice_clock;     /* allocation order, for deterministic stealing */
static uint32_t voice_age[STK_TEST_VOICES];
static int32_t gain_q15;         /* MASTER, smoothed per sample */
static volatile uint32_t peak_q15;

void core_init(void)
{
    uint32_t i;
    /* frequencies of MIDI 60..71 (C4..B4) in milli-Hz, so the increments are exact integers to compute */
    static const uint32_t MHZ[12] = {261626, 277183, 293665, 311127, 329628, 349228,
                                     369994, 391995, 415305, 440000, 466164, 493883};
    for (i = 0; i < 256u; i++)
        sine_tab[i] = (int16_t)sine_q15_from_index(i);
    sine_tab[256] = sine_tab[0];
    for (i = 0; i < 12u; i++)    /* inc = f * 2^32 / SR, f in mHz */
        note_inc_tab[i] = (uint32_t)(((uint64_t)MHZ[i] << 32) / ((uint64_t)STK_SR * 1000u));
    for (i = 0; i < STK_TEST_VOICES; i++) {
        tv[i].gate = 0;
        tv[i].env = 0;
        tv[i].phase = 0;
        voice_age[i] = 0;
    }
    evq.head = evq.tail = 0;
    voice_clock = 0;
    gain_q15 = 0;
    peak_q15 = 0;
}

int core_post(uint32_t type, uint32_t note, uint32_t vel)
{
    uint32_t h = evq.head;
    if (h - evq.tail >= STK_EVQ_LEN)
        return 0;
    evq.ev[h & (STK_EVQ_LEN - 1u)] = type << 24 | (note & 0x7Fu) << 16 | (vel & 0x7Fu) << 8;
    __asm__ volatile("" ::: "memory");             /* the slot is written before head moves */
    evq.head = h + 1u;
    return 1;
}

/* deterministic voice choice: a free voice (lowest index); else the voice that started longest ago */
static uint32_t voice_pick(void)
{
    uint32_t i, best = 0;
    for (i = 0; i < STK_TEST_VOICES; i++)
        if (!tv[i].gate && tv[i].env == 0)
            return i;
    for (i = 1; i < STK_TEST_VOICES; i++)
        if (voice_age[i] < voice_age[best])
            best = i;
    return best;
}

static void events(void)
{
    while (evq.tail != evq.head) {
        uint32_t e = evq.ev[evq.tail & (STK_EVQ_LEN - 1u)];
        uint32_t type = e >> 24, note = (e >> 16) & 0x7Fu, vel = (e >> 8) & 0x7Fu, i;
        __asm__ volatile("" ::: "memory");
        evq.tail++;
        if (type == EV_NOTE_ON && vel) {
            i = voice_pick();
            tv[i].note = (uint8_t)note;
            tv[i].gate = 1;
            tv[i].inc = note_inc(note);
            tv[i].vel_q15 = (int32_t)(vel * 258u);  /* 127 -> 32766 */
            voice_age[i] = ++voice_clock;
        } else if (type == EV_NOTE_OFF || (type == EV_NOTE_ON && !vel)) {
            for (i = 0; i < STK_TEST_VOICES; i++)
                if (tv[i].gate && tv[i].note == note)
                    tv[i].gate = 0;
        } else if (type == EV_ALL_OFF) {
            for (i = 0; i < STK_TEST_VOICES; i++)
                tv[i].gate = 0;
        }
    }
}

/* interpolated sine, Q15 */
static inline int32_t sine_q15(uint32_t phase)
{
    uint32_t idx = phase >> 24, frac = (phase >> 8) & 0xFFFFu;
    int32_t a = sine_tab[idx], b = sine_tab[idx + 1u];
    return a + (int32_t)(((int64_t)(b - a) * frac) >> 16);
}

#define ENV_ONE (1u << 24)
#define ENV_ATTACK (ENV_ONE / 220u)                /* ~5 ms linear attack */
#define ENV_RELEASE_SHIFT 10                        /* exponential release, ~23 ms time constant */
#define ENV_FLOOR (1u << 12)                        /* -72 dB: below it the voice is free (no long inaudible tail) */

void core_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    uint32_t f, i;
    int32_t target = (int32_t)(master_q12 > 4096u ? 4096u : master_q12) << 3;   /* Q12 -> Q15 */
    uint32_t pk = peak_q15;
    events();
    for (f = 0; f < n; f++) {
        int32_t mix = 0, out;
        for (i = 0; i < STK_TEST_VOICES; i++) {
            tvoice_t *v = &tv[i];
            int32_t s;
            if (v->gate) {
                v->env = v->env + ENV_ATTACK > ENV_ONE ? ENV_ONE : v->env + ENV_ATTACK;
            } else if (v->env) {
                v->env -= v->env >> ENV_RELEASE_SHIFT;
                if (v->env < ENV_FLOOR)
                    v->env = 0;
            } else {
                continue;
            }
            v->phase += v->inc;
            /* sine plus a little of its octave: a rounder, more audible test tone on a small speaker */
            s = sine_q15(v->phase) + (sine_q15(v->phase << 1) >> 2);
            s = (int32_t)(((int64_t)s * (int32_t)(v->env >> 9)) >> 15);   /* env Q24 -> Q15 */
            s = (s * (v->vel_q15 >> 3)) >> 12;                             /* velocity */
            mix += s >> 1;                                                 /* -6 dB a voice; the clip below catches 4 */
        }
        gain_q15 += (target - gain_q15) >> 8;       /* ~6 ms glide: MASTER never clicks */
        out = (int32_t)(((int64_t)mix * gain_q15) >> 15);
        if (out > 32767)
            out = 32767;
        else if (out < -32767)
            out = -32767;
        if ((uint32_t)(out < 0 ? -out : out) > pk)
            pk = (uint32_t)(out < 0 ? -out : out);
        lr[2u * f] = out * 256;                     /* Q15 -> Q23 (a multiply: << on a negative is undefined in C) */
        lr[2u * f + 1u] = out * 256;
    }
    peak_q15 = pk;
}

void core_panic(void)
{
    uint32_t i;
    for (i = 0; i < STK_TEST_VOICES; i++) {
        tv[i].gate = 0;
        tv[i].env = 0;
    }
}

uint32_t core_voices_active(void)
{
    uint32_t i, c = 0;
    for (i = 0; i < STK_TEST_VOICES; i++)
        c += tv[i].env != 0;
    return c;
}

uint32_t core_peak(void)
{
    uint32_t p = peak_q15;
    peak_q15 = 0;
    return p;
}
