/* SPDX-License-Identifier: GPL-3.0-or-later
 * STACK — host unit tests for core/ and ui/ (no platform). Built and run by `make test`.
 *
 *   test_main GOLDEN_DIR OUT_DIR [--record]
 *
 * Golden values (framebuffer and audio hashes) live in GOLDEN_DIR as one "name value" per line. A missing name is
 * recorded (and reported); --record rewrites them all. Snapshots are written to OUT_DIR as PNGs to look at. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../app/core/core.h"
#include "../../app/ui/fb.h"
#include "../../app/ui/ui.h"
#include "png.h"

static int fails, checks;
static const char *golden_dir, *out_dir;
static int record;

#define CHECK(cond, ...)                                                                    \
    do {                                                                                    \
        checks++;                                                                           \
        if (!(cond)) {                                                                      \
            fails++;                                                                        \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                     \
            printf(__VA_ARGS__);                                                            \
            printf("\n");                                                                   \
        }                                                                                   \
    } while (0)

/* --------------------------------------------------------------- goldens --- */
#define MAXG 64
static char gname[MAXG][48];
static uint32_t gval[MAXG];
static int gn, gdirty;

static void golden_load(void)
{
    char path[512], name[48];
    unsigned v;
    FILE *f;
    snprintf(path, sizeof path, "%s/golden.txt", golden_dir);
    if (!(f = fopen(path, "r")))
        return;
    while (gn < MAXG && fscanf(f, "%47s %x", name, &v) == 2) {
        strcpy(gname[gn], name);
        gval[gn++] = v;
    }
    fclose(f);
}

static void golden_save(void)
{
    char path[512];
    FILE *f;
    int i;
    if (!gdirty)
        return;
    snprintf(path, sizeof path, "%s/golden.txt", golden_dir);
    if (!(f = fopen(path, "w")))
        return;
    for (i = 0; i < gn; i++)
        fprintf(f, "%s %08x\n", gname[i], gval[i]);
    fclose(f);
}

static void golden(const char *name, uint32_t v)
{
    int i;
    for (i = 0; i < gn; i++)
        if (!strcmp(gname[i], name)) {
            if (record && gval[i] != v) {
                gval[i] = v;
                gdirty = 1;
                printf("golden %s re-recorded %08x\n", name, v);
            }
            CHECK(gval[i] == v, "golden %s: got %08x, expected %08x", name, v, gval[i]);
            return;
        }
    if (gn < MAXG) {
        strcpy(gname[gn], name);
        gval[gn++] = v;
        gdirty = 1;
        printf("golden %s recorded %08x\n", name, v);
    }
}

static void snapshot(const char *name)
{
    static uint8_t rgb[FB_W * FB_H * 3];
    char path[512];
    int32_t x, y;
    for (y = 0; y < FB_H; y++)
        for (x = 0; x < FB_W; x++) {
            const uint8_t *c = fb_palette_rgb[fb_get(x, y)];
            memcpy(&rgb[(y * FB_W + x) * 3], c, 3);
        }
    snprintf(path, sizeof path, "%s/%s.png", out_dir, name);
    png_write_rgb(path, rgb, FB_W, FB_H);
    golden(name, fb_hash());
}

/* ------------------------------------------------------------------- fb --- */
static void test_fb(void)
{
    uint32_t m;
    fb_clear(P_BLACK);
    fb_pixel(3, 4, P_YELLOW);
    fb_pixel(4, 4, P_LIME);
    CHECK(fb_get(3, 4) == P_YELLOW && fb_get(4, 4) == P_LIME, "nibble packing");
    CHECK(fb_get(5, 4) == P_BLACK, "neighbour untouched");
    fb_pixel(-1, 0, P_RED);           /* clipped: no crash, no write */
    fb_pixel(240, 239, P_RED);
    fb_rect(-10, -10, 15, 15, P_WHITE);
    CHECK(fb_get(4, 4) == P_WHITE && fb_get(5, 5) == P_BLACK, "rect clipping");
    CHECK(fb_text_w("AB", 1) == 11, "text width %d", (int)fb_text_w("AB", 1));
    CHECK(fb_text_w("", 3) == 0, "empty text width");
    /* dirty bands: nothing changed after a full send; one pixel marks one band */
    fb_invalidate();
    CHECK(fb_changed_bands() == (1u << FB_NBANDS) - 1u, "invalid -> all bands");
    fb_mark_sent((1u << FB_NBANDS) - 1u);
    CHECK(fb_changed_bands() == 0, "after send -> none");
    fb_pixel(100, 20, P_MAGENTA);     /* band 1 (rows 16..31) */
    m = fb_changed_bands();
    CHECK(m == 1u << 1, "one pixel -> band 1, got %x", m);
    fb_mark_sent(m);
    CHECK(fb_changed_bands() == 0, "band 1 sent");
    fb_pixel(100, 20, P_MAGENTA);     /* same value again: no change */
    CHECK(fb_changed_bands() == 0, "same pixel value -> nothing to send");
    /* a flush capped at 8 bands a frame (app.c): the rest goes next frame, and then nothing is left */
    fb_invalidate();
    fb_mark_sent(0xFFu);
    m = fb_changed_bands();
    CHECK(m == ((1u << FB_NBANDS) - 1u) - 0xFFu, "capped flush leaves bands 8..14, got %x", m);
    fb_mark_sent(m);
    CHECK(fb_changed_bands() == 0, "second capped flush completes the screen");
}

/* ----------------------------------------------------------------- audio --- */
static int32_t buf[2 * 4096];

