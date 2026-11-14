/* SPDX-License-Identifier: GPL-3.0-or-later
 * STACK — F1 UI: the boot flyer and the control I/O test view. Phase 2 adds the sequencer views, Phase 3 the sound
 * system. Hardware-free: see stk_ui.h. */
#include "fb.h"
#include "ui.h"
#include "../core/core.h"

#define SPLASH_MS 3500u          /* the flyer stays this long unless a control is touched */
#define LOG_LINES 6
#define LOG_LEN 28

static const char *const BTN_NAME[UI_NBUTTONS] = {"FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME",
                                                  "SAVE", "ARP", "SEQ", "PLAY", "REC", "OCT-", "OCT+"};
static const char *const ENC_NAME[UI_NENC] = {"SEL", "ALG", "PRE", "K1", "K2", "K3", "K4"};
static const char *const NOTE_NAME[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

static struct {
    int view;
    uint32_t t0;                 /* boot time (the first frame) */
    uint32_t now;
    int started;
    ui_input_t prev;
    uint32_t ignore_keys, ignore_buttons;   /* held when the flyer was left: no event until released */
    int32_t enc_total[UI_NENC];
    int32_t octave;              /* the test keyboard's octave, -2..+2 */
    char log[LOG_LINES][LOG_LEN];
    uint32_t log_n;
    uint32_t cpu_pct, xruns;
    int dirty;
} stk_ui;

/* ----------------------------------------------------------- small text --- */
static void ui_cat(char *d, const char *s, uint32_t cap)
{
    uint32_t n = 0;
    while (d[n] && n < cap - 1u)
        n++;
    while (*s && n < cap - 1u)
        d[n++] = *s++;
    d[n] = 0;
}

static void ui_int(char *d, int32_t v, uint32_t cap)   /* append a signed decimal */
{
    char t[12];
    int n = 0;
    uint32_t u = (uint32_t)(v < 0 ? -v : v);
    do
        t[n++] = (char)('0' + u % 10u);
    while ((u /= 10u) && n < 11);
    if (v < 0)
        t[n++] = '-';
    {
        char r[13];
        int i;
        for (i = 0; i < n; i++)
            r[i] = t[n - 1 - i];
        r[n] = 0;
        ui_cat(d, r, cap);
    }
}

static void ui_log(const char *a, const char *b, const char *c)
{
    char *l = stk_ui.log[stk_ui.log_n % LOG_LINES];
    l[0] = 0;
    ui_cat(l, a, LOG_LEN);
    ui_cat(l, " ", LOG_LEN);
    ui_cat(l, b, LOG_LEN);
    if (c && *c) {
        ui_cat(l, " ", LOG_LEN);
        ui_cat(l, c, LOG_LEN);
    }
    stk_ui.log_n++;
}

static void ui_note_text(char *d, int32_t note, uint32_t cap)
{
    d[0] = 0;
    ui_cat(d, NOTE_NAME[note % 12], cap);
    ui_int(d, note / 12 - 1, cap);
}

/* --------------------------------------------------------------- splash --- */
void ui_draw_splash(uint32_t t)
{
    /* 128 BPM: the subs push on each beat, the tops on the off-beats */
    uint32_t beat_ms = 60000u / 128u, ph = t % beat_ms, off = (t + beat_ms / 2u) % beat_ms;
    int ex_beat = ph < 60u ? 3 : ph < 120u ? 2 : ph < 200u ? 1 : 0;
    int ex_off = off < 60u ? 2 : off < 140u ? 1 : 0;
    int32_t i;

    fb_vgrad(0, 10, FB_W, 186, P_NIGHT, P_PURPLE);
    fb_stripes(0, 0, FB_W, 10, P_YELLOW, P_BLACK, 12);
    fb_stripes(0, 230, FB_W, 10, P_YELLOW, P_BLACK, 12);
    fb_text_center(15, "* FM-1 GROOVEBOX FIRMWARE *", P_LIME, 1);
    /* the title: chunky letters with a magenta drop and a black outline-ish second drop */
    {
        const char *title = "STACK";
        int32_t s = 7, x = (FB_W - fb_text_w(title, s)) / 2;
        fb_text(x + 5, 31, title, P_BLACK, s);
        fb_text(x + 3, 29, title, P_MAGENTA, s);
        fb_text(x, 26, title, P_YELLOW, s);
    }
    if ((t / 400u) & 1u)          /* (the gradient above already cleared it on the other half-second) */
        fb_text_center(80, "PRESS ANY KEY", P_WHITE, 1);
    /* a little stack: 3 tops, 3 mids, 4 subs, on a plywood stage */
    for (i = 0; i < 3; i++)
        ui_draw_cabinet(73 + i * 32, 94, 30, 18, 2, ex_off, 0);
    for (i = 0; i < 3; i++)
        ui_draw_cabinet(63 + i * 39, 113, 37, 30, 1, ex_beat > 1 ? 1 : 0, 0);
    for (i = 0; i < 4; i++)
        ui_draw_cabinet(28 + i * 47, 144, 45, 50, 0, ex_beat, 0);
    fb_dither(0, 196, FB_W, 18, P_WOOD, P_DORANGE, 5);
    fb_hline(0, 196, FB_W, P_BLACK);
    fb_rect(0, 214, FB_W, 16, P_BLACK);
    fb_text_center(218, "RAVE - DUB - ACID - V0.1", P_ORANGE, 1);
}

/* ---------------------------------------------------------------- I/O view --- */
static void io_bar(int32_t x, int32_t y, int32_t w, int32_t h, int32_t v, int32_t vmax, uint8_t c)
{
    int32_t f = vmax > 0 ? (v < 0 ? 0 : v > vmax ? vmax : v) * (w - 2) / vmax : 0;
    fb_frame(x, y, w, h, P_GRAY);
    fb_rect(x + 1, y + 1, f, h - 2, c);
}

static void draw_io(void)
{
    static const uint8_t WK[16] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};
    static const uint8_t BK[11] = {1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25};
    static const uint8_t BK_AFTER[11] = {0, 1, 2, 4, 5, 7, 8, 9, 11, 12, 14};   /* the white key each follows */
    char t[32];
    int32_t i;
    const ui_input_t *in = &stk_ui.prev;

    fb_clear(P_NIGHT);
    /* header */
    fb_rect(0, 0, FB_W, 13, P_ORANGE);
    fb_text(4, 3, "STACK  I/O TEST", P_BLACK, 1);
    t[0] = 0;
    ui_cat(t, "CPU ", sizeof t);
    ui_int(t, (int32_t)stk_ui.cpu_pct, sizeof t);
    ui_cat(t, "% XR ", sizeof t);
    ui_int(t, (int32_t)stk_ui.xruns, sizeof t);
    fb_text(FB_W - 4 - fb_text_w(t, 1), 3, t, P_BLACK, 1);
    /* buttons: 7 x 2, lit while held */
    for (i = 0; i < UI_NBUTTONS; i++) {
        int32_t x = 5 + (i % 7) * 33, y = 17 + (i / 7) * 16;
        int on = (int)(in->buttons >> i & 1u);
        fb_rect(x, y, 31, 14, on ? P_YELLOW : P_BLACK);
        fb_frame(x, y, 31, 14, on ? P_YELLOW : P_DGRAY);
        fb_text(x + (31 - fb_text_w(BTN_NAME[i], 1)) / 2, y + 4, BTN_NAME[i], on ? P_BLACK : P_LGRAY, 1);
    }
    /* encoders: name, detents since boot, a dial */
    for (i = 0; i < UI_NENC; i++) {
        int32_t x = 5 + i * 33, cx = x + 15, cy = 70;
        int32_t a = ((stk_ui.enc_total[i] % 16) + 16) % 16;   /* 16 positions round the dial */
        static const int8_t DX[16] = {0, 3, 5, 6, 7, 6, 5, 3, 0, -3, -5, -6, -7, -6, -5, -3};
        static const int8_t DY[16] = {-7, -6, -5, -3, 0, 3, 5, 6, 7, 6, 5, 3, 0, -3, -5, -6};
        fb_text(x + (31 - fb_text_w(ENC_NAME[i], 1)) / 2, 51, ENC_NAME[i], P_LGRAY, 1);
        fb_disc(cx, cy, 9, P_DGRAY);
        fb_ring(cx, cy, 9, P_GRAY);
        fb_disc(cx + DX[a] * 6 / 7, cy + DY[a] * 6 / 7, 1, P_LIME);
        t[0] = 0;
        ui_int(t, stk_ui.enc_total[i], sizeof t);
        fb_text(x + (31 - fb_text_w(t, 1)) / 2, 83, t, P_WHITE, 1);
    }
    /* MASTER */
    fb_text(5, 97, "MASTER", P_LGRAY, 1);
    io_bar(48, 96, 150, 9, (int32_t)in->master, 4096, P_ORANGE);
    t[0] = 0;
    ui_int(t, (int32_t)(in->master * 100u / 4096u), sizeof t);
    ui_cat(t, "%", sizeof t);
    fb_text(204, 97, t, P_WHITE, 1);
    /* the keyboard: 16 white keys, 11 black keys over them; held keys lit */
    for (i = 0; i < 16; i++) {
        int on = (int)(in->keys >> WK[i] & 1u);
        fb_rect(8 + i * 14, 112, 13, 44, on ? P_LIME : P_WHITE);
    }
    for (i = 0; i < 11; i++) {
        int on = (int)(in->keys >> BK[i] & 1u);
        fb_rect(8 + BK_AFTER[i] * 14 + 9, 112, 9, 27, on ? P_MAGENTA : P_BLACK);
    }
    t[0] = 0;
    ui_cat(t, "OCT ", sizeof t);
    if (stk_ui.octave >= 0)
        ui_cat(t, "+", sizeof t);
    ui_int(t, stk_ui.octave, sizeof t);
    ui_cat(t, "  VOICES ", sizeof t);
    ui_int(t, (int32_t)core_voices_active(), sizeof t);
    fb_text(8, 160, t, P_LGRAY, 1);
    /* the last events */
    fb_rect(4, 172, FB_W - 8, 56, P_BLACK);
    for (i = 0; i < LOG_LINES; i++) {
        uint32_t k;
        if ((uint32_t)i >= stk_ui.log_n)
            break;
        k = (stk_ui.log_n - 1u - (uint32_t)i) % LOG_LINES;
        fb_text(8, 175 + i * 9, stk_ui.log[k], i == 0 ? P_YELLOW : P_GRAY, 1);
    }
    fb_text(4, 231, "HOME: FLYER   KEYS: TEST TONE", P_DGRAY, 1);
}

