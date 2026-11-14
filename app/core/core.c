/* SPDX-License-Identifier: GPL-3.0-or-later
 * STACK — the core: the sequencer, one voice per track, the mixer.
 *
 * Timing. A tick is 1/96 of a quarter note; its length in samples is kept in Q16, so the tempo is exact on average
 * and every tick lands within one sample of where it should. The render runs in chunks that end on tick boundaries,
 * so triggers are sample-accurate to the tick.
 *
 * Scheduling. Each track decides its next step half a step early (a look-ahead), which lets a step play early (a
 * negative nudge) and lets swing push off-beats late: the decision makes up to four hits (ratchets) and a note-off,
 * each at its own tick, kept in the track's small queue. Probability and conditions are decided then too.
 *
 * Swing. The odd steps of each track move late by (swing - 50) / 50 of a step: at 50 % they sit on the grid, at 75 %
 * half a step late (the off-beat 16th at 3/4 of its 8th).
 *
 * Fixed point throughout (dsp.h), 32-bit products. */
#include "core.h"
#include "engine.h"

project_t g_proj;
volatile uint32_t core_mute, core_solo;
volatile uint8_t core_fill, core_rec;
volatile uint8_t core_fx_bypass;

/* ------------------------------------------------------------- queues --- */
static struct {
    volatile uint32_t head, tail;
    uint32_t ev[STK_EVQ_LEN];
} evq;

static struct {
    volatile uint32_t head, tail;
    trig_t ev[STK_TRIGQ_LEN];
} trigq;

int core_post(uint32_t type, uint32_t track, uint32_t note, uint32_t vel)
{
    uint32_t h = evq.head;
    if (h - evq.tail >= STK_EVQ_LEN)
        return 0;
    evq.ev[h & (STK_EVQ_LEN - 1u)] = type << 24 | (track & 0xFFu) << 16 | (note & 0x7Fu) << 8 | (vel & 0x7Fu);
    __asm__ volatile("" ::: "memory");             /* the slot is written before head moves */
    evq.head = h + 1u;
    return 1;
}

int core_trig_pop(trig_t *t)
{
    uint32_t tl = trigq.tail;
    if (tl == trigq.head)
        return 0;
    __asm__ volatile("" ::: "memory");
    *t = trigq.ev[tl & (STK_TRIGQ_LEN - 1u)];
    __asm__ volatile("" ::: "memory");
    trigq.tail = tl + 1u;
    return 1;
}

static void trig_push(uint32_t sample, uint32_t track, uint32_t note, uint32_t vel, uint32_t flags)
{
    uint32_t h = trigq.head;
    trig_t *t;
    if (h - trigq.tail >= STK_TRIGQ_LEN)
        return;                                    /* nobody reading: the oldest stay, the newest drop */
    t = &trigq.ev[h & (STK_TRIGQ_LEN - 1u)];
    t->sample = sample;
    t->track = (uint8_t)track;
    t->note = (uint8_t)note;
    t->vel = (uint8_t)vel;
    t->flags = (uint8_t)flags;
    __asm__ volatile("" ::: "memory");
    trigq.head = h + 1u;
}

/* ----------------------------------------------------------- sequencer --- */
enum { QE_POS, QE_OFF, QE_HIT };  /* at the same tick: the playhead moves, an old note ends, then the new one starts */
typedef struct {
    uint32_t tick;
    uint8_t type, note, vel, flags;
    int8_t pos;
} qev_t;
#define TQ_LEN 12u

typedef struct {
    uint32_t next_nominal;       /* the tick the next undecided step sits on (before swing and nudge) */
    uint32_t count;              /* steps decided since PLAY */
    int32_t pos;                 /* the last decided step */
    int32_t pp_dir;              /* ping-pong: +1 or -1 */
    int32_t play_pos;            /* the step sounding (for the UI) */
    uint8_t tied;                /* the last note slides into the next one (acid) */
    uint8_t held;                /* a note is held (acid) */
    uint8_t nq;
    qev_t q[TQ_LEN];             /* pending playhead moves / offs / hits, any order */
} tplay_t;

static const uint8_t SCALE_TICKS[NSCALES] = {12, 24, 48, 96, 16, 32};

static tplay_t tp[NTRACKS];
static voice_t voice[NTRACKS];

/* the sounds as they play: the kit with the current step's parameter locks over it (engines read these) */
static track_t shadow[NTRACKS];
static uint8_t locked[NTRACKS];

/* patterns: where the current one started, how long it is; a switch under way (track by track) */
static uint32_t pat_start, pat_len;
static uint8_t sw_pending, sw_to, sw_from, sw_done, sw_planned;
static uint32_t sw_tick;
static int8_t cue = -1;          /* the pattern the player picked, for the next boundary */
static uint8_t chain_pos, song_row, song_rep, nx_chain_pos, nx_song_row, nx_song_rep;
static uint32_t nx_len;
static uint8_t song_end;         /* the song ran out: stop at the boundary */

/* MIDI: out (core -> platform), the external clock */
#define MIDIQ_LEN 64u
static struct {
    volatile uint32_t head, tail;
    uint32_t pkt[MIDIQ_LEN];
} midiq;
static uint32_t abs_samples;     /* samples since boot, running stopped or not */
static uint32_t ext_last, ext_target;
static int32_t ext_spt;          /* Q16 samples per tick, measured from the clock */
static uint8_t ext_seen;
static uint8_t midi_held[NTRACKS];   /* the note sent last per track (0xFF none) */
static uint32_t rng;
static uint32_t tick_now;        /* ticks since PLAY */
static int32_t tick_rem;         /* Q16 samples until the next tick */
static int32_t spt_q16;          /* Q16 samples per tick */
static uint16_t spt_bpm;         /* the tempo spt_q16 was made for */
static uint32_t sample_now;      /* samples since PLAY */
static uint8_t playing;
static int32_t gain_q15;
static volatile uint32_t peak_q15;

