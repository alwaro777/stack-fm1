/* SPDX-License-Identifier: GPL-3.0-or-later
 * STACK — the pattern bank, parameter locks and scales.
 *
 * The current pattern lives in g_proj (the tracks' steps and sequencer settings, g_proj.lock); the other fifteen in
 * g_bank. A switch swaps one track at a time (pattern_swap_track): the sequencer does it as each track reaches the
 * new pattern's first step, so every track changes on its own grid, exactly at the boundary. */
#include "core.h"
#include "engine.h"

bank_t g_bank[NPATTERNS] STK_BIG;

static const uint8_t SCALE_TICKS_P[NSCALES] = {12, 24, 48, 96, 16, 32};

void step_default(step_t *s)
{
    s->flags = 0;
    s->note = 60;
    s->vel = 100;
    s->gate = 12;
    s->prob = 100;
    s->ratchet = 1;
    s->micro = 0;
    s->cond = COND_ALWAYS;
}

void bank_clear(bank_t *b)
{
    uint32_t t, i;
    for (t = 0; t < NTRACKS; t++) {
        for (i = 0; i < NSTEPS; i++)
            step_default(&b->step[t][i]);
        b->len[t] = 16;
        b->scale[t] = SCALE_16;
        b->dir[t] = DIR_FWD;
    }
    for (i = 0; i < NLOCKS; i++)
        b->lock[i].trk = LOCK_NONE;
}

void locks_clear(plock_t *l)
{
    uint32_t i;
    for (i = 0; i < NLOCKS; i++)
        l[i].trk = LOCK_NONE;
}

/* track t of the working pattern into bank b, and bank b2's track t into the working pattern (b == b2: a load) */
static void store_track(uint32_t t, bank_t *b)
{
    track_t *w = &g_proj.pat.trk[t];
    uint32_t i, k = 0;
    for (i = 0; i < NSTEPS; i++)
        b->step[t][i] = w->step[i];
    b->len[t] = w->len;
    b->scale[t] = w->scale;
    b->dir[t] = w->dir;
    for (i = 0; i < NLOCKS; i++)                   /* this track's locks: drop the bank's, copy the working ones */
        if (b->lock[i].trk == t)
            b->lock[i].trk = LOCK_NONE;
    for (i = 0; i < NLOCKS; i++) {
        if (g_proj.lock[i].trk != t)
            continue;
        while (k < NLOCKS && b->lock[k].trk != LOCK_NONE)
            k++;
        if (k < NLOCKS)
            b->lock[k] = g_proj.lock[i];
    }
}

static void load_track(uint32_t t, const bank_t *b)
{
    track_t *w = &g_proj.pat.trk[t];
    uint32_t i, k = 0;
    for (i = 0; i < NSTEPS; i++)
        w->step[i] = b->step[t][i];
    w->len = b->len[t] ? b->len[t] : 16;
    w->scale = b->scale[t] < NSCALES ? b->scale[t] : SCALE_16;
    w->dir = b->dir[t] < NDIRS ? b->dir[t] : DIR_FWD;
    for (i = 0; i < NLOCKS; i++)
        if (g_proj.lock[i].trk == t)
            g_proj.lock[i].trk = LOCK_NONE;
    for (i = 0; i < NLOCKS; i++) {
        if (b->lock[i].trk != t)
            continue;
        while (k < NLOCKS && g_proj.lock[k].trk != LOCK_NONE)
            k++;
        if (k < NLOCKS)
            g_proj.lock[k] = b->lock[i];
    }
}

void pattern_swap_track(uint32_t t, uint32_t from, uint32_t to)
{
    store_track(t, &g_bank[from & 15u]);
    load_track(t, &g_bank[to & 15u]);
}

void pattern_copy_out(bank_t *b)                   /* the working pattern into b (a clipboard): reads only */
{
    uint32_t t;
    locks_clear(b->lock);
    for (t = 0; t < NTRACKS; t++)
        store_track(t, b);
}

void pattern_copy_in(const bank_t *b)              /* b into the working pattern (a paste) */
{
    uint32_t t;
    for (t = 0; t < NTRACKS; t++)
        load_track(t, b);
}

void pattern_load_current(void)                    /* after a load from the flash: the bank's current pattern in */
{
    uint32_t t;
    for (t = 0; t < NTRACKS; t++)
        load_track(t, &g_bank[g_proj.cur_pat & 15u]);
}

void pattern_store_all(void)                       /* the working pattern into its bank slot (before a save) */
{
    uint32_t t;
    for (t = 0; t < NTRACKS; t++)
        store_track(t, &g_bank[g_proj.cur_pat & 15u]);
}

void pattern_switch_now(uint32_t to)               /* stopped: every track at once */
{
    uint32_t t;
    for (t = 0; t < NTRACKS; t++)
        pattern_swap_track(t, g_proj.cur_pat, to);
    g_proj.cur_pat = (uint8_t)(to & 15u);
}

