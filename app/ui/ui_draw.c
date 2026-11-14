/* SPDX-License-Identifier: GPL-3.0-or-later
 * STACK — the views, drawn into the 4 bpp framebuffer. 240 x 240: a 12 px header everywhere, then the view. */
#include "ui_int.h"

static const char *const TRACK_KEY[NTRACKS] = {"1", "2", "3", "4", "5", "6", "7", "8"};

/* ------------------------------------------------------------- pieces --- */
static int lamp(uint32_t t)                     /* 1 while track t's last hit is fresh */
{
    return ui.flash_t[t] != 0;
}

void ui_pattern_text(char *d, uint32_t cap)
{
    int c = core_cue();
    d[0] = 0;
    ui_cat(d, "P", cap);
    if (g_proj.cur_pat < 9)
        ui_cat(d, "0", cap);
    ui_int(d, g_proj.cur_pat + 1, cap);
    if (c >= 0 && c != g_proj.cur_pat) {
        ui_cat(d, ">", cap);
        if (c < 9)
            ui_cat(d, "0", cap);
        ui_int(d, c + 1, cap);
    }
    if (g_proj.play_mode == PLAY_CHAIN)
        ui_cat(d, " CHAIN", cap);
    else if (g_proj.play_mode == PLAY_SONG)
        ui_cat(d, " SONG", cap);
}

void ui_header(const char *left)
{
    char t[24];
    int32_t x;
    fb_rect(0, 0, FB_W, 12, P_ORANGE);
    fb_text(3, 3, left, P_BLACK, 1);
    t[0] = 0;
    {
        uint32_t b = core_ext_bpm10() ? core_ext_bpm10() : g_proj.bpm10;
        if (core_ext_bpm10())
            ui_cat(t, "EXT ", sizeof t);
        ui_int(t, (int32_t)(b / 10u), sizeof t);
        ui_cat(t, ".", sizeof t);
        ui_int(t, (int32_t)(b % 10u), sizeof t);
    }
    x = FB_W - 3 - 16 - fb_text_w(t, 1);
    fb_text(x, 3, t, P_BLACK, 1);
    if (core_playing()) {                          /* play: a triangle */
        int32_t i;
        for (i = 0; i < 4; i++)
            fb_vline(FB_W - 15 + i, 3 + i, 7 - 2 * i, P_BLACK);
    } else {
        fb_rect(FB_W - 15, 3, 6, 6, P_BLACK);      /* stop: a square */
    }
    if (core_rec)
        fb_disc(FB_W - 5, 6, 3, P_RED);
}

static void footer(const char *hint)
{
    fb_rect(0, 228, FB_W, 12, P_BLACK);
    if (ui.msg_until)
        fb_text(3, 230, ui.msg, P_YELLOW, 1);
    else
        fb_text(3, 230, hint, P_GRAY, 1);
}

/* a knob box: name, value, bar (v of lo..hi) */
static void knob_box(int32_t x, int32_t y, int32_t w, const char *name, const char *val, int v, int lo, int hi,
                     int lit)
{
    int32_t f;
    fb_rect(x, y, w, 40, lit ? P_PURPLE : P_BLACK);
    fb_frame(x, y, w, 40, lit ? P_LIME : P_DGRAY);
    fb_text(x + (w - fb_text_w(name, 1)) / 2, y + 4, name, P_LGRAY, 1);
    fb_text(x + (w - fb_text_w(val, 1)) / 2, y + 17, val, P_WHITE, 1);
    fb_rect(x + 4, y + 30, w - 8, 5, P_DGRAY);
    f = hi > lo ? (v - lo) * (w - 8) / (hi - lo) : 0;
    fb_rect(x + 4, y + 30, f, 5, P_ORANGE);
}