static uint32_t rnd(void)
{
    uint32_t x = rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng = x;
    return x;
}

static void tempo_update(void)
{
    uint32_t b = g_proj.bpm10, q, r;
    if (b == spt_bpm)
        return;
    b = (uint32_t)clampi((int32_t)b, BPM_MIN, BPM_MAX);
    /* samples per tick = 44100 * 60 / (bpm * 96) = 275625 / bpm10, in Q16 without 64-bit arithmetic */
    q = 275625u / b;
    r = 275625u % b;
    spt_q16 = (int32_t)((q << 16) + (r << 16) / b);
    spt_bpm = g_proj.bpm10;
}

static void q_add(tplay_t *p, uint32_t tick, uint32_t type, uint32_t note, uint32_t vel, uint32_t flags, int pos)
{
    qev_t *e;
    if (p->nq >= TQ_LEN)
        return;
    e = &p->q[p->nq++];
    e->tick = tick;
    e->type = (uint8_t)type;
    e->note = (uint8_t)note;
    e->vel = (uint8_t)vel;
    e->flags = (uint8_t)flags;
    e->pos = (int8_t)pos;
}

static int cond_ok(uint32_t cond, uint32_t loop)
{
    static const uint8_t AB[NCONDS][2] = {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0},
                                          {1, 2}, {2, 2}, {1, 3}, {2, 3}, {3, 3}, {1, 4}, {2, 4}, {3, 4}, {4, 4}};
    switch (cond) {
    case COND_ALWAYS: return 1;
    case COND_FILL: return core_fill != 0;
    case COND_NOT_FILL: return core_fill == 0;
    case COND_FIRST: return loop == 0;
    case COND_NOT_FIRST: return loop != 0;
    default:
        if (cond >= NCONDS)
            return 1;
        return loop % AB[cond][1] == (uint32_t)(AB[cond][0] - 1u);
    }
}

static int32_t next_pos(tplay_t *p, const track_t *t)
{
    int32_t len = t->len ? t->len : 1;
    if (p->count == 0)                             /* the first step after PLAY */
        return t->dir == DIR_REV ? len - 1 : t->dir == DIR_RANDOM ? (int32_t)(rnd() % (uint32_t)len) : 0;
    switch (t->dir) {
    case DIR_REV: return p->pos <= 0 || p->pos >= len ? len - 1 : p->pos - 1;
    case DIR_PINGPONG:
        if (len == 1)
            return 0;
        if (p->pos + p->pp_dir >= len || p->pos + p->pp_dir < 0)
            p->pp_dir = -p->pp_dir;
        return clampi(p->pos + p->pp_dir, 0, len - 1);
    case DIR_RANDOM: return (int32_t)(rnd() % (uint32_t)len);
    default: return p->pos + 1 >= len ? 0 : p->pos + 1;
    }
}

static void shadow_sync(uint32_t ti)              /* the kit's sound into the shadow (no lock in force) */
{
    const track_t *t = &g_proj.pat.trk[ti];
    track_t *sh = &shadow[ti];
    uint32_t i;
    sh->preset = t->preset;
    sh->engine = t->engine;
    sh->model = t->model;
    sh->category = t->category;
    sh->midi_ch = t->midi_ch;
    for (i = 0; i < NPARAMS; i++)
        sh->p[i] = t->p[i];
    for (i = 0; i < 4; i++)
        sh->chord[i] = t->chord[i];
}

static void apply_locks(uint32_t ti, uint32_t pos) /* a hit: this step's locks over the kit, until the next hit */
{
    uint32_t i;
    shadow_sync(ti);
    locked[ti] = 0;
    for (i = 0; i < NLOCKS; i++)
        if (g_proj.lock[i].trk == ti && g_proj.lock[i].step == pos && g_proj.lock[i].param < NPARAMS) {
            shadow[ti].p[g_proj.lock[i].param] = g_proj.lock[i].value;
            locked[ti] = 1;
        }
}

/* ------------------------------------------------------------------ MIDI out --- */
static void midi_out(uint32_t status, uint32_t d1, uint32_t d2)
{
    uint32_t h = midiq.head, cin = status >= 0xF0u ? 0x0Fu : status >> 4;
    if (h - midiq.tail >= MIDIQ_LEN)
        return;
    midiq.pkt[h & (MIDIQ_LEN - 1u)] = cin | status << 8 | (d1 & 0x7Fu) << 16 | (d2 & 0x7Fu) << 24;
    __asm__ volatile("" ::: "memory");
    midiq.head = h + 1u;
}

int core_midi_pop(uint32_t *pkt)
{
    uint32_t t = midiq.tail;
    if (t == midiq.head)
        return 0;
    __asm__ volatile("" ::: "memory");
    *pkt = midiq.pkt[t & (MIDIQ_LEN - 1u)];
    midiq.tail = t + 1u;
    return 1;
}