/* ------------------------------------------------------------------ input --- */
static int ui_touched(const ui_input_t *in)
{
    int i;
    if (in->buttons || in->keys)
        return 1;
    for (i = 0; i < UI_NENC; i++)
        if (in->enc[i])
            return 1;
    return 0;
}

static void io_input(const ui_input_t *in)
{
    uint32_t i, down, up;
    char t[12];
    down = in->buttons & ~stk_ui.prev.buttons;
    up = ~in->buttons & stk_ui.prev.buttons & ~stk_ui.ignore_buttons;
    stk_ui.ignore_buttons &= in->buttons;
    for (i = 0; i < UI_NBUTTONS; i++) {
        if (down >> i & 1u) {
            ui_log("BTN", BTN_NAME[i], "DOWN");
            if (i == UB_OCTDN && stk_ui.octave > -2)
                stk_ui.octave--;
            else if (i == UB_OCTUP && stk_ui.octave < 2)
                stk_ui.octave++;
            else if (i == UB_HOME) {
                stk_ui.view = VIEW_SPLASH;
                stk_ui.t0 = stk_ui.now;
                core_post(EV_ALL_OFF, 0, 0);
            }
        } else if (up >> i & 1u) {
            ui_log("BTN", BTN_NAME[i], "UP");
        }
    }
    down = in->keys & ~stk_ui.prev.keys;
    up = ~in->keys & stk_ui.prev.keys & ~stk_ui.ignore_keys;
    stk_ui.ignore_keys &= in->keys;
    for (i = 0; i < UI_NKEYS; i++) {
        int32_t note = 53 + (int32_t)i + 12 * stk_ui.octave;   /* key 0 = F3 = MIDI 53 */
        if (down >> i & 1u) {
            ui_note_text(t, note, sizeof t);
            core_post(EV_NOTE_ON, (uint32_t)note, 100u);
            ui_log("KEY", t, "ON");
        } else if (up >> i & 1u) {
            ui_note_text(t, note, sizeof t);
            core_post(EV_NOTE_OFF, (uint32_t)note, 0u);
            ui_log("KEY", t, "OFF");
        }
    }
    for (i = 0; i < UI_NENC; i++)
        if (in->enc[i]) {
            stk_ui.enc_total[i] += in->enc[i];
            t[0] = 0;
            if (in->enc[i] > 0)
                ui_cat(t, "+", sizeof t);
            ui_int(t, in->enc[i], sizeof t);
            ui_log("ENC", ENC_NAME[i], t);
        }
}