/* the track's page of knobs, or the held step's fields */
static void knob_row(int32_t y, const track_t *t)
{
    char val[16];
    int k;
    if (ui.held_step >= 0 && ((ui.prev.buttons >> UB_LFO) & 1u)) {   /* locking: the page's params, locked lit */
        for (k = 0; k < 4; k++) {
            int id = ui_page_param(t, ui.edit_page, k), lo, hi, v = ui_param_get(t, id, &lo, &hi);
            int lv = id < NPARAMS ? lock_get((uint32_t)ui.track, (uint32_t)ui.held_step, (uint32_t)id) : -1;
            track_t tmp;
            if (lv >= 0) {
                tmp = *t;
                tmp.p[id] = (uint8_t)lv;
                v = lv;
                ui_param_text(&tmp, id, val, sizeof val);
            } else {
                ui_param_text(t, id, val, sizeof val);
            }
            knob_box(4 + k * 59, y, 55, ui_param_name(t, id), val, v, lo, hi, lv >= 0);
        }
        return;
    }
    if (ui.held_step >= 0) {
        const step_t *s = &t->step[ui.held_step];
        static const int16_t HI[2][4] = {{127, 127, 64, 100}, {4, 11, NCONDS - 1, 3}};
        static const int16_t LO[2][4] = {{0, 1, 1, 0}, {1, -11, 0, 0}};
        for (k = 0; k < 4; k++) {
            int v;
            ui_step_text(s, ui.step_page, k, val, sizeof val);
            v = ui.step_page == 0 ? (k == 0 ? s->note : k == 1 ? s->vel : k == 2 ? s->gate : s->prob)
                                  : (k == 0 ? s->ratchet : k == 1 ? s->micro : k == 2 ? s->cond
                                                                                      : (s->flags >> 1) & 3);
            knob_box(4 + k * 59, y, 55, UI_STEP_FIELD[ui.step_page][k], val, v, LO[ui.step_page][k],
                     HI[ui.step_page][k], 1);
        }
        return;
    }
    for (k = 0; k < 4; k++) {
        int id = ui_page_param(t, ui.edit_page, k), lo, hi, v = ui_param_get(t, id, &lo, &hi);
        ui_param_text(t, id, val, sizeof val);
        knob_box(4 + k * 59, y, 55, ui_param_name(t, id), val, v, lo, hi, 0);
    }
}

/* one step cell: on / off, velocity, accent, slide, probability, ratchets, condition, nudge */
static void step_cell(int32_t x, int32_t y, int32_t w, int32_t h, const step_t *s, int beat, int play, int held,
                      int muted, int locks)
{
    uint8_t face = muted ? P_GRAY : P_ORANGE;
    if (!(s->flags & STEP_ON)) {
        fb_rect(x, y, w, h, P_BLACK);
        fb_frame(x, y, w, h, beat ? P_GRAY : P_DGRAY);
    } else {
        int32_t vh = (h - 2) * s->vel / 127;
        fb_rect(x, y, w, h, P_DORANGE);
        if (s->prob < 100)
            fb_dither(x + 1, y + h - 1 - vh, w - 2, vh, P_DORANGE, face, (uint32_t)(s->prob * 16 / 100));
        else
            fb_rect(x + 1, y + h - 1 - vh, w - 2, vh, face);
        if (s->flags & STEP_ACCENT)
            fb_rect(x, y, w, 3, P_YELLOW);
        if (s->flags & STEP_SLIDE)
            fb_rect(x + w - 2, y + h / 2 - 1, 3, 3, P_LIME);
        if (s->ratchet > 1) {
            int32_t i;
            for (i = 0; i < s->ratchet; i++)
                fb_rect(x + 2 + i * 3, y + 4, 2, 2, P_WHITE);
        }
        if (s->cond)
            fb_rect(x + w - 4, y + 4, 2, 2, P_MAGENTA);
        if (s->micro)
            fb_vline(x + w / 2 + s->micro * (w / 2 - 1) / 11, y + h - 3, 3, P_CYAN);
    }
    if (locks && (s->flags & STEP_ON))             /* parameter locks: a white corner */
        fb_rect(x, y + h - 4, 4, 4, P_WHITE);
    if (play)
        fb_frame(x - 1, y - 1, w + 2, h + 2, P_WHITE);
    if (held)
        fb_frame(x, y, w, h, P_LIME);
}

/* ------------------------------------------------------------- splash --- */
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
    {
        const char *title = "STACK";
        int32_t s = 7, x = (FB_W - fb_text_w(title, s)) / 2;
        fb_text(x + 5, 31, title, P_BLACK, s);
        fb_text(x + 3, 29, title, P_MAGENTA, s);
        fb_text(x, 26, title, P_YELLOW, s);
    }
    if ((t / 400u) & 1u)                           /* (the gradient above cleared it on the other half-second) */
        fb_text_center(80, "PRESS ANY KEY", P_WHITE, 1);
    for (i = 0; i < 3; i++)
        ui_draw_cabinet(73 + i * 32, 94, 30, 18, 2, ex_off, 0);
    for (i = 0; i < 3; i++)
        ui_draw_cabinet(63 + i * 39, 113, 37, 30, 1, ex_beat > 1 ? 1 : 0, 0);
    for (i = 0; i < 4; i++)
        ui_draw_cabinet(28 + i * 47, 144, 45, 50, 0, ex_beat, 0);
    fb_dither(0, 196, FB_W, 18, P_WOOD, P_DORANGE, 5);
    fb_hline(0, 196, FB_W, P_BLACK);
    fb_rect(0, 214, FB_W, 16, P_BLACK);
    fb_text_center(218, "RAVE - DUB - ACID - V0.2", P_ORANGE, 1);
}