static void midi_note_off_track(uint32_t ti)
{
    uint32_t ch = g_proj.pat.trk[ti].midi_ch;
    if (ch && midi_held[ti] != 0xFFu)
        midi_out(0x80u | (ch - 1u), midi_held[ti], 0);
    midi_held[ti] = 0xFFu;
}

/* ------------------------------------------------------- the next pattern --- */
/* what plays after the current pattern, by the play mode; the positions it implies are committed at the boundary */
static uint32_t plan_next(void)
{
    uint32_t cur = g_proj.cur_pat;
    nx_chain_pos = chain_pos;
    nx_song_row = song_row;
    nx_song_rep = song_rep;
    if (cue >= 0) {
        uint32_t c = (uint32_t)cue;
        cue = -1;
        return c;
    }
    if (g_proj.play_mode == PLAY_CHAIN && g_proj.chain_len) {
        nx_chain_pos = (uint8_t)((chain_pos + 1u) % g_proj.chain_len);
        return g_proj.chain[nx_chain_pos] & 15u;
    }
    if (g_proj.play_mode == PLAY_SONG && g_proj.song_len) {
        const songrow_t *r = &g_proj.song[song_row % NSONG];
        if ((uint32_t)song_rep + 1u < (r->reps ? r->reps : 1u)) {
            nx_song_rep = (uint8_t)(song_rep + 1u);
            return r->pat & 15u;
        }
        nx_song_rep = 0;
        nx_song_row = (uint8_t)(song_row + 1u);
        if (nx_song_row >= g_proj.song_len) {
            song_end = 1;                          /* the song's last bar: stop when it ends */
            nx_song_row = 0;
        }
        return g_proj.song[nx_song_row].pat & 15u;
    }
    return cur;
}

static void transport_stop(void);

/* decide the next step of track ti: its hits, its note-off, its playhead move */
static void decide(uint32_t ti)
{
    tplay_t *p = &tp[ti];
    const track_t *t = &g_proj.pat.trk[ti];
    int32_t spt = SCALE_TICKS[t->scale < NSCALES ? t->scale : SCALE_16];
    int32_t len = t->len ? t->len : 1, pos, off, first, k, nh;
    uint32_t loop = p->count / (uint32_t)len, nominal = p->next_nominal;
    const step_t *s;
    if (sw_pending && p->next_nominal >= sw_tick && !((sw_done >> ti) & 1u)) {
        /* this track's next step is the new pattern's first: swap its steps, start it from step 1, on the boundary */
        pattern_swap_track(ti, sw_from, sw_to);
        p->count = 0;
        p->pos = 0;
        p->pp_dir = 1;
        p->next_nominal = sw_tick;
        sw_done |= (uint8_t)(1u << ti);
        if (sw_done == 0xFFu) {
            g_proj.cur_pat = sw_to;
            sw_pending = 0;
        }
        spt = SCALE_TICKS[t->scale < NSCALES ? t->scale : SCALE_16];
        len = t->len ? t->len : 1;
        loop = 0;
        nominal = p->next_nominal;
    }
    pos = next_pos(p, t);
    p->pos = pos;
    p->count++;
    p->next_nominal += (uint32_t)spt;
    s = &t->step[pos];
    q_add(p, nominal, QE_POS, 0, 0, 0, pos);
    /* swing on odd steps, then the nudge (in 1/24 of a step, kept inside half a step) */
    off = (p->count - 1u) & 1u ? ((int32_t)g_proj.swing - 50) * spt / 50 : 0;
    off += clampi(s->micro, -11, 11) * spt / 24;
    first = (int32_t)nominal + off;
    if (first < (int32_t)tick_now)
        first = (int32_t)tick_now;
    if (!(s->flags & STEP_ON) || !cond_ok(s->cond, loop) || (s->prob < 100 && (int32_t)(rnd() % 100u) >= s->prob)) {
        if (p->tied)                               /* a slide into a rest: the held note ends on the grid */
            q_add(p, (uint32_t)first, QE_OFF, 0, 0, 0, pos);
        p->tied = 0;
        return;
    }
    nh = s->ratchet < 1 ? 1 : s->ratchet > 4 ? 4 : s->ratchet;
    for (k = 0; k < nh; k++)
        q_add(p, (uint32_t)(first + k * spt / nh), QE_HIT, s->note, s->vel ? s->vel : 1, s->flags, pos);
    if (!(s->flags & STEP_SLIDE)) {               /* the gate: from the last hit, in 1/16 of a step */
        int32_t g = s->gate ? s->gate : 1;
        int32_t last = first + (nh - 1) * spt / nh;
        int32_t glen = nh > 1 ? spt / nh * g / 16 : spt * g / 16;
        q_add(p, (uint32_t)(last + (glen > 0 ? glen : 1)), QE_OFF, s->note, 0, 0, pos);
    }
}

static int track_audible(uint32_t ti)
{
    uint32_t solo = core_solo;
    if (solo)
        return (solo >> ti) & 1u;
    return !((core_mute >> ti) & 1u);
}

/* the voices are a union over the engines: a track whose engine changed (a new preset) starts from a clean voice */
static uint8_t voice_engine[NTRACKS];

static void voice_sync(uint32_t ti)
{
    uint32_t e = g_proj.pat.trk[ti].engine, i;
    if (voice_engine[ti] == e)
        return;
    for (i = 0; i < sizeof voice[ti]; i++)
        ((uint8_t *)&voice[ti])[i] = 0;
    for (i = 0; i < NPOOL; i++)                   /* (and the pool voices it held under the old engine) */
        if (g_pool[i].used && g_pool[i].track == ti)
            g_pool[i].used = 0;
    voice_engine[ti] = (uint8_t)e;
    tp[ti].held = tp[ti].tied = 0;
}