/* ------------------------------------------------------------------- API --- */
void ui_init(void)
{
    uint32_t i;
    stk_ui.view = VIEW_SPLASH;
    stk_ui.started = 0;
    stk_ui.octave = 0;
    stk_ui.log_n = 0;
    stk_ui.cpu_pct = stk_ui.xruns = 0;
    for (i = 0; i < UI_NENC; i++)
        stk_ui.enc_total[i] = 0;
    stk_ui.prev.buttons = stk_ui.prev.keys = 0;
    stk_ui.prev.master = 0;
    stk_ui.dirty = 1;
}

int ui_frame(const ui_input_t *in, uint32_t now)
{
    uint32_t i;
    int changed = 0;
    stk_ui.now = now;
    if (!stk_ui.started) {
        stk_ui.started = 1;
        stk_ui.t0 = now;
    }
    if (stk_ui.view == VIEW_SPLASH) {
        /* any new press (or a turn) leaves the flyer; so does time */
        int fresh = (in->buttons & ~stk_ui.prev.buttons) || (in->keys & ~stk_ui.prev.keys);
        for (i = 0; i < UI_NENC; i++)
            fresh |= in->enc[i] != 0;
        if (fresh || now - stk_ui.t0 >= SPLASH_MS) {
            stk_ui.view = VIEW_IO;
            stk_ui.dirty = 1;
            stk_ui.prev = *in;        /* the press that left the flyer does not count as a test event */
            stk_ui.ignore_keys = in->keys;
            stk_ui.ignore_buttons = in->buttons;
            for (i = 0; i < UI_NENC; i++)
                stk_ui.prev.enc[i] = 0;
        } else {
            ui_draw_splash(now - stk_ui.t0);   /* animated: every frame (only the cones' bands change) */
            stk_ui.prev = *in;
            return 1;
        }
    }
    if (ui_touched(in) || in->buttons != stk_ui.prev.buttons || in->keys != stk_ui.prev.keys) {
        io_input(in);
        stk_ui.dirty = 1;
    }
    if (in->master / 41u != stk_ui.prev.master / 41u)   /* 1 % steps */
        stk_ui.dirty = 1;
    stk_ui.prev = *in;
    for (i = 0; i < UI_NENC; i++)
        stk_ui.prev.enc[i] = 0;
    if (stk_ui.dirty && stk_ui.view == VIEW_IO) {
        draw_io();
        changed = 1;
    } else if (stk_ui.view == VIEW_SPLASH) {
        ui_draw_splash(now - stk_ui.t0);
        changed = 1;
    }
    stk_ui.dirty = 0;
    return changed;
}

void ui_leds(ui_leds_t *out)
{
    out->buttons = stk_ui.prev.buttons;
    out->keys = stk_ui.prev.keys;
    if (stk_ui.view == VIEW_SPLASH) {  /* the flyer: a light chasing along the white keys at 128 BPM */
        static const uint8_t WK[16] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};
        uint32_t step = ((stk_ui.now - stk_ui.t0) * 128u * 4u / 60000u) % 16u;
        out->keys |= 1u << WK[step];
    }
}

int ui_view(void) { return stk_ui.view; }

void ui_set_stats(uint32_t cpu_pct, uint32_t xruns)
{
    if (cpu_pct != stk_ui.cpu_pct || xruns != stk_ui.xruns) {
        stk_ui.cpu_pct = cpu_pct;
        stk_ui.xruns = xruns;
        stk_ui.dirty = 1;
    }
}

void ui_say(const char *a, const char *b)
{
    ui_log("SAY", a ? a : "", b);
    stk_ui.dirty = 1;
}
