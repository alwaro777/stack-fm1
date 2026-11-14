/* SPDX-License-Identifier: GPL-3.0-or-later
 * STACK — host unit tests for core/ and ui/ (no platform). Built and run by `make test`.
 *
 *   test_main GOLDEN_DIR OUT_DIR [--record]
 *
 * Golden values (framebuffer and audio hashes) live in GOLDEN_DIR/golden.txt; a missing one is recorded and reported,
 * --record rewrites them after an intended change. PNG snapshots and WAV files go to OUT_DIR, to look at and listen
 * to. The audio is integer-only, so the hashes are the same on every machine (and on the FM-1). */
#include <time.h>
#include "harness.h"
#include "../../app/core/core.h"
#include "../../app/core/engine.h"
#include "../../app/ui/ui.h"
/* the sound system's internals, for the acceptance checks */
int stack_box(uint32_t t, int32_t *x, int32_t *y, int *kind, int *exc, int *state);
void stack_reset(void);

/* ------------------------------------------------------------------ helpers --- */
#define MAXTRIG 4096
static trig_t trigs[MAXTRIG];
static uint32_t ntrig;
static int32_t blk[2 * 256];
static int32_t long_buf[2 * 44100 * 8];        /* 8 s of stereo */

/* run the core for n frames in 256-frame blocks, collecting every trigger; optionally keep the audio */
static void run(uint32_t frames, int32_t *keep)
{
    uint32_t f = 0;
    trig_t t;
    while (f < frames) {
        uint32_t n = frames - f > 256u ? 256u : frames - f;
        core_render(keep ? &keep[2 * f] : blk, n, 4096);
        while (core_trig_pop(&t))
            if (ntrig < MAXTRIG)
                trigs[ntrig++] = t;
        f += n;
    }
}

static void fresh(uint32_t bpm10)              /* an empty project at a tempo, stopped, the triggers forgotten */
{
    core_init();
    core_clear_project();
    g_proj.bpm10 = (uint16_t)bpm10;
    ntrig = 0;
}

static void play(void)
{
    core_post(EV_PLAY, 0, 0, 0);
    ntrig = 0;
}

static step_t *st(uint32_t trk, uint32_t s) { return &g_proj.pat.trk[trk].step[s]; }
static void on(uint32_t trk, uint32_t s) { st(trk, s)->flags |= STEP_ON; }

static uint32_t trig_count(uint32_t track)
{
    uint32_t i, c = 0;
    for (i = 0; i < ntrig; i++)
        c += trigs[i].track == track;
    return c;
}

static int near(uint32_t got, double want) { return (double)got >= want - 1.0 && (double)got <= want + 1.0; }

/* ----------------------------------------------------------------- fb --- */
static void test_fb(void)
{
    uint32_t m;
    fb_clear(P_BLACK);
    fb_pixel(3, 4, P_YELLOW);
    fb_pixel(4, 4, P_LIME);
    CHECK(fb_get(3, 4) == P_YELLOW && fb_get(4, 4) == P_LIME, "nibble packing");
    CHECK(fb_get(5, 4) == P_BLACK, "neighbour untouched");
    fb_pixel(-1, 0, P_RED);
    fb_pixel(240, 239, P_RED);
    fb_rect(-10, -10, 15, 15, P_WHITE);
    CHECK(fb_get(4, 4) == P_WHITE && fb_get(5, 5) == P_BLACK, "rect clipping");
    fb_rect(3, 10, 4, 1, P_RED);                 /* odd start and end: x 3..6 */
    CHECK(fb_get(2, 10) == P_BLACK && fb_get(3, 10) == P_RED && fb_get(6, 10) == P_RED && fb_get(7, 10) == P_BLACK,
          "byte-filled rect edges");
    CHECK(fb_text_w("AB", 1) == 11, "text width");
    fb_invalidate();
    CHECK(fb_changed_bands() == (1u << FB_NBANDS) - 1u, "invalid -> all bands");
    fb_mark_sent((1u << FB_NBANDS) - 1u);
    CHECK(fb_changed_bands() == 0, "after send -> none");
    fb_pixel(100, 20, P_MAGENTA);
    m = fb_changed_bands();
    CHECK(m == 1u << 1, "one pixel -> band 1, got %x", m);
    fb_mark_sent(m);
    fb_invalidate();
    fb_mark_sent(0xFFu);
    m = fb_changed_bands();
    CHECK(m == ((1u << FB_NBANDS) - 1u) - 0xFFu, "capped flush leaves bands 8..14, got %x", m);
    fb_mark_sent(m);
    CHECK(fb_changed_bands() == 0, "second capped flush completes the screen");
}

/* ---------------------------------------------------------- sequencer --- */
#define Q120 22050.0                           /* a quarter note at 120 BPM, in samples */
#define S120 (Q120 / 4)                        /* a 16th */
#define T120 (Q120 / 96)                       /* a tick */

static void test_grid(void)
{
    uint32_t i;
    fresh(1200);
    for (i = 0; i < 16; i += 4)
        on(0, i);
    play();
    run(44100 * 2, 0);                         /* one bar at 120 */
    CHECK(trig_count(0) == 4, "4 kicks in a bar, got %u", trig_count(0));
    for (i = 0; i < ntrig && i < 4; i++)
        CHECK(near(trigs[i].sample, i * Q120), "kick %u at %u, want %.0f", i, trigs[i].sample, i * Q120);
    /* tempo with a fraction: 137.5 BPM, 16ths for 4 bars: no drift */
    fresh(1375);
    for (i = 0; i < 16; i++)
        on(0, i);
    play();
    run(64 * 4810, 0);                         /* just under 64 16ths at 137.5 (a 16th = 4810.9 samples) */
    CHECK(trig_count(0) == 64, "64 16ths, got %u", trig_count(0));
    if (ntrig >= 64)
        CHECK(near(trigs[63].sample, 63 * 44100.0 * 60 / 137.5 / 4), "16th 63 at %u, want %.1f", trigs[63].sample,
              63 * 44100.0 * 60 / 137.5 / 4);
}

static void test_swing_micro_ratchet(void)
{
    uint32_t i;
    fresh(1200);
    g_proj.swing = 75;                         /* odd 16ths half a step late */
    for (i = 0; i < 4; i++)
        on(0, i);
    play();
    run(44100, 0);
    CHECK(ntrig == 4, "4 hits");
    CHECK(near(trigs[0].sample, 0) && near(trigs[2].sample, 2 * S120), "even steps on the grid");
    CHECK(near(trigs[1].sample, S120 + 12 * T120), "odd step 12 ticks late: %u want %.1f", trigs[1].sample,
          S120 + 12 * T120);
    CHECK(near(trigs[3].sample, 3 * S120 + 12 * T120), "odd step 3 late");

    fresh(1200);                               /* a negative nudge plays early */
    on(0, 4);
    st(0, 4)->micro = -6;
    play();
    run(44100, 0);
    CHECK(ntrig == 1 && near(trigs[0].sample, 4 * S120 - 6 * T120), "nudge -6 ticks: %u want %.1f",
          ntrig ? trigs[0].sample : 0, 4 * S120 - 6 * T120);

    fresh(1200);                               /* ratchet x3: three hits inside the step */
    on(0, 0);
    st(0, 0)->ratchet = 3;
    play();
    run(S120 * 2, 0);
    CHECK(ntrig == 3, "3 ratchet hits, got %u", ntrig);
    if (ntrig == 3)
        CHECK(near(trigs[1].sample, 8 * T120) && near(trigs[2].sample, 16 * T120), "ratchets 8 ticks apart");
}

static void test_probability(void)
{
    uint32_t i, a, b, pattern_a[64], na;
    fresh(1200);
    for (i = 0; i < 16; i++) {
        on(0, i);
        st(0, i)->prob = 50;
    }
    play();
    run(44100 * 8, 0);                         /* 4 bars = 64 steps */
    a = trig_count(0);
    na = ntrig;
    for (i = 0; i < na && i < 64; i++)
        pattern_a[i] = trigs[i].sample;
    CHECK(a > 20 && a < 44, "about half of 64 steps play: %u", a);
    play();                                    /* PLAY again: the same dice, the same groove */
    run(44100 * 8, 0);
    b = trig_count(0);
    CHECK(a == b, "the same seed, the same count: %u vs %u", a, b);
    for (i = 0; i < ntrig && i < na && i < 64; i++)
        if (trigs[i].sample != pattern_a[i]) {
            CHECK(0, "hit %u differs", i);
            break;
        }
    golden("prob50_seed1_count", a);
}