static uint32_t audio_hash(const int32_t *p, uint32_t n)
{
    uint32_t h = 2166136261u, i;
    for (i = 0; i < n; i++) {
        uint32_t v = (uint32_t)p[i];
        h = (h ^ (v & 0xFF)) * 16777619u;
        h = (h ^ (v >> 8 & 0xFF)) * 16777619u;
        h = (h ^ (v >> 16 & 0xFF)) * 16777619u;
        h = (h ^ (v >> 24)) * 16777619u;
    }
    return h;
}

static void render(uint32_t frames, uint32_t master)   /* longer renders wrap round buf (4096 frames) */
{
    uint32_t f = 0;
    while (f < frames) {
        uint32_t n = frames - f > 256u ? 256u : frames - f;
        core_render(&buf[2 * (f % 4096u)], n, master);
        f += n;
    }
}

static void test_audio(void)
{
    uint32_t i, crossings = 0;
    int32_t maxv = 0;
    core_init();
    render(1024, 4096);
    for (i = 0; i < 2048u; i++)
        CHECK(buf[i] == 0, "silence without notes (frame %u = %d)", i / 2, buf[i]);
    CHECK(core_voices_active() == 0, "no voice active");

    core_init();
    CHECK(core_post(EV_NOTE_ON, 69, 127), "post A4");
    render(4096, 4096);               /* 92.9 ms */
    for (i = 1024; i < 4096u; i++) {  /* after the MASTER glide and the attack */
        int32_t s = buf[2 * i];
        if (s > maxv)
            maxv = s;
        if ((buf[2 * (i - 1)] < 0) != (s < 0))
            crossings++;
        CHECK(buf[2 * i] == buf[2 * i + 1], "mono test voice: L == R");
        if (buf[2 * i] != buf[2 * i + 1])
            break;
    }
    /* 3072 frames of 440 Hz at 44.1 kHz = 30.6 cycles = ~61 zero crossings (the octave partial adds none: it is a
     * quarter of the level and never flips the sign of the sum on its own) */
    CHECK(crossings >= 58 && crossings <= 64, "A4 zero crossings: %u", crossings);
    CHECK(maxv > (1 << 20) && maxv < (1 << 23), "level within 24-bit, audible: %d", maxv);
    golden("audio_a4_4096", audio_hash(buf, 2 * 4096));

    /* MASTER at zero: silence after the glide */
    render(4096, 0);
    CHECK(buf[2 * 4095] == 0, "MASTER 0 -> silent");

    /* deterministic stealing: 6 notes on 4 voices -> 4 active */
    core_init();
    for (i = 0; i < 6u; i++)
        core_post(EV_NOTE_ON, 60 + i, 100);
    render(256, 4096);
    CHECK(core_voices_active() == 4, "4 voices active, got %u", core_voices_active());
    core_post(EV_ALL_OFF, 0, 0);
    render(44100, 4096);
    CHECK(core_voices_active() == 0, "all released after 1 s, got %u", core_voices_active());

    /* the ring never blocks: 64 events fit, the 65th is refused */
    core_init();
    for (i = 0; i < STK_EVQ_LEN; i++)
        CHECK(core_post(EV_NOTE_OFF, 60, 0), "post %u", i);
    CHECK(!core_post(EV_NOTE_OFF, 60, 0), "full ring refuses");
    render(16, 4096);                 /* drained */
    CHECK(core_post(EV_NOTE_OFF, 60, 0), "drained ring accepts");
}

/* -------------------------------------------------------------------- UI --- */
static void test_ui(void)
{
    ui_input_t in;
    ui_leds_t leds;
    memset(&in, 0, sizeof in);
    core_init();
    ui_init();
    in.master = 4096;
    CHECK(ui_frame(&in, 1000) == 1, "first frame draws");
    CHECK(ui_view() == VIEW_SPLASH, "boot on the flyer");
    snapshot("splash_t0");
    ui_frame(&in, 1000 + 470);        /* a beat later: the cones move, the blink is on */
    snapshot("splash_t470");
    ui_leds(&leds);
    CHECK(leds.keys != 0, "flyer chases a key light");

    in.keys = 1u << 7;                /* C4 (key 7) leaves the flyer, without playing */
    ui_frame(&in, 1600);
    CHECK(ui_view() == VIEW_IO, "a key leaves the flyer");
    CHECK(core_voices_active() == 0, "the key that left the flyer is not a note");
    in.keys = 0;
    ui_frame(&in, 1620);
    in.keys = 1u << 7 | 1u << 11;     /* C4 + E4 */
    in.buttons = 1u << UB_PLAY;
    in.enc[UE_K2] = 3;
    ui_frame(&in, 1640);
    render(512, 4096);
    CHECK(core_voices_active() == 2, "two keys -> two voices, got %u", core_voices_active());
    in.enc[UE_K2] = 0;
    in.master = 2048;
    ui_frame(&in, 1660);
    snapshot("io_c4_e4_play");
    ui_leds(&leds);
    CHECK(leds.keys == (1u << 7 | 1u << 11) && leds.buttons == 1u << UB_PLAY, "held controls are lit");
    in.keys = 0;
    in.buttons = 1u << UB_HOME;
    ui_frame(&in, 1700);
    CHECK(ui_view() == VIEW_SPLASH, "HOME goes back to the flyer");
}

/* cabinets, alone: the stack's building blocks at their Phase 3 sizes */
static void test_cabinets(void)
{
    int k;
    fb_clear(P_NIGHT);
    for (k = 0; k < 4; k++) {
        ui_draw_cabinet(8 + k * 58, 10, 56, 56, 0, k, 0);
        ui_draw_cabinet(8 + k * 58, 80, 48, 44, 1, k, 0);
        ui_draw_cabinet(8 + k * 58, 140, 40, 32, 2, k, 0);
    }
    ui_draw_cabinet(8, 186, 56, 50, 0, 0, 1);    /* muted */
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
    test_audio();
    test_ui();
    test_cabinets();
    golden_save();
    printf("%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