/* -------------------------------------------------------------- SEQ --- */
static void draw_seq(void)
{
    const track_t *t = &g_proj.pat.trk[ui.track];
    int np = (t->len + PAGE - 1) / PAGE, i, r, play = core_play_pos((uint32_t)ui.track);
    int muted = (core_mute >> ui.track) & 1u;
    char s[40];
    fb_clear(P_NIGHT);
    s[0] = 0;
    ui_cat(s, "T", sizeof s);
    ui_cat(s, TRACK_KEY[ui.track], sizeof s);
    ui_cat(s, " ", sizeof s);
    ui_cat(s, PRESETS[t->preset].name, sizeof s);
    ui_header(s);
    /* the track's settings */
    s[0] = 0;
    ui_cat(s, "LEN ", sizeof s);
    ui_int(s, t->len, sizeof s);
    ui_cat(s, "  ", sizeof s);
    {
        char v[8];
        ui_param_text(t, PX_SCALE, v, sizeof v);
        ui_cat(s, v, sizeof s);
        ui_cat(s, "  ", sizeof s);
        ui_param_text(t, PX_DIR, v, sizeof v);
        ui_cat(s, v, sizeof s);
    }
    fb_text(4, 16, s, P_LGRAY, 1);
    if (ui.keys_mode) {
        s[0] = 0;
        ui_cat(s, "KEYS OCT", sizeof s);
        if (ui.octave >= 0)
            ui_cat(s, "+", sizeof s);
        ui_int(s, ui.octave, sizeof s);
        fb_text(FB_W - 4 - fb_text_w(s, 1), 16, s, P_LIME, 1);
    } else if (muted) {
        fb_text(FB_W - 4 - fb_text_w("MUTED", 1), 16, "MUTED", P_RED, 1);
    }
    /* the page's 16 steps, big */
    for (i = 0; i < PAGE; i++) {
        int st = ui.page * PAGE + i;
        int32_t x = 8 + i * 14;
        if (st >= t->len) {
            fb_dither(x, 28, 13, 34, P_NIGHT, P_DGRAY, 4);
            continue;
        }
        step_cell(x, 28, 13, 34, &t->step[st], !(i & 3), st == play, st == ui.held_step, muted,
                  step_has_locks((uint32_t)ui.track, (uint32_t)st));
    }
    /* pages: which one is shown, where the playhead is */
    for (i = 0; i < 4; i++) {
        int32_t x = 8 + i * 56;
        uint8_t c = i >= np ? P_BLACK : i == ui.page ? P_YELLOW : P_DGRAY;
        fb_rect(x, 66, 52, 3, c);
        if (play >= 0 && play / PAGE == i)
            fb_rect(x + (play % PAGE) * 52 / PAGE, 65, 3, 5, P_WHITE);
    }
    /* every track: its name, its 16 steps on this page, its playhead; the selected one framed */
    for (r = 0; r < NTRACKS; r++) {
        const track_t *tr = &g_proj.pat.trk[r];
        int32_t y = 74 + r * 10, pp = core_play_pos((uint32_t)r);
        int m = ((core_mute >> r) & 1u) || (core_solo && !((core_solo >> r) & 1u));
        char nm[8];
        if (r == ui.track)
            fb_rect(0, y - 1, FB_W, 10, P_PURPLE);
        nm[0] = 0;
        ui_cat(nm, TRACK_KEY[r], sizeof nm);
        fb_text(3, y, nm, lamp((uint32_t)r) ? P_YELLOW : P_GRAY, 1);
        {
            char sh[6];
            int j;
            for (j = 0; j < 4 && tr->preset < NPRESETS && PRESETS[tr->preset].name[j]; j++)
                sh[j] = PRESETS[tr->preset].name[j];
            sh[j] = 0;
            fb_text(11, y, sh, m ? P_DGRAY : lamp((uint32_t)r) ? P_YELLOW : P_LGRAY, 1);
        }
        for (i = 0; i < PAGE; i++) {
            int st = ui.page * PAGE + i;
            int32_t x = 40 + i * 12;
            uint8_t c;
            if (st >= tr->len)
                c = P_NIGHT;
            else if (tr->step[st].flags & STEP_ON)
                c = m ? P_GRAY : (tr->step[st].flags & STEP_ACCENT) ? P_YELLOW : P_ORANGE;
            else
                c = !(i & 3) ? P_DGRAY : P_BLACK;
            fb_rect(x, y, 11, 8, c);
            if (st == pp)
                fb_frame(x, y, 11, 8, P_WHITE);
        }
    }
    knob_row(160, t);
    /* the page's name */
    if (ui.held_step >= 0) {
        s[0] = 0;
        ui_cat(s, "STEP ", sizeof s);
        ui_int(s, ui.held_step + 1, sizeof s);
        ui_cat(s, (ui.prev.buttons >> UB_LFO) & 1u ? "  P-LOCKS" : ui.step_page ? "  ALGO: NOTE..." : "  ALGO: RATCHET...",
               sizeof s);
        fb_text(4, 206, s, P_LIME, 1);
    } else {
        s[0] = 0;
        ui_cat(s, "PAGE ", sizeof s);
        ui_int(s, ui.page + 1, sizeof s);
        ui_cat(s, "/", sizeof s);
        ui_int(s, np, sizeof s);
        ui_cat(s, "   KNOBS ", sizeof s);
        ui_int(s, ui.edit_page + 1, sizeof s);
        ui_cat(s, "/", sizeof s);
        ui_int(s, ui_npages(t), sizeof s);
        fb_text(4, 206, s, P_GRAY, 1);
    }
    footer(ui.keys_mode ? "KEYS PLAY THE TRACK" : "HOLD A STEP + KNOBS: EDIT IT");
}