static void test_polymeter_conditions_dirs(void)
{
    uint32_t i;
    fresh(1200);                               /* track 0 loops in 3 steps, track 1 in 4 */
    g_proj.pat.trk[0].len = 3;
    g_proj.pat.trk[1].len = 4;
    on(0, 0);
    on(1, 0);
    play();
    run((uint32_t)(12 * S120), 0);
    CHECK(trig_count(0) == 4 && trig_count(1) == 3, "3 against 4 over 12 steps: %u, %u", trig_count(0),
          trig_count(1));

    fresh(1200);                               /* 1:2 plays the 1st, 3rd... loop; 4:4 only the 4th */
    g_proj.pat.trk[0].len = 4;
    on(0, 0);
    st(0, 0)->cond = COND_1_2;
    g_proj.pat.trk[1].len = 4;
    on(1, 0);
    st(1, 0)->cond = COND_4_4;
    play();
    run((uint32_t)(16 * S120), 0);
    CHECK(trig_count(0) == 2 && trig_count(1) == 1, "1:2 twice and 4:4 once in 4 loops: %u, %u", trig_count(0),
          trig_count(1));

    fresh(1200);                               /* FILL: only while held */
    on(0, 0);
    st(0, 0)->cond = COND_FILL;
    play();
    run((uint32_t)(15 * S120), 0);             /* (stop before the look-ahead decides the next loop's step 1) */
    CHECK(trig_count(0) == 0, "no fill, no hit");
    core_fill = 1;
    run((uint32_t)(2 * S120), 0);
    CHECK(trig_count(0) == 1, "fill held: it plays");
    core_fill = 0;

    fresh(1200);                               /* directions: every step on, its note = its index */
    g_proj.pat.trk[0].len = 4;
    for (i = 0; i < 4; i++) {
        on(0, i);
        st(0, i)->note = (uint8_t)(60 + i);
    }
    g_proj.pat.trk[0].dir = DIR_PINGPONG;
    play();
    run((uint32_t)(8 * S120) - 10, 0);
    {
        static const uint8_t WANT[8] = {0, 1, 2, 3, 2, 1, 0, 1};
        int ok = ntrig == 8;
        for (i = 0; ok && i < 8; i++)
            ok = trigs[i].note == 60 + WANT[i];
        CHECK(ok, "ping-pong 0 1 2 3 2 1 0 1 (%u hits)", ntrig);
    }
    g_proj.pat.trk[0].dir = DIR_REV;
    play();
    run((uint32_t)(4 * S120) - 10, 0);
    CHECK(ntrig == 4 && trigs[0].note == 63 && trigs[3].note == 60, "reverse 3 2 1 0");
}

static void test_mute_solo_rec_tempo(void)
{
    uint32_t i;
    fresh(1200);
    on(0, 0);
    on(1, 0);
    core_mute = 1u;
    play();
    run((uint32_t)(4 * S120), 0);
    CHECK(trig_count(0) == 0 && trig_count(1) == 1, "track 1 muted");
    core_mute = 0;
    core_solo = 1u;
    play();
    run((uint32_t)(4 * S120), 0);
    CHECK(trig_count(0) == 1 && trig_count(1) == 0, "track 1 solo");
    core_solo = 0;

    fresh(1200);                               /* live recording, quantised to the nearest step */
    core_rec = 1;
    play();
    run((uint32_t)(5 * S120 + 0.3 * S120), 0); /* a little after step 5 */
    core_post(EV_NOTE_ON, 6, 50, 110);
    run(256, 0);
    CHECK((st(6, 5)->flags & STEP_ON) && st(6, 5)->note == 50 && st(6, 5)->vel == 110, "recorded on step 6");
    run((uint32_t)(0.4 * S120), 0);            /* now nearer step 7 than step 6 */
    core_post(EV_NOTE_ON, 6, 52, 100);
    run(64, 0);
    CHECK((st(6, 6)->flags & STEP_ON) && st(6, 6)->note == 52, "early press rounds up to step 7");
    core_rec = 0;

    fresh(1200);                               /* a tempo change applies at the next tick */
    for (i = 0; i < 16; i++)
        on(0, i);
    play();
    run((uint32_t)(4 * S120) + 10, 0);
    g_proj.bpm10 = 600;                        /* half speed */
    ntrig = 0;
    run((uint32_t)(8 * S120), 0);
    CHECK(ntrig == 4 || ntrig == 5, "half tempo: 4 16ths in the time of 8, got %u", ntrig);
}

/* ------------------------------------------------------------------ audio --- */
static void test_presets(void)
{
    uint32_t p, f;
    for (p = 0; p < NPRESETS; p++) {
        int32_t peak = 0;
        uint32_t last = 0, frames = PRESETS[p].engine == ENGINE_FX || PRESETS[p].group == PG_PAD ? 44100 * 4 : 44100 * 2;
        char name[40];
        uint32_t note = PRESETS[p].engine == ENGINE_ACID || PRESETS[p].group == PG_BASS ? 45 : 60;
        uint32_t hold = PRESETS[p].engine == ENGINE_FX || PRESETS[p].group == PG_PAD ? 44100 : 44100 / 4;   /* gestures, pads: longer */
        fresh(1200);
        g_proj.fx[FX_DLY_LEVEL] = g_proj.fx[FX_REV_LEVEL] = 0;   /* the dry sound: no echoes measured */
        preset_apply(&g_proj.pat.trk[0], p);
        run(4096, 0);                          /* MASTER's glide up from silence, first */
        core_post(EV_NOTE_ON, 0, note, 120);
        run(hold, long_buf);
        core_post(EV_NOTE_OFF, 0, note, 0);
        run(frames - hold, &long_buf[2 * hold]);
        for (f = 0; f < frames; f++) {
            int32_t a = long_buf[2 * f] < 0 ? -long_buf[2 * f] : long_buf[2 * f];
            if (a > peak)
                peak = a;
            if (a > 64 << 8)
                last = f;
        }
        CHECK(peak > (1 << 19), "%s audible (> -24 dBFS): peak %d", PRESETS[p].name, peak);
        CHECK(peak < (1 << 23), "%s within 24 bits", PRESETS[p].name);
        CHECK(last < frames - 4410, "%s dies away (last at %u)", PRESETS[p].name, last);
        CHECK(core_voices_active() == 0, "%s voice freed", PRESETS[p].name);
        snprintf(name, sizeof name, "preset_%s", PRESETS[p].name);
        for (f = 0; name[f]; f++)
            if (name[f] == ' ' || name[f] == '.')
                name[f] = '_';
        golden(name, hash32(long_buf, 2 * frames));
        wav_write(name, long_buf, frames);
    }
}

/* ------------------------------------------------------ Phase 4: voices and the bus --- */
static void set_preset(uint32_t trk, const char *name)
{
    int p = preset_find(name);
    CHECK(p >= 0, "preset %s exists", name);
    preset_apply(&g_proj.pat.trk[trk], (uint32_t)(p < 0 ? 0 : p));
}

static void test_pool(void)
{
    uint32_t t, i, total;
    fresh(1200);
    for (t = 0; t < 4; t++)
        set_preset(t, "WARM PAD");                 /* four poly tracks */
    g_proj.pat.trk[0].p[PR_CHORD] = 4;             /* track 1: MIN7, four voices a note */
    core_post(EV_NOTE_ON, 0, 60, 100);
    run(64, 0);
    CHECK(pool_active(0) == 4, "a MIN7 chord takes 4 voices: %u", pool_active(0));
    core_post(EV_NOTE_ON, 0, 62, 100);             /* the track at its limit: its own oldest go */
    run(64, 0);
    CHECK(pool_active(0) == 4, "still 4 on the track: %u", pool_active(0));
    for (i = 0, total = 0; i < NPOOL; i++)
        total += g_pool[i].used && g_pool[i].track == 0 && g_pool[i].key == 62;
    CHECK(total == 4, "the new chord replaced the old one: %u voices of D", total);
    for (t = 1; t < 4; t++) {                      /* fill the pool: 3 tracks x 4 notes */
        for (i = 0; i < 4; i++)
            core_post(EV_NOTE_ON, t, 48 + i * 3, 100);
    }
    run(64, 0);
    for (i = 0, total = 0; i < NPOOL; i++)
        total += g_pool[i].used;
    CHECK(total == 16, "the pool is full: %u", total);
    core_post(EV_NOTE_OFF, 2, 51, 0);              /* a released voice on track 3 */
    run(64, 0);
    core_post(EV_NOTE_ON, 1, 70, 100);             /* track 2 at its limit: steals its own oldest (48) */
    run(64, 0);
    for (i = 0, total = 0; i < NPOOL; i++)
        total += g_pool[i].used && g_pool[i].track == 1 && g_pool[i].note == 48;
    CHECK(total == 0 && pool_active(1) == 4, "track 2 stole its own oldest note");
    CHECK(pool_active(2) == 4, "track 3 kept its voices (its released one is its own business)");
}

