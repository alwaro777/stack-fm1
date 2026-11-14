/* SPDX-License-Identifier: GPL-3.0-or-later
 * STACK — the voice pool for the polyphonic engines (the VA synth, FM): 16 voices, up to 4 a track.
 *
 * A note on a chord track becomes several voices (its intervals), all keyed to the step's note so one note-off ends
 * them together. Allocation is deterministic (the same pattern always takes the same voices):
 *   1. a free voice, lowest index first
 *   2. the track already holds its maximum: its oldest released voice, else its oldest
 *   3. the oldest released voice of any track
 *   4. the oldest voice of all
 * A mono model (a lead, the reese, FM bass) keeps one voice and retriggers or glides it. */
#include "engine.h"

pool_t g_pool[NPOOL];
static uint32_t pool_clock;

const char *const CHORD_NAME[] = {"OFF", "MEMORY", "MAJ", "MIN", "MIN7", "MAJ7", "DOM7", "SUS4", "FIFTHS", "MIN9",
                                  "OCTAVE"};
static const int8_t CHORDS[][4] = {
    {0, -128, -128, -128}, {0, -128, -128, -128},          /* (OFF; MEMORY comes from the track) */
    {0, 4, 7, -128}, {0, 3, 7, -128}, {0, 3, 7, 10}, {0, 4, 7, 11}, {0, 4, 7, 10}, {0, 5, 7, -128},
    {0, 7, 12, -128}, {0, 3, 10, 14}, {0, 12, -128, -128},
};
const uint32_t NCHORDS = sizeof CHORDS / sizeof CHORDS[0];

static uint32_t max_poly(uint32_t engine, uint32_t model)
{
    if (engine == ENGINE_SYNTH)
        return model == SY_MONO || model == SY_HOOVER ? 1u : POLY_MAX;
    if (engine == ENGINE_FM)
        return model == FMM_BASS ? 1u : POLY_MAX;
    return 1u;
}

uint32_t chord_of(const track_t *t, int8_t iv[4])
{
    uint32_t c = t->p[PR_CHORD] < NCHORDS ? t->p[PR_CHORD] : 0u, i, n = 0;
    const int8_t *src = c == 1u ? t->chord : CHORDS[c];
    for (i = 0; i < 4u; i++)
        if (src[i] != -128)
            iv[n++] = src[i];
    if (!n)
        iv[n++] = 0;
    return n;
}

void pool_init(void)
{
    uint32_t i;
    for (i = 0; i < NPOOL; i++)
        g_pool[i].used = 0;
    pool_clock = 0;
}

static int pick(uint32_t track, uint32_t maxp)
{
    int i, best = -1, count = 0;
    for (i = 0; i < NPOOL; i++)
        count += g_pool[i].used && g_pool[i].track == track;
    if ((uint32_t)count < maxp)
        for (i = 0; i < NPOOL; i++)
            if (!g_pool[i].used)
                return i;
    if ((uint32_t)count >= maxp) {               /* the track at its limit: its own oldest, released first */
        for (i = 0; i < NPOOL; i++)
            if (g_pool[i].used && g_pool[i].track == track && !g_pool[i].gate &&
                (best < 0 || g_pool[i].age < g_pool[best].age))
                best = i;
        if (best >= 0)
            return best;
        for (i = 0; i < NPOOL; i++)
            if (g_pool[i].used && g_pool[i].track == track && (best < 0 || g_pool[i].age < g_pool[best].age))
                best = i;
        return best;
    }
    for (i = 0; i < NPOOL; i++)                  /* anyone's oldest released voice */
        if (!g_pool[i].gate && (best < 0 || g_pool[i].age < g_pool[best].age))
            best = i;
    if (best >= 0)
        return best;
    for (i = 0; i < NPOOL; i++)                  /* anyone's oldest */
        if (best < 0 || g_pool[i].age < g_pool[best].age)
            best = i;
    return best;
}

static void start(pool_t *p, const track_t *t, uint32_t note, uint32_t vel, int legato)
{
    int32_t n256 = (int32_t)note << 8;
    if (t->engine == ENGINE_FM)
        fm_start(&p->v, t, n256, vel);
    else
        synth_start(&p->v, t, n256, vel, legato);
}

void pool_note_on(uint32_t track, const track_t *t, uint32_t note, uint32_t vel, uint32_t flags, int legato)
{
    uint32_t maxp = max_poly(t->engine, t->model), n, i;
    int8_t iv[4];
    (void)flags;
    if (maxp == 1u) {                            /* mono: one voice, glided or retriggered */
        int idx = -1;
        for (i = 0; i < NPOOL; i++)
            if (g_pool[i].used && g_pool[i].track == track)
                idx = (int)i;
        if (idx < 0)
            idx = pick(track, 1u);
        else
            legato = legato && g_pool[idx].gate;
        g_pool[idx].used = 1;
        g_pool[idx].track = (uint8_t)track;
        g_pool[idx].note = g_pool[idx].key = (uint8_t)note;
        g_pool[idx].gate = 1;
        g_pool[idx].engine = t->engine;
        g_pool[idx].model = t->model;
        g_pool[idx].age = ++pool_clock;
        start(&g_pool[idx], t, note, vel, legato);
        return;
    }
    n = chord_of(t, iv);
    if (n > maxp)
        n = maxp;
    for (i = 0; i < n; i++) {
        int32_t nn = (int32_t)note + iv[i];
        int idx;
        if (nn < 0 || nn > 127)
            continue;
        idx = pick(track, maxp);
        g_pool[idx].used = 1;
        g_pool[idx].track = (uint8_t)track;
        g_pool[idx].note = (uint8_t)nn;
        g_pool[idx].key = (uint8_t)note;
        g_pool[idx].gate = 1;
        g_pool[idx].engine = t->engine;
        g_pool[idx].model = t->model;
        g_pool[idx].age = ++pool_clock;
        start(&g_pool[idx], t, (uint32_t)nn, vel, 0);
    }
}

void pool_note_off(uint32_t track, uint32_t note)
{
    uint32_t i;
    for (i = 0; i < NPOOL; i++) {
        pool_t *p = &g_pool[i];
        if (p->used && p->gate && p->track == track && (note == 255u || p->key == note)) {
            p->gate = 0;
            adsr_release(&p->v.amp);
            adsr_release(&p->v.mod);
        }
    }
}

int pool_render(uint32_t track, const track_t *t, int32_t *out, uint32_t n)
{
    uint32_t i;
    int any = 0;
    for (i = 0; i < NPOOL; i++) {
        pool_t *p = &g_pool[i];
        if (!p->used || p->track != track)
            continue;
        if (p->engine == ENGINE_FM)
            fm_render(&p->v, t, out, n);
        else
            synth_render(&p->v, t, out, n);
        any = 1;
        if (p->v.amp.stage == ADSR_OFF)
            p->used = 0;
    }
    return any;
}

uint32_t pool_active(uint32_t track)
{
    uint32_t i, c = 0;
    for (i = 0; i < NPOOL; i++)
        c += g_pool[i].used && g_pool[i].track == track;
    return c;
}