/* ------------------------------------------------------------- EDIT --- */
static void draw_edit(void)
{
    const track_t *t = &g_proj.pat.trk[ui.track];
    int np = ui_npages(t), i, play = core_play_pos((uint32_t)ui.track);
    char s[32];
    fb_clear(P_NIGHT);
    s[0] = 0;
    ui_cat(s, "T", sizeof s);
    ui_cat(s, TRACK_KEY[ui.track], sizeof s);
    ui_cat(s, " EDIT", sizeof s);
    ui_header(s);
    /* the preset, big, with its engine; PRESET turns it */
    fb_text(4, 18, "PRESET", P_GRAY, 1);
    fb_text_shadow(4, 30, PRESETS[t->preset].name, P_YELLOW, P_MAGENTA, 2, 1);
    {
        const char *g = PRESET_GROUP[PRESETS[t->preset].group];
        fb_text(FB_W - 4 - fb_text_w(g, 1), 18, g, P_LIME, 1);
    }
    /* page tabs */
    for (i = 0; i < np; i++) {
        int32_t x = 4 + i * 30;
        s[0] = 0;
        ui_int(s, i + 1, sizeof s);
        fb_rect(x, 56, 26, 11, i == ui.edit_page ? P_ORANGE : P_DGRAY);
        fb_text(x + 10, 58, s, i == ui.edit_page ? P_BLACK : P_LGRAY, 1);
    }
    knob_row(74, t);
    /* the track's steps, small, for context */
    for (i = 0; i < PAGE; i++) {
        int st = ui.page * PAGE + i;
        if (st < t->len)
            step_cell(8 + i * 14, 126, 13, 22, &t->step[st], !(i & 3), st == play, 0, 0,
                      step_has_locks((uint32_t)ui.track, (uint32_t)st));
    }
    lamp((uint32_t)ui.track) ? fb_disc(120, 176, 14, P_YELLOW) : fb_ring(120, 176, 14, P_DGRAY);
    footer("ALGO/EDIT: PAGE  PRESET: SOUND");
}