static void voice_hit(uint32_t ti, uint32_t note, uint32_t vel, uint32_t flags, int legato)
{
    track_t *t = &shadow[ti];
    uint32_t j;
    voice_sync(ti);
    if (t->engine == ENGINE_ACID) {
        acid_trigger(&voice[ti].a, t, note, vel, flags, legato);
    } else if (t->engine == ENGINE_SYNTH || t->engine == ENGINE_FM) {
        pool_note_on(ti, t, note, vel, flags, legato);
    } else if (t->engine == ENGINE_FX) {
        fx_trigger(&voice[ti].x, t, note, vel);
    } else {
        drum_trigger(&voice[ti].d, t, note, vel, flags);
        if (DM_IS_KICK(t->model))                 /* the kicks drive the pump */
            fx_pump_kick();
        if (t->model == DM_HAT_C)                 /* a closed hat chokes every open one */
            for (j = 0; j < NTRACKS; j++)
                if (g_proj.pat.trk[j].engine == ENGINE_DRUM && g_proj.pat.trk[j].model == DM_HAT_O &&
                    voice[j].d.active)
                    drum_choke(&voice[j].d);
    }
}

static void voice_off(uint32_t ti, uint32_t note)   /* note 255: whatever the track holds */
{
    uint32_t e = g_proj.pat.trk[ti].engine;
    voice_sync(ti);
    if (e == ENGINE_ACID)
        acid_release(&voice[ti].a);
    else if (e == ENGINE_SYNTH || e == ENGINE_FM)
        pool_note_off(ti, note);
    else if (e == ENGINE_FX)
        fx_release(&voice[ti].x);
}

static int voice_active(uint32_t ti)
{
    switch (g_proj.pat.trk[ti].engine) {
    case ENGINE_ACID: return voice[ti].a.active;
    case ENGINE_SYNTH:
    case ENGINE_FM: return pool_active(ti) != 0;
    case ENGINE_FX: return voice[ti].x.active;
    default: return voice[ti].d.active;
    }
}

static void fire(uint32_t ti, const qev_t *e)
{
    tplay_t *p = &tp[ti];
    switch (e->type) {
    case QE_POS:
        p->play_pos = e->pos;
        break;
    case QE_HIT:
        if (!track_audible(ti)) {
            if (p->held)
                voice_off(ti, 255);
            p->held = 0;
            p->tied = (e->flags & STEP_SLIDE) != 0;
            break;
        }
        apply_locks(ti, (uint32_t)e->pos);
        voice_hit(ti, e->note, e->vel, e->flags, p->tied && p->held);
        p->held = 1;
        p->tied = (e->flags & STEP_SLIDE) != 0;
        trig_push(sample_now, ti, e->note, e->vel, e->flags);
        if (g_proj.pat.trk[ti].midi_ch) {          /* and to the outside: the track's MIDI channel */
            midi_note_off_track(ti);
            midi_out(0x90u | (g_proj.pat.trk[ti].midi_ch - 1u), e->note, e->vel);
            midi_held[ti] = e->note;
        }
        break;
    case QE_OFF:
        if (p->held)
            voice_off(ti, e->note ? e->note : 255u);
        p->held = 0;
        midi_note_off_track(ti);
        break;
    }
}

static void seq_tick(void)
{
    uint32_t ti;
    /* the pattern's boundary: plan what follows a beat early, so every track finds it when it looks ahead */
    if (!sw_planned && tick_now + 48u >= pat_start + pat_len) {
        uint32_t nx = plan_next();
        sw_planned = 1;
        nx_len = nx == g_proj.cur_pat ? pattern_ticks() : bank_ticks(&g_bank[nx]);
        if (nx != g_proj.cur_pat) {
            sw_pending = 1;
            sw_from = g_proj.cur_pat;
            sw_to = (uint8_t)nx;
            sw_done = 0;
            sw_tick = pat_start + pat_len;
        }
    }
    if (tick_now >= pat_start + pat_len) {         /* the boundary itself */
        pat_start += pat_len;
        pat_len = nx_len ? nx_len : pattern_ticks();
        chain_pos = nx_chain_pos;
        song_row = nx_song_row;
        song_rep = nx_song_rep;
        sw_planned = 0;
        if (song_end) {
            song_end = 0;
            transport_stop();
            if (g_proj.transport & 1u)
                midi_out(0xFC, 0, 0);
            return;
        }
    }
    if (g_proj.clk_out && !(tick_now & 3u))       /* MIDI clock: 24 a quarter, one every 4 ticks */
        midi_out(0xF8, 0, 0);
    for (ti = 0; ti < NTRACKS; ti++) {
        tplay_t *p = &tp[ti];
        const track_t *t = &g_proj.pat.trk[ti];
        uint32_t half = SCALE_TICKS[t->scale < NSCALES ? t->scale : SCALE_16] / 2u;
        uint32_t i;
        while (tick_now + half >= p->next_nominal)  /* look half a step ahead */
            decide(ti);
        for (;;) {                                 /* fire what is due, in tick order, then by type */
            int best = -1;
            for (i = 0; i < p->nq; i++)
                if (p->q[i].tick <= tick_now &&
                    (best < 0 || p->q[i].tick < p->q[best].tick ||
                     (p->q[i].tick == p->q[best].tick && p->q[i].type < p->q[best].type)))
                    best = (int)i;
            if (best < 0)
                break;
            {
                qev_t e = p->q[best];
                p->q[best] = p->q[--p->nq];
                fire(ti, &e);
            }
        }
    }
    tick_now++;
}

