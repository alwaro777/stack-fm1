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
        uint32_t last = 0, frames = 44100 * 2;
        char name[40];
        fresh(1200);
        preset_apply(&g_proj.pat.trk[0], p);
        run(4096, 0);                          /* MASTER's glide up from silence, first */
        core_post(EV_NOTE_ON, 0, PRESETS[p].engine == ENGINE_ACID ? 45 : 60, 120);
        run(44100 / 4, long_buf);
        core_post(EV_NOTE_OFF, 0, 45, 0);
        run(frames - 44100 / 4, &long_buf[2 * (44100 / 4)]);
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
        snprintf(name, sizeof name, "preset_%02u", p);
        golden(name, hash32(long_buf, 2 * frames));
        wav_write(name, long_buf, frames);
    }
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
    printf("worst case: 4 s of audio in %.1f ms on this host (%.1f%% of real time)\n", sec * 1000, sec / 4 * 100);
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
    CHECK(ui_view() == VIEW_SEQ, "a key leaves the flyer for the sequencer");
    CHECK(!(g_proj.pat.trk[0].step[3].flags & STEP_ON), "the key that left the flyer edited nothing");
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
    /* the mixer: a HOME tap; black key 3 mutes track 3 */
    btn_tap(UB_HOME);
    CHECK(ui_view() == VIEW_MIX, "HOME tap: mixer");
    key_tap(UI_BLACK[2]);
    CHECK(core_mute == 1u << 2, "black key 3 mutes track 3 in the mixer");
    snapshot("mix_mute3");
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
    test_demo();
    test_worst_case();
    test_ui();
    test_cabinets();
    golden_save();
    printf("%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