/* ---------------------------------------------------------- GLOBAL --- */
static void draw_global(void)
{
    char a[16], b[16], c[16], d[16];
    fb_clear(P_NIGHT);
    ui_header(ui.glo_page ? "GLOBAL 2/2" : "GLOBAL 1/2");
    if (ui.glo_page) {                             /* the look and MIDI */
        static const char *const TR[4] = {"OFF", "SEND", "OBEY", "BOTH"};
        knob_box(4, 30, 55, "SCANLINES", ui.scanlines ? "ON" : "OFF", ui.scanlines, 0, 1, 0);
        knob_box(63, 30, 55, "CLOCK OUT", g_proj.clk_out ? "ON" : "OFF", g_proj.clk_out, 0, 1, 0);
        knob_box(122, 30, 55, "CLOCK IN", g_proj.clk_in ? "EXT" : "INT", g_proj.clk_in, 0, 1, 0);
        knob_box(181, 30, 55, "START/STOP", TR[g_proj.transport & 3u], g_proj.transport, 0, 3, 0);
        fb_text(4, 84, "MIDI OVER USB. EACH TRACK SENDS", P_GRAY, 1);
        fb_text(4, 94, "AND HEARS ON ITS MIDI CH (EDIT,", P_GRAY, 1);
        fb_text(4, 104, "LAST PAGE).", P_GRAY, 1);
        footer("ALGO: PAGE 1");
        return;
    }
    a[0] = b[0] = c[0] = d[0] = 0;
    ui_int(a, g_proj.bpm10 / 10, sizeof a);
    ui_int(b, g_proj.swing, sizeof b);
    ui_cat(b, "%", sizeof b);
    ui_int(c, g_proj.bpm10 / 10, sizeof c);
    ui_cat(c, ".", sizeof c);
    ui_int(c, g_proj.bpm10 % 10, sizeof c);
    ui_int(d, g_proj.seed, sizeof d);
    knob_box(4, 30, 55, "BPM", a, g_proj.bpm10, BPM_MIN, BPM_MAX, 0);
    knob_box(63, 30, 55, "SWING", b, g_proj.swing, 50, 75, 0);
    knob_box(122, 30, 55, "FINE", c, g_proj.bpm10 % 10, 0, 9, 0);
    knob_box(181, 30, 55, "SEED", d, g_proj.seed, 0, 255, 0);
    fb_text(4, 84, "SEED: THE DICE OF PROBABILITY", P_GRAY, 1);
    fb_text(4, 94, "STEPS, THE SAME AT EVERY PLAY.", P_GRAY, 1);
    a[0] = 0;
    ui_cat(a, "CPU ", sizeof a);
    ui_int(a, (int32_t)ui.cpu_pct, sizeof a);
    ui_cat(a, "%  XRUNS ", sizeof a);
    ui_int(a, (int32_t)ui.xruns, sizeof a);
    fb_text(4, 120, a, P_LGRAY, 1);
    footer("ALGO: PAGE 2   SHIFT+GLO: HELP");
}

/* ----------------------------------------------------- the sound system --- */
void ui_steps_bar(int32_t y)
{
    const track_t *t = &g_proj.pat.trk[ui.track];
    int i, play = core_play_pos((uint32_t)ui.track);
    char s[24];
    fb_rect(0, y, FB_W, FB_H - y, P_BLACK);
    if (ui.msg_until) {
        fb_text(3, y + 3, ui.msg, P_YELLOW, 1);
    } else {
        s[0] = 0;
        ui_cat(s, TRACK_KEY[ui.track], sizeof s);
        ui_cat(s, " ", sizeof s);
        ui_cat(s, PRESETS[t->preset].name, sizeof s);
        fb_text(3, y + 3, s, lamp((uint32_t)ui.track) ? P_YELLOW : P_LGRAY, 1);
        if ((core_mute >> ui.track) & 1u)
            fb_text(FB_W - 3 - fb_text_w("MUTED", 1), y + 3, "MUTED", P_RED, 1);
        else if ((core_solo >> ui.track) & 1u)
            fb_text(FB_W - 3 - fb_text_w("SOLO", 1), y + 3, "SOLO", P_LIME, 1);
    }
    for (i = 0; i < PAGE; i++) {
        int st = ui.page * PAGE + i;
        int32_t x = 4 + i * 15;
        uint8_t c;
        if (st >= t->len)
            c = P_NIGHT;
        else if (t->step[st].flags & STEP_ON)
            c = (t->step[st].flags & STEP_ACCENT) ? P_YELLOW : P_ORANGE;
        else
            c = !(i & 3) ? P_DGRAY : P_NIGHT;
        fb_rect(x, y + 13, 13, 9, c);
        if (st == play)
            fb_frame(x - 1, y + 12, 15, 11, P_WHITE);
        if (st == ui.held_step)
            fb_frame(x, y + 13, 13, 9, P_LIME);
    }
}

static void draw_stack(uint32_t now)
{
    stack_draw(now);
    if (ui.scanlines)
        fb_scanlines(12, 214);
    {
        char pt[20];
        ui_pattern_text(pt, sizeof pt);
        ui_header(pt);
    }
    ui_steps_bar(214);
}