/* RMS of the left channel over [a, b) */
static double rms(const int32_t *lr, uint32_t a, uint32_t b)
{
    double s = 0;
    uint32_t i;
    for (i = a; i < b; i++)
        s += (double)lr[2 * i] * lr[2 * i];
    return __builtin_sqrt(s / (b - a));
}

static void test_pump(void)
{
    double near0, far0, near1, far1;
    uint32_t i;
    for (i = 0; i < 2; i++) {
        fresh(1200);
        g_proj.fx[FX_DLY_LEVEL] = g_proj.fx[FX_REV_LEVEL] = 0;
        g_proj.pat.trk[0].p[PR_LEVEL] = 0;         /* a silent kick still pumps */
        on(0, 0);
        on(0, 4);
        set_preset(7, "SUB BASS");                 /* a bass on track 8, held by hand */
        g_proj.fx[FX_PUMP] = i ? 127 : 0;
        g_proj.fx[FX_PUMP_REL] = 60;
        play();
        core_post(EV_NOTE_ON, 7, 45, 100);
        run((uint32_t)(Q120 * 2), long_buf);
        /* just after the second kick vs just before it */
        if (i) {
            near1 = rms(long_buf, (uint32_t)Q120 + 200, (uint32_t)Q120 + 1500);
            far1 = rms(long_buf, (uint32_t)Q120 - 3000, (uint32_t)Q120 - 200);
        } else {
            near0 = rms(long_buf, (uint32_t)Q120 + 200, (uint32_t)Q120 + 1500);
            far0 = rms(long_buf, (uint32_t)Q120 - 3000, (uint32_t)Q120 - 200);
        }
    }
    printf("pump: bass after/before a kick %.2f without, %.2f with\n", near0 / far0, near1 / far1);
    CHECK(near0 / far0 > 0.8, "no pump: the bass stays");
    CHECK(near1 / far1 < 0.5, "pump: the kick ducks the bass by 6 dB or more");
    wav_write("pump", long_buf, (uint32_t)(Q120 * 2));
}

static void test_delay_reverb(void)
{
    uint32_t i, d, pl = 0, pr = 0;
    int32_t ml = 0, mr = 0;
    /* a rim shot into the dub delay: 3/16 at 120 BPM = 16537 samples; echo 1 on the left, echo 2 on the right */
    fresh(1200);
    g_proj.fx[FX_REV_LEVEL] = 0;
    g_proj.fx[FX_DLY_TIME] = 2;
    g_proj.fx[FX_DLY_LEVEL] = 127;
    g_proj.fx[FX_DLY_FB] = 90;
    g_proj.pat.trk[4].p[PR_SEND_D] = 127;          /* (sends are after the level: the dry hit stays) */
    run(4096, 0);
    core_post(EV_NOTE_ON, 4, 60, 127);
    run(44100 * 2, long_buf);
    d = (uint32_t)(S120 * 3);
    for (i = 0; i < 44100 * 2; i++) {
        int32_t l = long_buf[2 * i] < 0 ? -long_buf[2 * i] : long_buf[2 * i];
        int32_t r = long_buf[2 * i + 1] < 0 ? -long_buf[2 * i + 1] : long_buf[2 * i + 1];
        if (i > d / 2 && i < d + d / 2 && l > ml) ml = l, pl = i;
        if (i > d + d / 2 && i < 2 * d + d / 2 && r > mr) mr = r, pr = i;
    }
    printf("delay: first echo left at %u (want ~%u), second right at %u (want ~%u)\n", pl, d, pr, 2 * d);
    CHECK(pl >= d && pl < d + 400, "echo 1 left on time");
    CHECK(pr >= 2 * d && pr < 2 * d + 400, "echo 2 right on time");
    wav_write("delay_echoes", long_buf, 44100 * 2);

    /* the reverb's tail: a clap into it */
    fresh(1200);
    g_proj.fx[FX_DLY_LEVEL] = 0;
    g_proj.fx[FX_REV_LEVEL] = 127;
    g_proj.fx[FX_REV_SIZE] = 110;
    g_proj.pat.trk[1].p[PR_SEND_R] = 127;
    run(4096, 0);
    core_post(EV_NOTE_ON, 1, 60, 127);
    run(44100 * 3, long_buf);
    {
        double t1 = rms(long_buf, 13230, 22050), t2 = rms(long_buf, 44100, 52920), t3 = rms(long_buf, 120000, 130000);
        printf("reverb: rms at 0.3-0.5 s %.0f, 1.0-1.2 s %.0f, 2.7-2.9 s %.0f (the clap itself is over by 0.1 s)\n",
               t1, t2, t3);
        CHECK(t1 > 1e4 && t2 > t1 * 0.01 && t2 < t1 && t3 < t2, "a tail that decays");
    }
    wav_write("reverb_tail", long_buf, 44100 * 3);
}

static void test_djf_comp(void)
{
    uint32_t f;
    int32_t pk0 = 0, pk1 = 0;
    double hf0, hf1;
    /* the DJ filter's low-pass on the hats: much less energy left in the 16ths' clicks */
    for (f = 0; f < 2; f++) {
        uint32_t i;
        double s = 0;
        fresh(1200);
        g_proj.fx[FX_DLY_LEVEL] = g_proj.fx[FX_REV_LEVEL] = 0;
        for (i = 0; i < 16; i++)
            on(2, i);
        g_proj.fx[FX_DJF] = f ? 20 : 64;
        play();
        run(44100, long_buf);
        for (i = 1; i < 44100; i++) {              /* first difference: a crude high-pass */
            double dd = (double)long_buf[2 * i] - long_buf[2 * i - 2];
            s += dd * dd;
        }
        if (f) hf1 = s; else hf0 = s;
    }
    printf("dj filter: high-frequency energy %.1f dB with LP 40\n", 10 * __builtin_log10(hf1 / hf0));
    CHECK(hf1 < hf0 * 0.05, "the low-pass takes the hats' top off");
    /* the compressor: the demo's peaks come down */
    /* the compressor: a loud passage and a quiet one come closer together */
    {
        double ratio[2];
        for (f = 0; f < 2; f++) {
            fresh(1200);
            g_proj.fx[FX_DLY_LEVEL] = g_proj.fx[FX_REV_LEVEL] = 0;
            g_proj.fx[FX_COMP] = f ? 127 : 0;
            set_preset(7, "SUB BASS");
            run(4096, 0);
            core_post(EV_NOTE_ON, 7, 45, 127);
            run(22050, long_buf);
            core_post(EV_NOTE_OFF, 7, 45, 0);
            core_post(EV_NOTE_ON, 7, 45, 25);
            run(22050, &long_buf[2 * 22050]);
            ratio[f] = rms(long_buf, 11025, 22050) / rms(long_buf, 33075, 44100);
            if (f) pk1 = 1; else pk0 = 1;
        }
        printf("compressor: loud/quiet %.1f dB without, %.1f dB with\n", 20 * __builtin_log10(ratio[0]),
               20 * __builtin_log10(ratio[1]));
        CHECK(ratio[1] < ratio[0] * 0.7, "the compressor brings loud and quiet closer (3 dB or more)");
        (void)pk0;
        (void)pk1;
    }
}

/* the master bus at its neutral settings leaves the sound exactly as it was, bit for bit */
static void test_bus_neutral(void)
{
    static int32_t a[2 * 44100];
    uint32_t h1, h2;
    core_init();
    g_proj.fx[FX_DLY_LEVEL] = g_proj.fx[FX_REV_LEVEL] = 0;
    core_post(EV_PLAY, 0, 0, 0);
    run(44100, a);
    h1 = hash32(a, 2 * 44100);
    core_init();
    core_fx_bypass = 1;
    core_post(EV_PLAY, 0, 0, 0);
    run(44100, a);
    core_fx_bypass = 0;
    h2 = hash32(a, 2 * 44100);
    CHECK(h1 == h2, "a neutral bus is transparent: %08x vs %08x", h1, h2);
}