static void transport_play(void)
{
    uint32_t i;
    for (i = 0; i < NTRACKS; i++) {
        if (tp[i].held)
            voice_off(i, 255);
        tp[i].next_nominal = 0;
        tp[i].count = 0;
        tp[i].pos = 0;
        tp[i].pp_dir = 1;
        tp[i].play_pos = -1;
        tp[i].tied = tp[i].held = 0;
        tp[i].nq = 0;
    }
    rng = 0x6C8E9CF5u ^ ((uint32_t)g_proj.seed * 0x9E3779B9u);
    tick_now = 0;
    tick_rem = 0;
    sample_now = 0;
    trigq.tail = trigq.head;                       /* (a fresh start: old triggers are history) */
    /* where to start: the chain's first pattern, the song's first row, or the current pattern */
    chain_pos = song_row = song_rep = 0;
    song_end = 0;
    if (g_proj.play_mode == PLAY_CHAIN && g_proj.chain_len && g_proj.chain[0] != g_proj.cur_pat)
        pattern_switch_now(g_proj.chain[0]);
    else if (g_proj.play_mode == PLAY_SONG && g_proj.song_len && g_proj.song[0].pat != g_proj.cur_pat)
        pattern_switch_now(g_proj.song[0].pat);
    if (cue >= 0) {
        if ((uint32_t)cue != g_proj.cur_pat)
            pattern_switch_now((uint32_t)cue);
        cue = -1;
    }
    pat_start = 0;
    pat_len = pattern_ticks();
    sw_pending = sw_planned = 0;
    ext_target = 0;
    for (i = 0; i < NTRACKS; i++)
        locked[i] = 0;
    if (g_proj.transport & 1u)
        midi_out(0xFA, 0, 0);
    playing = 1;
}

static void transport_stop(void)
{
    uint32_t i;
    playing = 0;
    if (sw_pending) {                              /* a switch caught half way: finish it, so no track is left behind */
        for (i = 0; i < NTRACKS; i++)
            if (!((sw_done >> i) & 1u))
                pattern_swap_track(i, sw_from, sw_to);
        g_proj.cur_pat = sw_to;
        sw_pending = 0;
    }
    for (i = 0; i < NTRACKS; i++) {
        midi_note_off_track(i);
        if (tp[i].held)
            voice_off(i, 255);
        tp[i].held = tp[i].tied = 0;
        tp[i].nq = 0;
        tp[i].play_pos = -1;
    }
}

/* live recording: the note lands on the step nearest in time, which is always the one decided last (decisions are
 * made half a step early) */
static void record(uint32_t ti, uint32_t note, uint32_t vel)
{
    step_t *s = &g_proj.pat.trk[ti].step[tp[ti].pos & (NSTEPS - 1)];
    s->flags = STEP_ON;
    s->note = (uint8_t)note;
    s->vel = (uint8_t)vel;
    s->gate = 12;
    s->prob = 100;
    s->ratchet = 1;
    s->micro = 0;
    s->cond = COND_ALWAYS;
}

static void events(void)
{
    while (evq.tail != evq.head) {
        uint32_t e = evq.ev[evq.tail & (STK_EVQ_LEN - 1u)];
        uint32_t type = e >> 24, ti = (e >> 16) & 0xFFu, note = (e >> 8) & 0x7Fu, vel = e & 0x7Fu, i;
        __asm__ volatile("" ::: "memory");
        evq.tail++;
        if (ti >= NTRACKS && (type == EV_NOTE_ON || type == EV_NOTE_OFF))
            continue;
        if (type == EV_NOTE_ON)                    /* a live note plays the kit as it is, without the step's locks */
            shadow_sync(ti), locked[ti] = 0;
        switch (type) {
        case EV_NOTE_ON:
            voice_hit(ti, note, vel ? vel : 100, 0, 0);
            trig_push(sample_now, ti, note, vel, 0);
            if (core_rec && playing)
                record(ti, note, vel ? vel : 100);
            break;
        case EV_NOTE_OFF:
            voice_off(ti, note);
            break;
        case EV_ALL_OFF:
            for (i = 0; i < NTRACKS; i++)
                voice_off(i, 255);
            break;
        case EV_PLAY:
            transport_play();
            break;
        case EV_STOP:
            transport_stop();
            if (g_proj.transport & 1u)
                midi_out(0xFC, 0, 0);
            break;
        case EV_PATTERN:                           /* note = the pattern: now if stopped, at the boundary if playing */
            if (!playing) {
                if ((note & 15u) != g_proj.cur_pat)
                    pattern_switch_now(note & 15u);
            } else {
                cue = (int8_t)(note & 15u);
                g_proj.play_mode = PLAY_PATTERN;
            }
            break;
        }
    }
}

/* ---------------------------------------------------------------- MIDI in --- */
/* from the platform, in the audio interrupt before the render: notes play the tracks on their channel; the clock and
 * the transport when the settings say so */