/* --------------------------------------------------------------- FX --- */
static void draw_fx(void)
{
    static const char *const FX_TITLE[3] = {"FX 1/3 DUB DELAY", "FX 2/3 REVERB", "FX 3/3 PUMP COMP DJ"};
    char v[16];
    int k, i;
    fb_clear(P_NIGHT);
    ui_header(FX_TITLE[ui.fx_page]);
    for (k = 0; k < 4; k++) {
        int id = UI_FX_PAGE[ui.fx_page][k];
        if (id < 0)
            continue;
        ui_fx_text(id, v, sizeof v);
        knob_box(4 + k * 59, 30, 55, UI_FX_NAME[id], v, g_proj.fx[id], 0, id == FX_DLY_TIME ? 5 : 127, 0);
    }
    /* what each track sends: two bars a track */
    fb_text(4, 82, "SENDS   DELAY / REVERB", P_GRAY, 1);
    for (i = 0; i < NTRACKS; i++) {
        const track_t *t = &g_proj.pat.trk[i];
        int32_t x = 4 + i * 29;
        char n[2];
        n[0] = (char)('1' + i);
        n[1] = 0;
        fb_text(x + 10, 96, n, lamp((uint32_t)i) ? P_YELLOW : P_LGRAY, 1);
        fb_rect(x + 6, 108, 7, 64, P_BLACK);
        fb_rect(x + 15, 108, 7, 64, P_BLACK);
        fb_rect(x + 6, 108 + 64 - t->p[PR_SEND_D] / 2, 7, t->p[PR_SEND_D] / 2, P_ORANGE);
        fb_rect(x + 15, 108 + 64 - t->p[PR_SEND_R] / 2, 7, t->p[PR_SEND_R] / 2, P_CYAN);
    }
    fb_text(4, 182, "THE KICKS DRIVE THE PUMP;", P_GRAY, 1);
    fb_text(4, 192, "SENDS ARE ON EACH TRACK'S EDIT.", P_GRAY, 1);
    footer("FX/ALGO: PAGE   STACK K3 DJ, K4 PUMP");
}

/* ---------------------------------------------------------- PATTERNS --- */
static void draw_pattern(void)
{
    static const char *const MODE[3] = {"PATTERN", "CHAIN", "SONG"};
    char t[24], h[24];
    int i, c = core_cue();
    fb_clear(P_NIGHT);
    ui_pattern_text(h, sizeof h);
    if (ui.pat_page == 0) {
        ui_header(h);
        for (i = 0; i < NPATTERNS; i++) {                 /* 4 x 4: used, current, cued, in the chain */
            int32_t x = 6 + (i % 4) * 58, y = 18 + (i / 4) * 34;
            int used = bank_used((uint32_t)i), k;
            uint8_t bg = i == g_proj.cur_pat ? P_ORANGE : used ? P_DORANGE : P_BLACK;
            fb_rect(x, y, 54, 30, bg);
            fb_frame(x, y, 54, 30, i == c && ((ui.now / 150u) & 1u) ? P_YELLOW : P_DGRAY);
            t[0] = 0;
            ui_cat(t, "P", sizeof t);
            if (i < 9)
                ui_cat(t, "0", sizeof t);
            ui_int(t, i + 1, sizeof t);
            fb_text(x + 4, y + 4, t, i == g_proj.cur_pat ? P_BLACK : used ? P_WHITE : P_GRAY, 1);
            for (k = 0; k < g_proj.chain_len; k++)        /* its places in the chain */
                if (g_proj.chain[k] == i)
                    fb_rect(x + 4 + k * 3, y + 22, 2, 4, P_LIME);
        }
        t[0] = 0;
        ui_cat(t, "MODE ", sizeof t);
        ui_cat(t, MODE[g_proj.play_mode % 3], sizeof t);
        fb_text(6, 158, t, P_YELLOW, 1);
        fb_text(6, 172, "KEYS 1-16: PATTERN (NEXT BAR)", P_GRAY, 1);
        fb_text(6, 182, "HOLD ARP + KEYS: A CHAIN", P_GRAY, 1);
        fb_text(6, 192, "K1 MODE  ALGO: THE SONG", P_GRAY, 1);
        fb_text(6, 202, "HOME+SEQ/EDIT/REC: COPY PASTE CLR", P_GRAY, 1);
        footer("ALGO: THE SONG PAGE");
        return;
    }
    ui_header("SONG");
    for (i = 0; i < 12; i++) {                            /* the rows around the cursor */
        int r = ui.song_cursor - 5 + i;
        int32_t y = 16 + i * 13;
        if (r < 0 || r >= NSONG)
            continue;
        if (r == ui.song_cursor)
            fb_rect(0, y - 2, FB_W, 12, P_PURPLE);
        t[0] = 0;
        if (r < 9)
            ui_cat(t, "0", sizeof t);
        ui_int(t, r + 1, sizeof t);
        if (r < g_proj.song_len) {
            ui_cat(t, "  P", sizeof t);
            if (g_proj.song[r].pat < 9)
                ui_cat(t, "0", sizeof t);
            ui_int(t, g_proj.song[r].pat + 1, sizeof t);
            ui_cat(t, "  X", sizeof t);
            ui_int(t, g_proj.song[r].reps, sizeof t);
            if (core_playing() && g_proj.play_mode == PLAY_SONG && r == core_song_row())
                ui_cat(t, "  <", sizeof t);
        } else {
            ui_cat(t, "  --", sizeof t);
        }
        fb_text(8, y, t, r < g_proj.song_len ? P_WHITE : P_DGRAY, 1);
    }
    t[0] = 0;
    ui_cat(t, "LENGTH ", sizeof t);
    ui_int(t, g_proj.song_len, sizeof t);
    fb_text(150, 18, t, P_LGRAY, 1);
    fb_text(150, 32, "K1 ROW", P_GRAY, 1);
    fb_text(150, 42, "K2 PATTERN", P_GRAY, 1);
    fb_text(150, 52, "K3 REPEATS", P_GRAY, 1);
    fb_text(150, 62, "K4 LENGTH", P_GRAY, 1);
    footer(g_proj.play_mode == PLAY_SONG ? "SONG MODE ON" : "PATTERNS: K1 MODE = SONG");
}