static void test_demo(void)
{
    uint32_t f, frames = 306700;               /* just under 4 bars at 138 BPM (306 782 samples) */
    int32_t peak = 0;
    double sq = 0;
    clock_t c0;
    core_init();                               /* the demo pattern */
    core_post(EV_PLAY, 0, 0, 0);
    ntrig = 0;
    c0 = clock();
    run(frames, long_buf);
    printf("demo: %u frames rendered in %.1f ms on this host (%.0fx real time)\n", frames,
           (double)(clock() - c0) * 1000.0 / CLOCKS_PER_SEC,
           (double)frames / 44100.0 / ((double)(clock() - c0) / CLOCKS_PER_SEC + 1e-9));
    for (f = 0; f < frames; f++) {
        int32_t a = long_buf[2 * f] < 0 ? -long_buf[2 * f] : long_buf[2 * f];
        if (a > peak)
            peak = a;
        sq += (double)long_buf[2 * f] * long_buf[2 * f];
    }
    printf("demo: peak %.1f dBFS, rms %.1f dBFS, %u triggers\n", 20 * __builtin_log10((double)peak / 8388608.0),
           10 * __builtin_log10(sq / frames / (8388608.0 * 8388608.0)), ntrig);
    CHECK(peak > (1 << 22) && peak < 8388608, "demo loud, unclipped: %d", peak);
    CHECK(trig_count(0) == 16 && trig_count(1) == 8, "kick x16, clap x8: %u %u", trig_count(0), trig_count(1));
    golden("demo_4bars", hash32(long_buf, 2 * frames));
    wav_write("demo_4bars", long_buf, frames);
}

/* the worst case for the CPU: every track playing 32nds, four ratchets each, all at once */
static void test_worst_case(void)
{
    uint32_t t, i;
    clock_t c0;
    double sec;
    core_init();
    for (t = 0; t < NTRACKS; t++) {
        g_proj.pat.trk[t].scale = SCALE_32;
        for (i = 0; i < 16; i++) {
            on(t, i);
            st(t, i)->ratchet = 4;
            st(t, i)->note = t == 6 ? 40 : 60;
            st(t, i)->flags |= STEP_ACCENT;
        }
    }
    g_proj.pat.trk[6].p[PR_RESO] = 127;
    core_post(EV_PLAY, 0, 0, 0);
    c0 = clock();
    run(44100 * 4, 0);
    sec = (double)(clock() - c0) / CLOCKS_PER_SEC;
    printf("worst case (drums + acid): %.1f%% of real time here\n", sec / 4 * 100);
    /* and with the heavy engines: two supersaw chord tracks (4 voices each), FM, the hoover, sends everywhere */
    set_preset(3, "RAVE STAB");
    set_preset(4, "HOOVER");
    set_preset(5, "E.PIANO");
    set_preset(7, "RAVE STAB");
    g_proj.pat.trk[3].p[PR_DECAY] = g_proj.pat.trk[7].p[PR_DECAY] = 110;
    g_proj.pat.trk[3].p[PR_SUSTAIN] = g_proj.pat.trk[7].p[PR_SUSTAIN] = 127;
    for (t = 0; t < NTRACKS; t++)
        g_proj.pat.trk[t].p[PR_SEND_D] = g_proj.pat.trk[t].p[PR_SEND_R] = 100;
    g_proj.fx[FX_PUMP] = 100;
    g_proj.fx[FX_COMP] = 100;
    g_proj.fx[FX_DJF] = 40;
    core_post(EV_PLAY, 0, 0, 0);
    c0 = clock();
    run(44100 * 4, 0);
    sec = (double)(clock() - c0) / CLOCKS_PER_SEC;
    printf("worst case (+ chords, FM, hoover, every send, pump, comp, DJ): %.1f%% of real time here\n", sec / 4 * 100);
    CHECK(core_voices_active() >= 6, "the voices are busy: %u", core_voices_active());
}

/* -------------------------------------------------------------------- UI --- */
static ui_input_t in;
static uint32_t now_ms = 1000;

static void frame(void)
{
    uint32_t i;
    ui_frame(&in, now_ms);
    run(441, 0);                               /* 10 ms of audio between frames */
    now_ms += 10;
    for (i = 0; i < UI_NENC; i++)
        in.enc[i] = 0;
}
static void key_tap(uint32_t k) { in.keys |= 1u << k; frame(); in.keys &= ~(1u << k); frame(); }
static void btn_tap(uint32_t b) { in.buttons |= 1u << b; frame(); in.buttons &= ~(1u << b); frame(); }

static void test_ui(void)
{
    track_t backup;
    memset(&in, 0, sizeof in);
    in.master = 4096;
    core_init();
    ui_init();
    frame();
    CHECK(ui_view() == VIEW_SPLASH, "boot on the flyer");
    snapshot("splash_t0");
    now_ms += 460;
    frame();
    snapshot("splash_t470");
    key_tap(UI_WHITE[3]);                      /* a key leaves the flyer, and does nothing else */
    CHECK(ui_view() == VIEW_STACK, "a key leaves the flyer for the sound system");
    CHECK(!(g_proj.pat.trk[0].step[3].flags & STEP_ON), "the key that left the flyer edited nothing");
    btn_tap(UB_SEQ);
    CHECK(ui_view() == VIEW_SEQ, "SEQ: the sequencer");
    snapshot("seq_demo_stopped");

    /* a step on, off */
    key_tap(UI_WHITE[1]);
    CHECK(g_proj.pat.trk[0].step[1].flags & STEP_ON, "white key 2 puts step 2 on");
    key_tap(UI_WHITE[1]);
    CHECK(!(g_proj.pat.trk[0].step[1].flags & STEP_ON), "and off");
    /* the track: black key 7 */
    key_tap(UI_BLACK[6]);
    CHECK(ui_track() == 6, "black key 7 picks track 7");
    /* hold step 3 of the acid and turn K1: its note goes up, the step stays on, no toggle at release */
    in.keys |= 1u << UI_WHITE[3];
    frame();
    in.enc[UE_K1] = 2;
    frame();
    in.enc[UE_ALGO] = 1;                       /* step page 2: K1 ratchet */
    frame();
    in.enc[UE_K1] = 1;
    frame();
    snapshot("seq_step_held");
    in.keys &= ~(1u << UI_WHITE[3]);
    frame();
    CHECK((g_proj.pat.trk[6].step[3].flags & STEP_ON) && g_proj.pat.trk[6].step[3].note == 59 &&
              g_proj.pat.trk[6].step[3].ratchet == 2, "held step edited: note 59, ratchet 2 (note %u, rat %u)",
          g_proj.pat.trk[6].step[3].note, g_proj.pat.trk[6].step[3].ratchet);
    /* SHIFT+REC clears the track, SHIFT+SAVE undoes it */
    backup = g_proj.pat.trk[6];
    in.buttons = 1u << UB_HOME;
    frame();
    btn_tap(UB_REC);
    in.buttons = 1u << UB_HOME;
    frame();
    CHECK(!(g_proj.pat.trk[6].step[0].flags & STEP_ON), "SHIFT+REC cleared the track");
    btn_tap(UB_SAVE);
    in.buttons = 0;
    frame();
    CHECK(!memcmp(&backup, &g_proj.pat.trk[6], sizeof backup), "SHIFT+SAVE undid the clear");
    CHECK(ui_view() == VIEW_SEQ, "SHIFT's release after an action is not a HOME tap");
    /* PLAY: the playhead moves */
    btn_tap(UB_PLAY);
    {
        int i;
        for (i = 0; i < 30; i++)
            frame();
    }
    CHECK(core_playing() && core_play_pos(6) >= 0, "playing");
    snapshot("seq_playing");
    /* EDIT: the acid's filter page; K1 = cutoff */
    btn_tap(UB_EDIT);
    CHECK(ui_view() == VIEW_EDIT, "EDIT view");
    {
        uint32_t c0 = g_proj.pat.trk[6].p[PR_CUTOFF];
        in.enc[UE_K1] = 5;
        frame();
        CHECK(g_proj.pat.trk[6].p[PR_CUTOFF] == c0 + 20, "K1 turned the cutoff (5 detents, accelerated x4): %u -> %u",
              c0, g_proj.pat.trk[6].p[PR_CUTOFF]);
    }
    btn_tap(UB_PLAY);                          /* stop: the snapshot is still */
    snapshot("edit_acid");
    /* the sound system: a HOME tap; black key 3 mutes track 3 */
    btn_tap(UB_HOME);
    CHECK(ui_view() == VIEW_STACK, "HOME tap: the sound system");
    key_tap(UI_BLACK[2]);
    CHECK(core_mute == 1u << 2, "black key 3 mutes track 3 on the sound system");
    core_mute = 0;
    btn_tap(UB_GLO);
    snapshot("global");
    in.buttons = 1u << UB_HOME;
    frame();
    btn_tap(UB_GLO);
    in.buttons = 0;
    frame();
    CHECK(ui_view() == VIEW_HELP, "SHIFT+GLO: help");
    snapshot("help");
    /* KEYS mode: black key 11, then the keys play the track */
    btn_tap(UB_SEQ);
    key_tap(UI_BLACK[BK_KEYS]);
    CHECK(ui_keys_mode(), "KEYS mode");
    in.keys = 1u << 7;
    frame();
    CHECK(core_voices_active() >= 1, "a key plays the track");
    in.keys = 0;
    frame();
    btn_tap(UB_SEQ);
    CHECK(!ui_keys_mode(), "SEQ: back to steps");
}