void core_midi_in(uint32_t status, uint32_t d1, uint32_t d2)
{
    uint32_t ti, type = status & 0xF0u, ch = (status & 0x0Fu) + 1u;
    if (status == 0xF8u) {
        if (g_proj.clk_in) {
            uint32_t iv = abs_samples - ext_last;
            if (ext_seen && iv > 0u && iv < 44100u) {   /* 24 a quarter: 2.4 .. 300 BPM */
                int32_t sp = (int32_t)((iv << 16) / 4u);
                ext_spt = ext_spt ? ext_spt - (ext_spt >> 3) + (sp >> 3) : sp;   /* smoothed: the block's jitter */
            }
            if (playing)
                ext_target += 4u;
            ext_last = abs_samples;
            ext_seen = 1;
        }
        return;
    }
    if (status == 0xFAu || status == 0xFBu) {      /* start, continue (from the top: no song position) */
        if (g_proj.transport & 2u) {
            transport_play();
            ext_target = 0;
        }
        return;
    }
    if (status == 0xFCu) {
        if (g_proj.transport & 2u)
            transport_stop();
        return;
    }
    if (type != 0x90u && type != 0x80u)
        return;
    for (ti = 0; ti < NTRACKS; ti++) {
        if (g_proj.pat.trk[ti].midi_ch != ch)
            continue;
        if (type == 0x90u && d2) {
            shadow_sync(ti);
            locked[ti] = 0;
            voice_hit(ti, d1 & 0x7Fu, d2 & 0x7Fu, 0, 0);
            trig_push(sample_now, ti, d1, d2, 0);
            if (core_rec && playing)
                record(ti, d1 & 0x7Fu, d2 & 0x7Fu);
        } else {
            voice_off(ti, d1 & 0x7Fu);
        }
    }
}

uint32_t core_ext_bpm10(void)                      /* the followed clock's tempo, 0 when not following one */
{
    if (!g_proj.clk_in || !ext_seen || ext_spt < 256)
        return 0;
    return (275625u << 8) / ((uint32_t)ext_spt >> 8);
}

int core_pattern(void) { return g_proj.cur_pat; }
int core_cue(void) { return sw_pending ? sw_to : cue; }
int core_song_row(void) { return song_row; }

/* -------------------------------------------------------------- render --- */
static int32_t trk_buf[STK_MAX_BLOCK];
static int32_t mix_l[STK_MAX_BLOCK], mix_r[STK_MAX_BLOCK];
static int32_t send_d[STK_MAX_BLOCK], send_r[STK_MAX_BLOCK], duck[STK_MAX_BLOCK];

static void render_chunk(int32_t *lr, uint32_t n, int32_t target)
{
    uint32_t ti, f;
    uint32_t pk = peak_q15;
    int pumping, any_d = 0, any_r = 0;
    for (f = 0; f < n; f++)
        mix_l[f] = mix_r[f] = send_d[f] = send_r[f] = 0;
    pumping = fx_pump_fill(duck, n);
    for (ti = 0; ti < NTRACKS; ti++) {
        const track_t *t;
        int32_t lvl;
        if (!locked[ti] || shadow[ti].engine != g_proj.pat.trk[ti].engine)
            shadow_sync(ti);                       /* no lock in force: the knobs as they are now */
        t = &shadow[ti];
        lvl = (int32_t)t->p[PR_LEVEL] * t->p[PR_LEVEL] * 2;                  /* a squared curve, Q15 */
        int32_t gl = (lvl * (TAB_PAN_L[t->p[PR_PAN] & 127] >> 1)) >> 14;     /* < 2^15 * 2^14 */
        int32_t gr = (lvl * (TAB_PAN_R[t->p[PR_PAN] & 127] >> 1)) >> 14;
        int32_t gd = (lvl * ((int32_t)t->p[PR_SEND_D] * 129)) >> 14;     /* sends: after the level, Q15 */
        int32_t gs = (lvl * ((int32_t)t->p[PR_SEND_R] * 129)) >> 14;
        int ducked = pumping && !(t->engine == ENGINE_DRUM && DM_IS_KICK(t->model));
        voice_sync(ti);
        if (!voice_active(ti))
            continue;
        for (f = 0; f < n; f++)
            trk_buf[f] = 0;
        switch (t->engine) {
        case ENGINE_ACID: acid_render(&voice[ti].a, t, trk_buf, n); break;
        case ENGINE_SYNTH:
        case ENGINE_FM: pool_render(ti, t, trk_buf, n); break;
        case ENGINE_FX: fx_render(&voice[ti].x, t, trk_buf, n); break;
        default: drum_render(&voice[ti].d, t, trk_buf, n); break;
        }
        any_d |= gd != 0;
        any_r |= gs != 0;
        for (f = 0; f < n; f++) {
            int32_t x = clampi(trk_buf[f], -65535, 65535);
            if (ducked)
                x = (x * (duck[f] >> 1)) >> 14;
            mix_l[f] += (x * (gl >> 1)) >> 14;     /* < 2^16 * 2^14 */
            mix_r[f] += (x * (gr >> 1)) >> 14;
            if (gd)
                send_d[f] += (x * (gd >> 1)) >> 14;
            if (gs)
                send_r[f] += (x * (gs >> 1)) >> 14;
        }
    }
    tempo_update();
    if (!core_fx_bypass)
        fx_bus(mix_l, mix_r, send_d, send_r, n, spt_q16, any_d, any_r);
    /* the master: a soft limiter (tanh, the sum at 3/4 into it), then MASTER, gliding so it never clicks */
    for (f = 0; f < n; f++) {
        int32_t l = dsp_tanh(clampi(mix_l[f] - (mix_l[f] >> 2), -131071, 131071));
        int32_t r = dsp_tanh(clampi(mix_r[f] - (mix_r[f] >> 2), -131071, 131071));
        uint32_t a;
        gain_q15 += (target - gain_q15) >> 8;
        l = (l * gain_q15) >> 15;
        r = (r * gain_q15) >> 15;
        a = (uint32_t)(l < 0 ? -l : l);
        if (a > pk)
            pk = a;
        lr[2u * f] = l * 256;                      /* Q15 -> Q23 (a multiply: << on a negative is undefined in C) */
        lr[2u * f + 1u] = r * 256;
    }
    peak_q15 = pk;
}