static void draw_scale(void)
{
    static const char *const N[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    static const uint8_t BK_AFTER[11] = {0, 1, 2, 4, 5, 7, 8, 9, 11, 12, 14};
    int i;
    fb_clear(P_NIGHT);
    ui_header("SCALE");
    knob_box(4, 30, 55, "TONIC", N[g_proj.tonic % 12], g_proj.tonic, 0, 11, 0);
    knob_box(63, 30, 114, "SCALE", SCALE_NAME[g_proj.scale % NSCALE_TYPES], g_proj.scale, 0, NSCALE_TYPES - 1, 0);
    for (i = 0; i < 16; i++) {                            /* the keyboard (F3 up): the scale's notes lit */
        int note = 53 + UI_WHITE[i];
        fb_rect(8 + i * 14, 100, 13, 50, scale_has(note) ? (((note - g_proj.tonic) % 12 + 12) % 12 == 0 ? P_YELLOW
                                                                                                   : P_ORANGE)
                                                         : P_DGRAY);
    }
    for (i = 0; i < 11; i++) {
        int note = 53 + UI_BLACK[i];
        fb_rect(8 + BK_AFTER[i] * 14 + 9, 100, 9, 30, scale_has(note) ? P_LORANGE : P_BLACK);
    }
    fb_text(4, 160, "KEYS MODE AND THE STEP NOTE KNOB", P_GRAY, 1);
    fb_text(4, 170, "KEEP TO THIS SCALE.", P_GRAY, 1);
    footer("K1 TONIC  K2 SCALE");
}

/* ------------------------------------------------------------- HELP --- */
static void draw_help(void)
{
    static const char *const L[] = {
        "WHITE KEYS   STEPS (HOLD+KNOB=EDIT)",
        "BLACK 1-8    TRACK (STACK: MUTE)",
        "BLACK 9/10   PAGE / FILL (HOLD)",
        "BLACK 11     KEYS MODE (SEQ: BACK)",
        "PLAY  REC    PLAY-STOP / LIVE REC",
        "SEQ EDIT ENV GLO FX ARP SEL  VIEWS",
        "HOME (TAP)   THE SOUND SYSTEM",
        "OCT- OCT+    PAGE / OCTAVE   SAVE",
        "SELECT       TRACK",
        "ALGO         KNOB PAGE",
        "PRESET SOUND  LFO+STEP+KNOB: LOCK",
        "HOME HELD = SHIFT, THEN:",
        "SEQ COPY  EDIT PASTE  REC CLEAR",
        "SAVE UNDO  PLAY DEMO  FX I/O TEST",
        "KEY 1-8 MUTE (STACK: SOLO)",
    };
    uint32_t i;
    fb_clear(P_BLACK);
    ui_header("HELP");
    for (i = 0; i < sizeof L / sizeof L[0]; i++)
        fb_text(4, 18 + (int32_t)i * 12, L[i], i >= 11 ? P_LIME : P_LGRAY, 1);
    footer("SHIFT+GLO: CLOSE");
}

/* -------------------------------------------------------------- I/O --- */
static void draw_io(void)
{
    static const char *const BTN[UI_NBUTTONS] = {"FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME",
                                                 "SAVE", "ARP", "SEQ", "PLAY", "REC", "OCT-", "OCT+"};
    static const char *const ENC[UI_NENC] = {"SEL", "ALG", "PRE", "K1", "K2", "K3", "K4"};
    static const int8_t DX[16] = {0, 3, 5, 6, 7, 6, 5, 3, 0, -3, -5, -6, -7, -6, -5, -3};
    static const int8_t DY[16] = {-7, -6, -5, -3, 0, 3, 5, 6, 7, 6, 5, 3, 0, -3, -5, -6};
    static const uint8_t BK_AFTER[11] = {0, 1, 2, 4, 5, 7, 8, 9, 11, 12, 14};
    const ui_input_t *in = &ui.prev;
    char t[32];
    int32_t i;
    fb_clear(P_NIGHT);
    fb_rect(0, 0, FB_W, 13, P_ORANGE);
    fb_text(4, 3, "STACK  I/O TEST", P_BLACK, 1);
    t[0] = 0;
    ui_cat(t, "CPU ", sizeof t);
    ui_int(t, (int32_t)ui.cpu_pct, sizeof t);
    ui_cat(t, "% XR ", sizeof t);
    ui_int(t, (int32_t)ui.xruns, sizeof t);
    fb_text(FB_W - 4 - fb_text_w(t, 1), 3, t, P_BLACK, 1);
    for (i = 0; i < UI_NBUTTONS; i++) {
        int32_t x = 5 + (i % 7) * 33, y = 17 + (i / 7) * 16;
        int on = (int)(in->buttons >> i & 1u);
        fb_rect(x, y, 31, 14, on ? P_YELLOW : P_BLACK);
        fb_frame(x, y, 31, 14, on ? P_YELLOW : P_DGRAY);
        fb_text(x + (31 - fb_text_w(BTN[i], 1)) / 2, y + 4, BTN[i], on ? P_BLACK : P_LGRAY, 1);
    }
    for (i = 0; i < UI_NENC; i++) {
        int32_t x = 5 + i * 33, cx = x + 15, cy = 70, a = ((ui.enc_total[i] % 16) + 16) % 16;
        fb_text(x + (31 - fb_text_w(ENC[i], 1)) / 2, 51, ENC[i], P_LGRAY, 1);
        fb_disc(cx, cy, 9, P_DGRAY);
        fb_ring(cx, cy, 9, P_GRAY);
        fb_disc(cx + DX[a] * 6 / 7, cy + DY[a] * 6 / 7, 1, P_LIME);
        t[0] = 0;
        ui_int(t, ui.enc_total[i], sizeof t);
        fb_text(x + (31 - fb_text_w(t, 1)) / 2, 83, t, P_WHITE, 1);
    }
    fb_text(5, 97, "MASTER", P_LGRAY, 1);
    fb_frame(48, 96, 150, 9, P_GRAY);
    fb_rect(49, 97, (int32_t)in->master * 148 / 4096, 7, P_ORANGE);
    t[0] = 0;
    ui_int(t, (int32_t)(in->master * 100u / 4096u), sizeof t);
    ui_cat(t, "%", sizeof t);
    fb_text(204, 97, t, P_WHITE, 1);
    for (i = 0; i < 16; i++)
        fb_rect(8 + i * 14, 112, 13, 44, (in->keys >> UI_WHITE[i] & 1u) ? P_LIME : P_WHITE);
    for (i = 0; i < 11; i++)
        fb_rect(8 + BK_AFTER[i] * 14 + 9, 112, 9, 27, (in->keys >> UI_BLACK[i] & 1u) ? P_MAGENTA : P_BLACK);
    t[0] = 0;
    ui_cat(t, "TRACK ", sizeof t);
    ui_int(t, ui.track + 1, sizeof t);
    ui_cat(t, "  VOICES ", sizeof t);
    ui_int(t, (int32_t)core_voices_active(), sizeof t);
    fb_text(8, 160, t, P_LGRAY, 1);
    fb_rect(4, 172, FB_W - 8, 56, P_BLACK);
    for (i = 0; i < UI_LOG_LINES && (uint32_t)i < ui.log_n; i++) {
        uint32_t k = (ui.log_n - 1u - (uint32_t)i) % UI_LOG_LINES;
        fb_text(8, 175 + i * 9, ui.log[k], i == 0 ? P_YELLOW : P_GRAY, 1);
    }
    fb_text(4, 231, "HOME: BACK   KEYS: PLAY TRACK", P_DGRAY, 1);
}

void ui_draw(uint32_t now)
{
    switch (ui.view) {
    case VIEW_SPLASH: ui_draw_splash(now - ui.t0); break;
    case VIEW_EDIT: draw_edit(); break;
    case VIEW_GLOBAL: draw_global(); break;
    case VIEW_STACK: draw_stack(now); break;
    case VIEW_HELP: draw_help(); break;
    case VIEW_IO: draw_io(); break;
    case VIEW_FX: draw_fx(); break;
    case VIEW_PATTERN: draw_pattern(); break;
    case VIEW_SCALE: draw_scale(); break;
    default: draw_seq(); break;
    }
}