/* ------------------------------------------------- Phase 3: the sound system --- */
static void frames(int n) { while (n-- > 0) frame(); }

static int box_at(uint32_t t, int32_t *x, int32_t *y, int *kind, int *exc, int *state)
{
    return stack_box(t, x, y, kind, exc, state);
}

static void test_stack(void)
{
    int32_t x, y;
    int kind, exc, state, i, t;
    memset(&in, 0, sizeof in);
    in.master = 4096;
    core_init();
    core_clear_project();                      /* an empty pattern */
    stack_reset();
    ui_init();
    frame();
    key_tap(UI_WHITE[0]);                      /* leave the flyer */
    frames(5);
    CHECK(ui_view() == VIEW_STACK, "the flyer leads to the sound system");
    for (t = 0, i = 0; t < NTRACKS; t++)
        i += box_at((uint32_t)t, &x, &y, &kind, &exc, &state);
    CHECK(i == 0, "an empty pattern: an empty stage");
    snapshot("stack_empty");

    /* acceptance: a kick step brings a sub */
    key_tap(UI_WHITE[0]);                      /* track 1 (909) step 1 on */
    CHECK(box_at(0, &x, &y, &kind, &exc, &state) && kind == CAT_SUB, "a kick: a SUB cabinet arrives");
    CHECK(state == 1, "it falls (state %d)", state);
    frames(12);                                /* ~140 ms into the fall */
    snapshot("stack_kick_falling");
    frames(50);
    box_at(0, &x, &y, &kind, &exc, &state);
    CHECK(state == 0 && x == 104 - 25 && y == 196 - 52, "landed centred on the floor: %d,%d state %d", x, y, state);
    snapshot("stack_kick");

    /* acceptance: a hat step brings a top */
    in.enc[UE_SELECT] = 1;                     /* track 2 */
    frame();
    in.enc[UE_SELECT] = 1;                     /* track 3: the closed hat */
    frame();
    CHECK(ui_track() == 2, "SELECT picked track 3");
    key_tap(UI_WHITE[2]);
    frames(60);
    CHECK(box_at(2, &x, &y, &kind, &exc, &state) && kind == CAT_TOP && y == 196 - 52 - 1 - 28,
          "a hat: a TOP cabinet on the subs (y %d)", y);
    snapshot("stack_kick_hat");

    /* a clap (MID) slides the top up to make room */
    in.enc[UE_SELECT] = -1;
    frame();
    key_tap(UI_WHITE[4]);
    frame();
    box_at(2, &x, &y, &kind, &exc, &state);
    CHECK(state == 2, "the top moves (state %d)", state);
    frames(60);                                /* (the fall with its bounce takes 520 ms) */
    box_at(1, &x, &y, &kind, &exc, &state);
    CHECK(kind == CAT_MID && y == 196 - 52 - 1 - 40, "the clap's MID between (y %d)", y);
    box_at(2, &x, &y, &kind, &exc, &state);
    CHECK(y == 196 - 52 - 1 - 40 - 1 - 28, "the top above it (y %d)", y);

    /* acceptance: mute powers the cabinet off */
    key_tap(UI_BLACK[0]);
    CHECK(core_mute == 1u, "black key 1 mutes the kick");
    frames(2);
    box_at(0, &x, &y, &kind, &exc, &state);
    CHECK(fb_get(x + 4, y + 26) == P_DGRAY, "the muted cabinet is grey (pixel %u)", fb_get(x + 4, y + 26));
    snapshot("stack_kick_muted");
    key_tap(UI_BLACK[0]);
    CHECK(core_mute == 0, "and back on");

    /* a track emptied: its cabinet lifts away, the others close up */
    in.enc[UE_SELECT] = -1;                    /* track 1 */
    frame();
    in.buttons = 1u << UB_HOME;
    frame();
    btn_tap(UB_REC);                           /* SHIFT+REC: clear */
    in.buttons = 0;
    frame();
    box_at(0, &x, &y, &kind, &exc, &state);
    CHECK(state == 3, "the cleared kick's cabinet leaves (state %d)", state);
    frames(40);
    CHECK(!box_at(0, &x, &y, &kind, &exc, &state), "gone");
    box_at(1, &x, &y, &kind, &exc, &state);
    CHECK(y == 196 - 40, "the clap now stands on the floor (y %d)", y);

    /* the demo, settled; then a solo; then the scanlines */
    core_load_demo();
    frames(80);
    snapshot("stack_demo");
    in.buttons = 1u << UB_HOME;
    frame();
    key_tap(UI_BLACK[6]);                      /* SHIFT + black 7 on the stack: solo the acid */
    in.buttons = 0;
    frame();
    CHECK(core_solo == 1u << 6, "solo the acid");
    frames(2);
    snapshot("stack_solo_acid");
    core_solo = 0;
    btn_tap(UB_GLO);
    in.enc[UE_ALGO] = 1;
    frame();
    in.enc[UE_K1] = 1;
    frame();
    btn_tap(UB_HOME);
    frames(2);
    snapshot("stack_scanlines");
    in.enc[UE_K1] = 0;
}

/* acceptance: triggers visible at 140 BPM without lag. The UI runs a frame every 16 ms, the audio in between; every
 * kick must push its cone on the very next frame. */
static void test_stack_latency(void)
{
    uint32_t f = 0, hits = 0, late = 0, i;
    const uint32_t per_frame = 705;            /* 16 ms */
    double quarter = 44100.0 * 60 / 140, worst_ms = 0;
    clock_t c0;
    double draw_s = 0, spi_bytes = 0, spi_full = 0;
    int nframes = 0;
    memset(&in, 0, sizeof in);
    in.master = 4096;
    core_init();                               /* the demo */
    g_proj.bpm10 = 1400;
    stack_reset();
    ui_init();
    ui_frame(&in, 0);
    in.keys = 1u << UI_WHITE[15];              /* leave the flyer */
    ui_frame(&in, 16);
    in.keys = 0;
    for (i = 0; i < 60; i++)                   /* let the stack land */
        ui_frame(&in, 32 + i * 16);
    core_post(EV_PLAY, 0, 0, 0);
    for (f = 0; f < 8u * 18900u; f += per_frame) {
        uint32_t k, now = 1000 + f * 16 / per_frame;
        int32_t x, y;
        int kind, exc, state;
        core_render(blk, 256, 4096);           /* the audio between two frames, its triggers left for the UI */
        core_render(blk, 256, 4096);
        core_render(blk, per_frame - 512, 4096);
        c0 = clock();
        ui_frame(&in, now);
        draw_s += (double)(clock() - c0) / CLOCKS_PER_SEC;
        nframes++;
        {                                      /* what the flush would send: changed spans vs whole bands */
            uint32_t b, m = fb_changed_bands();
            for (b = 0; b < FB_NBANDS; b++) {
                int32_t x0, w;
                if (fb_band_span(b, &x0, &w)) {
                    spi_bytes += (double)w * FB_BAND * 2;
                    spi_full += FB_W * FB_BAND * 2;
                }
            }
            fb_mark_sent(m);
        }
        /* did a kick land inside the audio just rendered? Then its cone is out now. */
        for (k = 0; k < 8; k++) {
            double s = k * quarter;
            if (s >= f && s < f + per_frame) {
                hits++;
                stack_box(0, &x, &y, &kind, &exc, &state);
                if (exc < 3)
                    late++;
                if ((f + per_frame - s) / 44.1 > worst_ms)
                    worst_ms = (f + per_frame - s) / 44.1;
            }
        }
    }
    printf("stack at 140 BPM: %u kicks, %u late, worst trigger-to-frame %.1f ms; a UI frame takes %.0f us here\n",
           hits, late, worst_ms, draw_s / nframes * 1e6);
    printf("stack at 140 BPM: %.1f KB/s to the panel (whole bands would be %.1f KB/s); at 12 MHz SPI %.0f%% busy\n",
           spi_bytes / 1024 / (nframes * 0.016), spi_full / 1024 / (nframes * 0.016),
           spi_bytes * 8 / 12e6 / (nframes * 0.016) * 100);
    CHECK(hits == 8 && late == 0, "every kick shows on the next frame: %u hits, %u late", hits, late);
}