/* a pattern's length in ticks: its longest track (the boundary where the next one starts) */
uint32_t bank_ticks(const bank_t *b)
{
    uint32_t t, m = 0;
    for (t = 0; t < NTRACKS; t++) {
        uint32_t l = (uint32_t)(b->len[t] ? b->len[t] : 16) * SCALE_TICKS_P[b->scale[t] < NSCALES ? b->scale[t] : 1];
        if (l > m)
            m = l;
    }
    return m ? m : 384u;
}

uint32_t pattern_ticks(void)                       /* the working pattern's */
{
    uint32_t t, m = 0;
    for (t = 0; t < NTRACKS; t++) {
        const track_t *w = &g_proj.pat.trk[t];
        uint32_t l = (uint32_t)(w->len ? w->len : 16) * SCALE_TICKS_P[w->scale < NSCALES ? w->scale : 1];
        if (l > m)
            m = l;
    }
    return m ? m : 384u;
}

int bank_used(uint32_t p)                          /* does pattern p hold any step (for the pattern page) */
{
    uint32_t t, i;
    if ((p & 15u) == g_proj.cur_pat) {
        for (t = 0; t < NTRACKS; t++)
            for (i = 0; i < NSTEPS; i++)
                if (g_proj.pat.trk[t].step[i].flags & STEP_ON)
                    return 1;
        return 0;
    }
    for (t = 0; t < NTRACKS; t++)
        for (i = 0; i < NSTEPS; i++)
            if (g_bank[p & 15u].step[t][i].flags & STEP_ON)
                return 1;
    return 0;
}

/* ------------------------------------------------------------------ locks --- */
int lock_get(uint32_t t, uint32_t step, uint32_t param)   /* the value, or -1 */
{
    uint32_t i;
    for (i = 0; i < NLOCKS; i++)
        if (g_proj.lock[i].trk == t && g_proj.lock[i].step == step && g_proj.lock[i].param == param)
            return g_proj.lock[i].value;
    return -1;
}

int lock_set(uint32_t t, uint32_t step, uint32_t param, uint32_t value)   /* 0 = the bank is full */
{
    uint32_t i;
    int free_slot = -1;
    for (i = 0; i < NLOCKS; i++) {
        if (g_proj.lock[i].trk == t && g_proj.lock[i].step == step && g_proj.lock[i].param == param) {
            g_proj.lock[i].value = (uint8_t)value;
            return 1;
        }
        if (g_proj.lock[i].trk == LOCK_NONE && free_slot < 0)
            free_slot = (int)i;
    }
    if (free_slot < 0)
        return 0;
    g_proj.lock[free_slot].step = (uint8_t)step;
    g_proj.lock[free_slot].param = (uint8_t)param;
    g_proj.lock[free_slot].value = (uint8_t)value;
    __asm__ volatile("" ::: "memory");             /* (the audio may read the slot: its track goes in last) */
    g_proj.lock[free_slot].trk = (uint8_t)t;
    return 1;
}

uint32_t lock_clear_step(uint32_t t, uint32_t step)
{
    uint32_t i, n = 0;
    for (i = 0; i < NLOCKS; i++)
        if (g_proj.lock[i].trk == t && g_proj.lock[i].step == step) {
            g_proj.lock[i].trk = LOCK_NONE;
            n++;
        }
    return n;
}

int step_has_locks(uint32_t t, uint32_t step)
{
    uint32_t i;
    for (i = 0; i < NLOCKS; i++)
        if (g_proj.lock[i].trk == t && g_proj.lock[i].step == step)
            return 1;
    return 0;
}

/* ----------------------------------------------------------------- scales --- */
const char *const SCALE_NAME[NSCALE_TYPES] = {"CHROMATIC", "MAJOR", "MINOR", "DORIAN", "PHRYGIAN", "MIXOLYDIAN",
                                              "HARM MIN", "PENTA MIN", "PENTA MAJ", "BLUES"};
/* bit n: semitone n above the tonic is in the scale */
static const uint16_t SCALE_MASK[NSCALE_TYPES] = {0xFFF, 0xAB5, 0x5AD, 0x6AD, 0x5AB, 0x6B5, 0x9AD, 0x4A9, 0x295,
                                                  0x4E9};

int scale_has(int note)
{
    int d = ((note - (int)g_proj.tonic) % 12 + 12) % 12;
    return (SCALE_MASK[g_proj.scale < NSCALE_TYPES ? g_proj.scale : 0] >> d) & 1;
}

int scale_snap(int note)                           /* the nearest note of the scale, down first */
{
    int k;
    for (k = 0; k < 12; k++) {
        if (scale_has(note - k))
            return note - k;
        if (scale_has(note + k))
            return note + k;
    }
    return note;
}

int scale_step(int note, int d)                    /* d scale steps up or down from note (snapped first) */
{
    note = scale_snap(note);
    while (d > 0) {
        do note++; while (note < 127 && !scale_has(note));
        d--;
    }
    while (d < 0) {
        do note--; while (note > 0 && !scale_has(note));
        d++;
    }
    return clampi(note, 0, 127);
}