void core_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    int32_t target = (int32_t)(master_q12 > 4096u ? 4096u : master_q12) << 3;   /* Q12 -> Q15 */
    uint32_t done = 0;
    events();
    if (n > STK_MAX_BLOCK)
        n = STK_MAX_BLOCK;
    while (done < n) {
        uint32_t chunk = n - done;
        int ext = g_proj.clk_in && ext_seen;
        if (playing && ext) {
            /* following a clock: our own tick rate from the clock's, never more than a clock ahead, catching up
             * at once if behind */
            if (ext_spt)
                spt_q16 = ext_spt;
            if (tick_now >= ext_target + 4u)
                tick_rem = (int32_t)(chunk << 16) + 1;   /* hold: wait for the next clock */
            else if (tick_now + 8u < ext_target)
                tick_rem = 0;
        }
        if (playing) {
            if (tick_rem <= 0) {
                if (!ext)
                    tempo_update();
                seq_tick();
                tick_rem += spt_q16;
            }
            {
                uint32_t until = (uint32_t)(tick_rem + 65535) >> 16;   /* whole samples to the next tick */
                if (until < chunk)
                    chunk = until ? until : 1;
            }
        }
        render_chunk(&lr[2u * done], chunk, target);
        abs_samples += chunk;
        if (playing) {
            tick_rem -= (int32_t)chunk << 16;
            sample_now += chunk;
        }
        done += chunk;
    }
}

void core_panic(void)
{
    uint32_t i;
    playing = 0;
    for (i = 0; i < NTRACKS; i++) {
        voice[i].d.active = 0;
        voice[i].a.active = 0;
        voice[i].a.amp = 0;
        voice[i].x.active = 0;
        tp[i].held = tp[i].tied = 0;
        tp[i].nq = 0;
    }
    pool_init();
}

int core_playing(void) { return playing; }
int core_play_pos(uint32_t ti) { return ti < NTRACKS && playing ? tp[ti].play_pos : -1; }
uint32_t core_loops(uint32_t ti)
{
    const track_t *t = &g_proj.pat.trk[ti & 7u];
    return tp[ti & 7u].count ? (tp[ti & 7u].count - 1u) / (t->len ? t->len : 1u) : 0;
}

uint32_t core_voices_active(void)
{
    uint32_t i, c = 0;
    for (i = 0; i < NTRACKS; i++)
        c += (uint32_t)voice_active(i);
    return c;
}

uint32_t core_peak(void)
{
    uint32_t p = peak_q15;
    peak_q15 = 0;
    return p;
}

/* ------------------------------------------------------------ projects --- */
static const char *const DEFAULT_KIT[NTRACKS] = {"909 PUNCH", "CLAP", "CL HAT", "OP HAT", "RIM", "TOM",
                                                 "ACID SAW", "SUB BASS"};
/* the master bus's defaults: a dotted-8th dub delay and a mid room ready on the sends; no pump, no squash */
static const uint8_t FX_DEFAULT[NFX] = {2, 70, 80, 90, 90, 50, 80, 0, 60, 0, 64};

void core_clear_project(void)
{
    uint32_t ti, i;
    g_proj.bpm10 = 1380;
    g_proj.swing = 50;
    g_proj.seed = 1;
    g_proj.cur_pat = 0;
    g_proj.play_mode = PLAY_PATTERN;
    g_proj.chain_len = 0;
    for (i = 0; i < NCHAIN; i++)
        g_proj.chain[i] = 0;
    g_proj.song_len = 0;
    for (i = 0; i < NSONG; i++)
        g_proj.song[i].pat = 0, g_proj.song[i].reps = 1;
    g_proj.scale = SCALE_CHROMATIC;
    g_proj.tonic = 0;
    g_proj.clk_out = 0;
    g_proj.clk_in = 0;
    g_proj.transport = 0;
    locks_clear(g_proj.lock);
    for (i = 0; i < NPATTERNS; i++)
        bank_clear(&g_bank[i]);
    for (ti = 0; ti < NTRACKS; ti++) {
        track_t *t = &g_proj.pat.trk[ti];
        for (i = 0; i < NSTEPS; i++)
            step_default(&t->step[i]);
        t->len = 16;
        t->scale = SCALE_16;
        t->dir = DIR_FWD;
        t->midi_ch = 0;
        preset_apply(t, (uint32_t)preset_find(DEFAULT_KIT[ti]));
        t->chord[0] = 0;
        t->chord[1] = 4;
        t->chord[2] = 7;
        t->chord[3] = -128;
    }
    for (ti = 0; ti < NFX; ti++)
        g_proj.fx[ti] = FX_DEFAULT[ti];
}

static void on(uint32_t ti, uint32_t s, uint32_t note, uint32_t vel, uint32_t flags)
{
    step_t *st = &g_proj.pat.trk[ti].step[s];
    st->flags = (uint8_t)(STEP_ON | flags);
    st->note = (uint8_t)note;
    st->vel = (uint8_t)vel;
}