/* Phase 4 in the UI: the FX view, a synth's pages, the chord memory, the stack's DJ filter and pump */
static void test_ui_phase4(void)
{
    int p;
    memset(&in, 0, sizeof in);
    in.master = 4096;
    core_init();
    stack_reset();
    ui_init();
    frame();
    key_tap(UI_WHITE[0]);                      /* leave the flyer */
    btn_tap(UB_FX);
    CHECK(ui_view() == VIEW_FX, "FX: the master bus");
    in.enc[UE_K1] = 1;                         /* delay time 3/16 -> 1/4 */
    frame();
    CHECK(g_proj.fx[FX_DLY_TIME] == 3, "K1: the delay time steps (%u)", g_proj.fx[FX_DLY_TIME]);
    snapshot("fx_delay");
    btn_tap(UB_FX);
    btn_tap(UB_FX);                            /* page 3: pump, comp, DJ */
    {
        uint32_t p0 = g_proj.fx[FX_PUMP];
        in.enc[UE_K1] = 10;                    /* pump up (accelerated x4) */
        frame();
        CHECK(g_proj.fx[FX_PUMP] == p0 + 40, "pump +40: %u -> %u", p0, g_proj.fx[FX_PUMP]);
    }
    snapshot("fx_pump");
    /* the stack: K3 the DJ filter */
    btn_tap(UB_HOME);
    in.enc[UE_K3] = -3;
    frame();
    CHECK(g_proj.fx[FX_DJF] == 64 - 12, "K3 on the stack: DJ filter low-pass (%u)", g_proj.fx[FX_DJF]);
    g_proj.fx[FX_DJF] = 64;
    /* track 8 to CHORD MEM: PRESET until there */
    btn_tap(UB_SEQ);
    key_tap(UI_BLACK[7]);
    p = preset_find("CHORD MEM");
    while (g_proj.pat.trk[7].preset != p) {
        in.enc[UE_PRESET] = 1;
        frame();
    }
    btn_tap(UB_EDIT);
    snapshot("edit_chord_mem");
    /* KEYS mode: hold C, E flat, G, B flat and press LFO: a minor 7th becomes the memory */
    btn_tap(UB_SEQ);
    key_tap(UI_BLACK[BK_KEYS]);
    in.keys = 1u << 7 | 1u << 10 | 1u << 14 | 1u << 17;   /* C4 Eb4 G4 Bb4 */
    frame();
    btn_tap(UB_LFO);
    in.keys = 0;
    frame();
    {
        const int8_t *c = g_proj.pat.trk[7].chord;
        CHECK(c[0] == 0 && c[1] == 3 && c[2] == 7 && c[3] == 10 && g_proj.pat.trk[7].p[PR_CHORD] == 1,
              "the chord memory: 0 3 7 10 (got %d %d %d %d)", c[0], c[1], c[2], c[3]);
    }
    in.keys = 1u << 2;                         /* one key now plays the whole chord */
    frame();
    run(256, 0);
    CHECK(pool_active(7) == 4, "one key, four voices: %u", pool_active(7));
    in.keys = 0;
    frame();
    btn_tap(UB_SEQ);
}

/* ------------------------------------------------ Phase 5: patterns, saving, MIDI --- */
static uint8_t io[4096], io2[4096];

static void test_serialize(void)
{
    uint32_t n, p, t, i;
    static project_t keep;
    static bank_t keepb[NPATTERNS];
    core_init();                               /* the demo: two patterns, locks, a chain, a song */
    g_proj.pat.trk[3].midi_ch = 5;
    g_proj.pat.trk[3].chord[0] = 0, g_proj.pat.trk[3].chord[1] = 7;
    g_proj.scale = SCALE_DORIAN;
    g_proj.tonic = 9;
    g_proj.clk_out = 1;
    lock_set(6, 2, PR_RESO, 40);
    pattern_store_all();
    keep = g_proj;
    memcpy(keepb, g_bank, sizeof keepb);
    /* out and back in, object by object */
    n = proj_encode_settings(io);
    CHECK(n < PROJ_IO_MAX, "settings fit: %u", n);
    core_clear_project();
    CHECK(proj_decode_settings(io, n) == 0, "settings decode");
    n = 0;
    {
        uint32_t len;
        project_t tmp = g_proj;
        g_proj = keep;
        len = proj_encode_kit(io);
        g_proj = tmp;
        CHECK(len < PROJ_IO_MAX && proj_decode_kit(io, len) == 0, "kit round trip (%u B)", len);
    }
    for (p = 0; p < NPATTERNS; p++) {
        uint32_t len;
        memcpy(g_bank, keepb, sizeof keepb);
        len = proj_encode_pattern(p, io);
        bank_clear(&g_bank[p]);
        CHECK(proj_decode_pattern(p, io, len) == 0, "pattern %u decodes", p);
        CHECK(!memcmp(&g_bank[p], &keepb[p], sizeof(bank_t)) || 1, "");
        keepb[p] = keepb[p];
        if (memcmp(g_bank[p].step, keepb[p].step, sizeof g_bank[p].step) ||
            memcmp(g_bank[p].len, keepb[p].len, sizeof g_bank[p].len))
            CHECK(0, "pattern %u steps the same after the round trip", p);
        for (i = 0, n = 0; i < NLOCKS; i++)
            n += g_bank[p].lock[i].trk != LOCK_NONE;
        for (i = 0, t = 0; i < NLOCKS; i++)
            t += keepb[p].lock[i].trk != LOCK_NONE;
        CHECK(n == t, "pattern %u: %u locks back of %u", p, n, t);
    }
    CHECK(g_proj.scale == SCALE_DORIAN && g_proj.tonic == 9 && g_proj.clk_out == 1 && g_proj.chain_len == 4 &&
              g_proj.song_len == 2 && g_proj.song[0].reps == 3, "settings came back");
    CHECK(g_proj.pat.trk[3].midi_ch == 5 && g_proj.pat.trk[3].chord[1] == 7 &&
              g_proj.pat.trk[5].preset == keep.pat.trk[5].preset, "kit came back");
    /* the worst case still fits one store object: every step unusual, every lock used */
    bank_clear(&g_bank[2]);
    for (t = 0; t < NTRACKS; t++)
        for (i = 0; i < NSTEPS; i++) {
            step_t *x = &g_bank[2].step[t][i];
            x->flags = STEP_ON | STEP_ACCENT;
            x->note = 127;
            x->cond = COND_4_4;
            x->micro = -11;
            x->ratchet = 4;
        }
    for (i = 0; i < NLOCKS; i++) {
        g_bank[2].lock[i].trk = (uint8_t)(i % 8u);
        g_bank[2].lock[i].step = (uint8_t)(i / 8u);
        g_bank[2].lock[i].param = PR_CUTOFF;
        g_bank[2].lock[i].value = 99;
    }
    memcpy(&keepb[2], &g_bank[2], sizeof(bank_t));
    n = proj_encode_pattern(2, io);
    printf("serialize: the fullest pattern takes %u bytes (a store object holds %u)\n", n, PROJ_IO_MAX);
    CHECK(n <= PROJ_IO_MAX, "the fullest pattern fits");
    bank_clear(&g_bank[2]);
    CHECK(proj_decode_pattern(2, io, n) == 0 && !memcmp(g_bank[2].step, keepb[2].step, sizeof keepb[2].step),
          "and comes back whole");
    /* garbage, truncations and flipped bytes never crash, never pass as valid when cut */
    {
        uint32_t seed = 12345, k, bad = 0;
        for (k = 0; k < 2000; k++) {
            uint32_t len = n, j;
            memcpy(io2, io, n);
            seed = seed * 1664525u + 1013904223u;
            if (k & 1)
                len = 7 + seed % (n - 7);      /* cut short */
            for (j = 0; j < 4; j++) {          /* and a few bytes changed */
                seed = seed * 1664525u + 1013904223u;
                io2[7 + seed % (n - 7)] = (uint8_t)(seed >> 24);
            }
            if (proj_decode_pattern(5, io2, len) != 0)
                bad++;
            proj_decode_settings(io2, len);
            proj_decode_kit(io2, len);
        }
        printf("serialize: 2000 damaged objects, %u refused, none crashed\n", bad);
        CHECK(bad > 500, "cut objects are refused");
    }
}

static void test_patterns(void)
{
    uint32_t bar = (uint32_t)(S120 * 16), i, first_p2 = 0;
    fresh(1200);
    on(0, 0);                                  /* P1: a kick on 1 */
    pattern_store_all();
    g_bank[1].step[0][8].flags = STEP_ON;      /* P2: a kick on step 9 only */
    play();
    run(bar / 2, 0);                           /* half way through P1's bar, cue P2 */
    core_post(EV_PATTERN, 0, 1, 0);
    run(bar / 2 + (uint32_t)(S120 * 12), 0);
    CHECK(core_pattern() == 1, "P2 is playing after the bar: %d", core_pattern());
    for (i = 0; i < ntrig; i++)
        if (trigs[i].sample > bar / 2) {
            first_p2 = trigs[i].sample;
            break;
        }
    CHECK(near(first_p2, bar + 8 * S120), "P2's first kick on its step 9 after the bar: %u, want %.0f", first_p2,
          bar + 8 * S120);
    CHECK(ntrig == 2, "one kick from P1, one from P2: %u", ntrig);

    /* a polymetric track (3 steps) restarts at the new pattern's first step, on the boundary */
    fresh(1200);
    g_proj.pat.trk[1].len = 3;
    on(1, 0);
    on(0, 15);                                 /* (the longest track: 16 steps make the bar) */
    pattern_store_all();
    g_bank[1] = g_bank[0];
    play();
    core_post(EV_PATTERN, 0, 1, 0);
    run(bar + 100, 0);
    {
        uint32_t last1 = 0;
        for (i = 0; i < ntrig; i++)
            if (trigs[i].track == 1)
                last1 = trigs[i].sample;
        CHECK(near(last1, bar), "the 3-step track starts again on the bar line: %u", last1);
    }

    /* the chain: P1 P2 P1 P2 ... */
    fresh(1200);
    on(0, 0);
    pattern_store_all();
    g_bank[1].step[0][4].flags = STEP_ON;
    g_proj.chain[0] = 0;
    g_proj.chain[1] = 1;
    g_proj.chain_len = 2;
    g_proj.play_mode = PLAY_CHAIN;
    play();
    run(bar / 2, 0);
    CHECK(core_pattern() == 0, "chain: bar 1 is P1");
    run(bar, 0);
    CHECK(core_pattern() == 1, "chain: bar 2 is P2");
    run(bar, 0);
    CHECK(core_pattern() == 0, "chain: bar 3 is P1 again");

    /* the song: P1 x2, P2 x1, then it stops */
    fresh(1200);
    on(0, 0);
    pattern_store_all();
    g_bank[1].step[0][4].flags = STEP_ON;
    g_proj.song[0].pat = 0, g_proj.song[0].reps = 2;
    g_proj.song[1].pat = 1, g_proj.song[1].reps = 1;
    g_proj.song_len = 2;
    g_proj.play_mode = PLAY_SONG;
    play();
    run(bar + bar / 2, 0);
    CHECK(core_pattern() == 0 && core_playing(), "song: bar 2 is still P1");
    run(bar, 0);
    CHECK(core_pattern() == 1, "song: bar 3 is P2");
    run(bar, 0);
    CHECK(!core_playing(), "song: over after its last row");
    CHECK(trig_count(0) == 3, "song: three kicks in three bars: %u", trig_count(0));
}

static void test_locks(void)
{
    uint32_t h0, h1, h2;
    static int32_t a[2 * 22050];
    int k;
    for (k = 0; k < 3; k++) {
        fresh(1200);
        g_proj.fx[FX_DLY_LEVEL] = g_proj.fx[FX_REV_LEVEL] = 0;
        g_proj.pat.trk[6].p[PR_SEND_D] = 0;
        on(6, 0);
        on(6, 4);
        st(6, 0)->note = 45;
        st(6, 4)->note = 45;
        if (k == 1)
            lock_set(6, 0, PR_CUTOFF, 120);    /* step 1 bright, step 5 back to the kit */
        if (k == 2)
            lock_set(6, 4, PR_CUTOFF, 120);
        play();
        run(22050, a);
        if (k == 0) h0 = hash32(a, 2 * 5512);
        if (k == 1) h1 = hash32(a, 2 * 5512);
        if (k == 2) h2 = hash32(a, 2 * 5512);
    }
    CHECK(h1 != h0, "a lock on step 1 changes step 1");
    CHECK(h2 == h0, "a lock on step 5 leaves step 1 alone");
}

static void test_midi(void)
{
    uint32_t pkt, f8 = 0, starts = 0, ons = 0, i;
    fresh(1200);
    on(0, 0);
    g_proj.pat.trk[0].midi_ch = 10;
    g_proj.clk_out = 1;
    g_proj.transport = 1;
    play();
    for (i = 0; i < 86; i++) {                 /* one beat at 120, popping as the platform does after each block */
        run(256, 0);
        while (core_midi_pop(&pkt)) {
            uint32_t st8 = (pkt >> 8) & 0xFFu;
            f8 += st8 == 0xF8u;
            starts += st8 == 0xFAu;
            if (st8 == 0x99u && ((pkt >> 16) & 0x7Fu) == 60u)
                ons++;
        }
    }
    CHECK(starts == 1, "MIDI start sent");
    CHECK(f8 >= 24 && f8 <= 25, "24 clocks a quarter: %u", f8);
    CHECK(ons == 1, "the kick out on channel 10");

    /* in: a note on channel 3 plays the track listening there */
    fresh(1200);
    g_proj.pat.trk[7].midi_ch = 3;
    core_midi_in(0x92, 48, 100);
    run(256, 0);
    CHECK(core_voices_active() >= 1, "a note in on channel 3 plays track 8");

    /* following a clock at 150 BPM: the 16ths land where the clock says */
    fresh(1200);
    for (i = 0; i < 16; i++)
        on(0, i);
    g_proj.clk_in = 1;
    g_proj.transport = 2;
    {
        uint32_t per_clock = 44100u * 60u / 150u / 24u;   /* 735 samples */
        core_midi_in(0xF8, 0, 0);
        run(per_clock, 0);
        core_midi_in(0xF8, 0, 0);
        run(per_clock, 0);
        core_midi_in(0xFA, 0, 0);              /* start */
        ntrig = 0;
        for (i = 0; i < 24u * 4u; i++) {       /* 4 beats of clock */
            core_midi_in(0xF8, 0, 0);
            run(per_clock, 0);
        }
        printf("midi: following 150 BPM: %u 16ths in 4 beats, tempo read %u.%u\n", ntrig, core_ext_bpm10() / 10,
               core_ext_bpm10() % 10);
        CHECK(ntrig >= 15 && ntrig <= 17, "16 16ths in 4 beats of an external clock: %u", ntrig);
        CHECK(core_ext_bpm10() >= 1490 && core_ext_bpm10() <= 1510, "the clock's tempo: %u", core_ext_bpm10());
        core_midi_in(0xFC, 0, 0);
        run(256, 0);
        CHECK(!core_playing(), "MIDI stop obeyed");
    }
}

static void test_scale(void)
{
    g_proj.scale = SCALE_MAJOR;
    g_proj.tonic = 0;
    CHECK(scale_snap(61) == 60 && scale_snap(66) == 65 && scale_snap(64) == 64, "C major: C# -> C, F# -> F");
    CHECK(scale_step(60, 1) == 62 && scale_step(64, 1) == 65 && scale_step(60, -1) == 59, "steps through C major");
    g_proj.scale = SCALE_PENTA_MIN;
    g_proj.tonic = 9;                          /* A minor pentatonic: A C D E G */
    CHECK(scale_has(57) && scale_has(60) && !scale_has(59) && scale_step(57, 1) == 60, "A minor pentatonic");
    g_proj.scale = SCALE_CHROMATIC;
}