void core_load_demo(void)
{
    /* an A-minor acid line (0 = rest), with accents and slides */
    static const uint8_t ACID[16] = {45, 45, 0, 57, 48, 45, 55, 57, 45, 0, 60, 52, 45, 57, 43, 47};
    static const uint8_t ACID_F[16] = {0, STEP_ACCENT, 0, 0, STEP_SLIDE, STEP_SLIDE, 0, STEP_ACCENT,
                                       0, 0, 0, STEP_ACCENT, STEP_SLIDE, 0, 0, STEP_ACCENT};
    uint32_t i;
    core_clear_project();
    g_proj.swing = 54;
    for (i = 0; i < 16; i += 4)
        on(0, i, 60, 120, 0);                     /* kick: four to the floor */
    on(0, 14, 60, 100, 0);                        /* and a roll into the bar's end, on FILL only */
    g_proj.pat.trk[0].step[14].ratchet = 2;
    g_proj.pat.trk[0].step[14].cond = COND_FILL;
    on(1, 4, 60, 110, 0);                         /* clap on 2 and 4 */
    on(1, 12, 60, 115, 0);
    for (i = 0; i < 16; i++)                      /* closed hats on the 16ths but the off-beats */
        if ((i & 3u) != 2u)
            on(2, i, 60, (i & 1u) ? 64 : 96, 0);
    for (i = 2; i < 16; i += 4)                   /* open hats on the off-beats, choked by the next closed one */
        on(3, i, 60, 100, 0);
    on(4, 7, 60, 90, 0);                          /* rim, the last one on probability */
    on(4, 13, 60, 80, 0);
    on(4, 15, 60, 70, 0);
    g_proj.pat.trk[4].step[15].prob = 50;
    for (i = 0; i < 16; i++)
        if (ACID[i])
            on(6, i, ACID[i], ACID_F[i] & STEP_ACCENT ? 120 : 96, ACID_F[i]);
    /* Phase 4: a minor-seventh rave stab on track 6 into the dub delay, the clap into the reverb, the rim into the
     * delay, and a little pump from the kick */
    preset_apply(&g_proj.pat.trk[5], (uint32_t)preset_find("RAVE STAB"));
    on(5, 0, 57, 110, 0);
    on(5, 10, 57, 96, 0);
    g_proj.pat.trk[5].step[10].cond = COND_1_2;
    g_proj.pat.trk[5].p[PR_SEND_D] = 70;
    g_proj.pat.trk[1].p[PR_SEND_R] = 50;
    g_proj.pat.trk[4].p[PR_SEND_D] = 80;
    g_proj.fx[FX_PUMP] = 50;
    /* pattern 2, the breakdown: no kick, the stab on every bar and the acid an octave up with a p-locked filter
     * climbing; then the chain P1 P1 P1 P2 (play mode CHAIN to hear it) */
    {
        bank_t *b = &g_bank[1];
        uint32_t k;
        for (k = 0; k < 16; k++) {
            b->step[2][k] = g_proj.pat.trk[2].step[k];
            b->step[6][k] = g_proj.pat.trk[6].step[k];
            if (b->step[6][k].flags & STEP_ON)
                b->step[6][k].note = (uint8_t)(b->step[6][k].note + 12);
        }
        b->step[1][12] = g_proj.pat.trk[1].step[12];
        b->step[5][0] = g_proj.pat.trk[5].step[0];
        b->step[5][8] = g_proj.pat.trk[5].step[0];
        for (k = 0; k < 4; k++) {
            b->lock[k].trk = 6;
            b->lock[k].step = (uint8_t)(k * 4u);
            b->lock[k].param = PR_CUTOFF;
            b->lock[k].value = (uint8_t)(40 + k * 18);
        }
    }
    g_proj.chain[0] = g_proj.chain[1] = g_proj.chain[2] = 0;
    g_proj.chain[3] = 1;
    g_proj.chain_len = 4;
    g_proj.song[0].pat = 0;
    g_proj.song[0].reps = 3;
    g_proj.song[1].pat = 1;
    g_proj.song[1].reps = 1;
    g_proj.song_len = 2;
}

void core_init(void)
{
    uint32_t i;
    for (i = 0; i < sizeof voice; i++)            /* every voice from zero: noise seeds included (repeatable) */
        ((uint8_t *)voice)[i] = 0;
    for (i = 0; i < sizeof g_pool; i++)
        ((uint8_t *)g_pool)[i] = 0;
    for (i = 0; i < NTRACKS; i++) {
        tp[i].play_pos = -1;
        tp[i].nq = 0;
        tp[i].held = tp[i].tied = 0;
    }
    pool_init();
    fx_init();
    evq.head = evq.tail = 0;
    trigq.head = trigq.tail = 0;
    core_mute = core_solo = 0;
    core_fill = core_rec = 0;
    playing = 0;
    gain_q15 = 0;
    peak_q15 = 0;
    spt_bpm = 0;
    core_load_demo();
    for (i = 0; i < NTRACKS; i++) {
        voice_engine[i] = g_proj.pat.trk[i].engine;
        midi_held[i] = 0xFFu;
        locked[i] = 0;
        shadow_sync(i);
    }
    cue = -1;
    sw_pending = sw_planned = 0;
    midiq.head = midiq.tail = 0;
    ext_seen = 0;
    ext_spt = 0;
    pat_start = 0;
    pat_len = pattern_ticks();
    tempo_update();
}