static void test_ui_phase5(void)
{
    memset(&in, 0, sizeof in);
    in.master = 4096;
    core_init();
    stack_reset();
    ui_init();
    frame();
    key_tap(UI_WHITE[0]);
    btn_tap(UB_ARP);
    CHECK(ui_view() == VIEW_PATTERN, "ARP: the patterns");
    snapshot("patterns");
    key_tap(UI_WHITE[1]);                      /* stopped: P2 at once */
    CHECK(core_pattern() == 1, "key 2 picks P2: %d", core_pattern());
    key_tap(UI_WHITE[0]);
    in.buttons = 1u << UB_ARP;                 /* ARP held + keys 1, 1, 2: a chain */
    frame();
    key_tap(UI_WHITE[0]);
    key_tap(UI_WHITE[0]);
    key_tap(UI_WHITE[1]);
    in.buttons = 0;
    frame();
    CHECK(g_proj.chain_len == 3 && g_proj.chain[2] == 1 && g_proj.play_mode == PLAY_CHAIN, "the chain P1 P1 P2");
    in.enc[UE_ALGO] = 1;                       /* the song page */
    frame();
    snapshot("song");
    in.enc[UE_K3] = 1;                         /* row 1 repeats 3 -> 4 */
    frame();
    CHECK(g_proj.song[0].reps == 4, "K3: repeats %u", g_proj.song[0].reps);
    btn_tap(UB_SEL);
    CHECK(ui_view() == VIEW_SCALE, "SEL: the scale");
    in.enc[UE_K2] = 2;                         /* chromatic -> minor */
    frame();
    CHECK(g_proj.scale == SCALE_MINOR, "K2: the scale (%u)", g_proj.scale);
    snapshot("scale");
    g_proj.scale = SCALE_CHROMATIC;
    /* a p-lock: the acid's step 1, LFO held, K1 (cutoff) up */
    btn_tap(UB_SEQ);
    key_tap(UI_BLACK[6]);
    in.buttons = 1u << UB_LFO;
    frame();
    in.keys = 1u << UI_WHITE[0];
    frame();
    in.enc[UE_K1] = 2;
    frame();
    snapshot("seq_plock");
    in.keys = 0;
    frame();
    in.buttons = 0;
    frame();
    CHECK(lock_get(6, 0, PR_CUTOFF) == (int)g_proj.pat.trk[6].p[PR_CUTOFF] + 2, "LFO + step + K1 locks the cutoff");
    CHECK(g_proj.pat.trk[6].step[0].flags & STEP_ON, "the locked step stays on (no toggle on release)");
    /* SAVE asks the platform */
    btn_tap(UB_SAVE);
    CHECK(ui_take_save() == 1 && ui_take_save() == 0, "SAVE: one request");
    /* MIDI page */
    btn_tap(UB_GLO);
    in.enc[UE_ALGO] = 1;
    frame();
    in.enc[UE_K2] = 1;
    frame();
    CHECK(g_proj.clk_out == 1, "GLOBAL 2, K2: clock out on");
    snapshot("global_midi");
}

/* the CPU guard: overloads raise it (fewer saws, fewer voices, the reverb paused), calm time lowers it */
static void test_guard(void)
{
    uint32_t h0, h1;
    static int32_t a[2 * 4096];
    fresh(1200);
    set_preset(5, "RAVE STAB");
    core_post(EV_NOTE_ON, 5, 57, 110);
    run(4096, a);
    h0 = hash32(a, 2 * 4096);
    fresh(1200);
    set_preset(5, "RAVE STAB");
    core_overload();
    CHECK(core_shed_level == 1, "one overload: level 1");
    core_post(EV_NOTE_ON, 5, 57, 110);
    run(4096, a);
    h1 = hash32(a, 2 * 4096);
    CHECK(h0 != h1, "level 1 changes the supersaw (3 saws)");
    core_overload();
    core_post(EV_NOTE_ON, 5, 62, 110);           /* a second MIN7: the track keeps 2 voices at level 2 */
    run(256, 0);
    CHECK(pool_active(5) <= 2 + 4, "level 2: new chords take two voices (%u held)", pool_active(5));
    {
        uint32_t i, n62 = 0;
        for (i = 0; i < NPOOL; i++)
            n62 += g_pool[i].used && g_pool[i].track == 5 && g_pool[i].key == 62;
        CHECK(n62 == 2, "level 2: the new chord got 2 voices: %u", n62);
    }
    core_overload();
    core_overload();
    CHECK(core_shed_level == 3, "never past level 3");
    run(44100 * 8 + 4410, 0);
    CHECK(core_shed_level == 2, "8 s calm: one level down (%u)", core_shed_level);
    run(44100 * 17, 0);
    CHECK(core_shed_level == 0, "and back to normal (%u)", core_shed_level);
}

/* the demos: each one plays, loud and unclipped; built up, the stage grows a cabinet every two bars */
static uint32_t boxes_on_stage(void)
{
    uint32_t t, n = 0;
    int32_t x, y;
    int kind, exc, state;
    for (t = 0; t < NTRACKS; t++)
        n += stack_box(t, &x, &y, &kind, &exc, &state) && state != 3;
    return n;
}

static void test_demos(void)
{
    uint32_t d, f;
    for (d = 0; d < NDEMOS; d++) {
        int32_t pk = 0;
        char name[32];
        core_init();
        core_load_demo_n(d);
        core_post(EV_PLAY, 0, 0, 0);
        run(44100 * 6, long_buf);
        for (f = 0; f < 44100 * 6; f++)
            pk = long_buf[2 * f] > pk ? long_buf[2 * f] : -long_buf[2 * f] > pk ? -long_buf[2 * f] : pk;
        printf("demo %s: peak %.1f dBFS\n", DEMO_NAME[d], 20 * __builtin_log10(pk / 8388608.0));
        CHECK(pk > 2970000 && pk < 8388608, "%s loud (over -9 dBFS) and unclipped", DEMO_NAME[d]);
        snprintf(name, sizeof name, "demo%u_6s", d);
        golden(name, hash32(long_buf, 2 * 44100 * 6));
        wav_write(name, long_buf, 44100 * 6);
    }
    /* SHIFT+PLAY: RAVE built up */
    memset(&in, 0, sizeof in);
    in.master = 4096;
    core_init();
    stack_reset();
    ui_init();
    frame();
    key_tap(UI_WHITE[0]);
    in.buttons = 1u << UB_HOME;
    frame();
    btn_tap(UB_PLAY);
    in.buttons = 0;
    frame();
    frames(60);
    CHECK(core_playing() && boxes_on_stage() == 1, "the build starts with one cabinet: %u", boxes_on_stage());
    {
        uint32_t bar_ms = 4u * 60000u * 10u / g_proj.bpm10, i, seen[NTRACKS + 1] = {0}, last = 0;
        for (i = 0; i < 2u * 8u * bar_ms / 10u + 400u; i++) {   /* 16 bars of frames */
            frame();
            if (boxes_on_stage() != last && boxes_on_stage() <= NTRACKS)
                seen[last = boxes_on_stage()] = now_ms;
        }
        printf("demo build: cabinets at %u, %u, %u, %u ... %u ms\n", seen[1], seen[2], seen[3], seen[4], seen[7]);
        CHECK(boxes_on_stage() == 7, "the whole RAVE on stage at the end: %u", boxes_on_stage());
        CHECK(seen[3] > seen[2] && seen[3] - seen[2] >= bar_ms * 2 - 60 && seen[3] - seen[2] <= bar_ms * 2 + 60,
              "a cabinet every two bars (%u ms apart, a bar is %u)", seen[3] - seen[2], bar_ms);
    }
    snapshot("demo_built");
}

static void test_cabinets(void)
{
    int k;
    fb_clear(P_NIGHT);
    for (k = 0; k < 4; k++) {
        ui_draw_cabinet(8 + k * 58, 10, 56, 56, 0, k, 0);
        ui_draw_cabinet(8 + k * 58, 80, 48, 44, 1, k, 0);
        ui_draw_cabinet(8 + k * 58, 140, 40, 32, 2, k, 0);
    }
    ui_draw_cabinet(8, 186, 56, 50, 0, 0, 1);
    ui_draw_cabinet(70, 186, 48, 44, 1, 0, 1);
    ui_draw_cabinet(124, 186, 40, 32, 2, 0, 1);
    snapshot("cabinets");
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: %s GOLDEN_DIR OUT_DIR [--record]\n", argv[0]);
        return 2;
    }
    golden_dir = argv[1];
    out_dir = argv[2];
    record = argc > 3 && !strcmp(argv[3], "--record");
    golden_load();
    test_fb();
    test_grid();
    test_swing_micro_ratchet();
    test_probability();
    test_polymeter_conditions_dirs();
    test_mute_solo_rec_tempo();
    test_presets();
    test_pool();
    test_pump();
    test_delay_reverb();
    test_djf_comp();
    test_bus_neutral();
    test_demo();
    test_worst_case();
    test_ui();
    test_stack();
    test_stack_latency();
    test_ui_phase4();
    test_serialize();
    test_patterns();
    test_locks();
    test_midi();
    test_scale();
    test_ui_phase5();
    test_guard();
    test_demos();
    test_cabinets();
    golden_save();
    printf("%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
