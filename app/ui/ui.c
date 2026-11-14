/* SPDX-License-Identifier: GPL-3.0-or-later
 * STACK — the UI's state and input: what each control does in each view (docs/PLAN.md, "Mapa de controles").
 * Drawing is in ui_draw.c. Hardware-free: see ui.h.
 *
 * SHIFT is HOME held. A HOME tap (released with nothing else pressed) opens the mixer (the sound system from Phase 3).
 * Step mode: the 16 white keys are the page's steps (a tap toggles; held, the knobs edit that step), the black keys
 * 1-8 pick the track (mute in the mixer, solo with SHIFT), then PAGE, FILL (held) and KEYS. KEYS mode: every key plays
 * the track chromatically (and records it while REC is armed and playing); SEQ goes back to step mode. */
#include "ui_int.h"

const uint8_t UI_WHITE[16] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};
const uint8_t UI_BLACK[11] = {1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25};

ui_t ui;

/* ------------------------------------------------------------ small text --- */
void ui_cat(char *d, const char *s, uint32_t cap)
{
    uint32_t n = 0;
    while (d[n] && n < cap - 1u)
        n++;
    while (*s && n < cap - 1u)
        d[n++] = *s++;
    d[n] = 0;
}

void ui_int(char *d, int32_t v, uint32_t cap)
{
    char t[12], r[13];
    int n = 0, i;
    uint32_t u = (uint32_t)(v < 0 ? -v : v);
    do
        t[n++] = (char)('0' + u % 10u);
    while ((u /= 10u) && n < 11);
    if (v < 0)
        t[n++] = '-';
    for (i = 0; i < n; i++)
        r[i] = t[n - 1 - i];
    r[n] = 0;
    ui_cat(d, r, cap);
}

void ui_note_text(char *d, int32_t note, uint32_t cap)
{
    static const char *const N[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    d[0] = 0;
    ui_cat(d, N[note % 12], cap);
    ui_int(d, note / 12 - 1, cap);
}

void ui_log(const char *a, const char *b, const char *c)
{
    char *l = ui.log[ui.log_n % UI_LOG_LINES];
    l[0] = 0;
    ui_cat(l, a, UI_LOG_LEN);
    ui_cat(l, " ", UI_LOG_LEN);
    ui_cat(l, b, UI_LOG_LEN);
    if (c && *c) {
        ui_cat(l, " ", UI_LOG_LEN);
        ui_cat(l, c, UI_LOG_LEN);
    }
    ui.log_n++;
}

static void say(const char *a, const char *b)
{
    ui.msg[0] = 0;
    ui_cat(ui.msg, a, sizeof ui.msg);
    if (b && *b) {
        ui_cat(ui.msg, " ", sizeof ui.msg);
        ui_cat(ui.msg, b, sizeof ui.msg);
    }
    ui.msg_until = ui.now + 1200u;
    ui.dirty = 1;
}

/* --------------------------------------------------------------- params --- */
/* the knob pages per engine: param ids, PX_* for the track's sequencer settings */
static const int8_t PAGES_DRUM[][4] = {{PR_TUNE, PR_DECAY, PR_TONE, PR_SNAP},
                                       {PR_DRIVE, PR_LEVEL, PR_PAN, PX_CAT},
                                       {PX_LEN, PX_SCALE, PX_DIR, PX_CH}};
static const int8_t PAGES_ACID[][4] = {{PR_CUTOFF, PR_RESO, PR_ENVMOD, PR_DECAY},
                                       {PR_ACCENT, PR_DRIVE, PR_WAVE, PR_SNAP},
                                       {PR_TUNE, PR_LEVEL, PR_PAN, PX_CAT},
                                       {PX_LEN, PX_SCALE, PX_DIR, PX_CH}};

int ui_npages(const track_t *t)
{
    return t->engine == ENGINE_ACID ? 4 : 3;
}

int ui_page_param(const track_t *t, int page, int k)
{
    if (t->engine == ENGINE_ACID)
        return PAGES_ACID[page % 4][k & 3];
    return PAGES_DRUM[page % 3][k & 3];
}

const char *ui_param_name(const track_t *t, int id)
{
    static const char *const PX[] = {"LEN", "SCALE", "DIR", "CAT", "MIDI CH"};
    if (id >= PX_LEN)
        return PX[id - PX_LEN];
    return PARAM_NAME[t->engine < NENGINES ? t->engine : 0][id] ? PARAM_NAME[t->engine][id] : "--";
}

/* value 0..max and its range */
int ui_param_get(const track_t *t, int id, int *lo, int *hi)
{
    *lo = 0;
    *hi = 127;
    switch (id) {
    case PX_LEN: *lo = 1; *hi = NSTEPS; return t->len;
    case PX_SCALE: *hi = NSCALES - 1; return t->scale;
    case PX_DIR: *hi = NDIRS - 1; return t->dir;
    case PX_CAT: *hi = NCATS - 1; return t->category;
    case PX_CH: *hi = 16; return t->midi_ch;
    default: return t->p[id];
    }
}

static void param_set(track_t *t, int id, int v)
{
    int lo, hi;
    ui_param_get(t, id, &lo, &hi);
    v = v < lo ? lo : v > hi ? hi : v;
    switch (id) {
    case PX_LEN: t->len = (uint8_t)v; break;
    case PX_SCALE: t->scale = (uint8_t)v; break;
    case PX_DIR: t->dir = (uint8_t)v; break;
    case PX_CAT: t->category = (uint8_t)v; break;
    case PX_CH: t->midi_ch = (uint8_t)v; break;
    default: t->p[id] = (uint8_t)v; break;
    }
}

void ui_param_text(const track_t *t, int id, char *d, uint32_t cap)
{
    static const char *const SCALE[NSCALES] = {"1/32", "1/16", "1/8", "1/4", "1/16T", "1/8T"};
    static const char *const DIR[NDIRS] = {"FWD", "REV", "PING", "RAND"};
    static const char *const CAT[NCATS] = {"SUB", "MID", "TOP"};
    static const char *const WAVE[3] = {"SAW", "SQUARE", "SINE"};
    int lo, hi, v = ui_param_get(t, id, &lo, &hi);
    d[0] = 0;
    if (id < PX_LEN && !PARAM_NAME[t->engine][id]) {
        ui_cat(d, "--", cap);
        return;
    }
    switch (id) {
    case PX_SCALE: ui_cat(d, SCALE[v], cap); return;
    case PX_DIR: ui_cat(d, DIR[v], cap); return;
    case PX_CAT: ui_cat(d, CAT[v], cap); return;
    case PX_CH: if (!v) { ui_cat(d, "OFF", cap); return; } break;
    case PR_PAN:
        if (v == 64) { ui_cat(d, "C", cap); return; }
        ui_cat(d, v < 64 ? "L" : "R", cap);
        ui_int(d, v < 64 ? 64 - v : v - 64, cap);
        return;
    case PR_TUNE:
        if (v >= 64) ui_cat(d, "+", cap);
        ui_int(d, (v - 64) * 3 / 8, cap);   /* semitones: 64 steps = 24 */
        ui_cat(d, "ST", cap);
        return;
    case PR_DECAY:
        ui_int(d, TAB_DECAY_MS[v & 127], cap);
        ui_cat(d, "MS", cap);
        return;
    case PR_WAVE:
        if (t->engine == ENGINE_ACID) { ui_cat(d, WAVE[v / 43], cap); return; }
        break;
    case PR_SNAP:
        if (t->engine == ENGINE_ACID) { ui_cat(d, v >= 64 ? "18DB" : "24DB", cap); return; }
        break;
    }
    ui_int(d, v, cap);
}

/* step fields edited with a step held: page 0 NOTE VEL GATE PROB, page 1 RATCHET NUDGE COND ACC/SLD */
const char *const UI_STEP_FIELD[2][4] = {{"NOTE", "VEL", "GATE", "PROB"}, {"RATCHET", "NUDGE", "COND", "ACC/SLD"}};
const char *const UI_COND_NAME[NCONDS] = {"ALWAYS", "FILL", "!FILL", "1ST", "!1ST", "1:2", "2:2",
                                         "1:3", "2:3", "3:3", "1:4", "2:4", "3:4", "4:4"};

void ui_step_text(const step_t *s, int page, int k, char *d, uint32_t cap)
{
    static const char *const FL[4] = {"-", "ACC", "SLD", "A+S"};
    d[0] = 0;
    if (page == 0) {
        if (k == 0) ui_note_text(d, s->note, cap);
        else if (k == 1) ui_int(d, s->vel, cap);
        else if (k == 2) { ui_int(d, s->gate, cap); ui_cat(d, "/16", cap); }
        else { ui_int(d, s->prob, cap); ui_cat(d, "%", cap); }
    } else {
        if (k == 0) { ui_int(d, s->ratchet, cap); ui_cat(d, "X", cap); }
        else if (k == 1) { if (s->micro > 0) ui_cat(d, "+", cap); ui_int(d, s->micro, cap); }
        else if (k == 2) ui_cat(d, UI_COND_NAME[s->cond < NCONDS ? s->cond : 0], cap);
        else ui_cat(d, FL[(s->flags >> 1) & 3u], cap);
    }
}

static void step_edit(step_t *s, int page, int k, int d)
{
    if (!(s->flags & STEP_ON))                     /* editing a step turns it on */
        s->flags |= STEP_ON;
    if (page == 0) {
        if (k == 0) s->note = (uint8_t)clampi(s->note + d, 0, 127);
        else if (k == 1) s->vel = (uint8_t)clampi(s->vel + d, 1, 127);
        else if (k == 2) s->gate = (uint8_t)clampi(s->gate + d, 1, 64);
        else s->prob = (uint8_t)clampi(s->prob + d, 0, 100);
    } else {
        if (k == 0) s->ratchet = (uint8_t)clampi(s->ratchet + (d > 0 ? 1 : -1), 1, 4);
        else if (k == 1) s->micro = (int8_t)clampi(s->micro + d, -11, 11);
        else if (k == 2) s->cond = (uint8_t)clampi(s->cond + (d > 0 ? 1 : -1), 0, NCONDS - 1);
        else {
            uint32_t f = ((s->flags >> 1) & 3u) + (d > 0 ? 1u : 3u);
            s->flags = (uint8_t)((s->flags & ~(STEP_ACCENT | STEP_SLIDE)) | ((f & 3u) << 1));
        }
    }
}

/* knob acceleration: a fast spin moves further */
static int accel(int32_t d)
{
    int32_t a = d < 0 ? -d : d;
    return (int)(a >= 3 ? d * 4 : d);
}

/* ------------------------------------------------------- track actions --- */
static track_t *cur(void) { return &g_proj.pat.trk[ui.track]; }

static void undo_save(void)
{
    ui.undo = *cur();
    ui.undo_track = ui.track;
    ui.has_undo = 1;
}

static void select_track(int t)
{
    ui.track = clampi(t, 0, NTRACKS - 1);
    ui.edit_page = 0;
    if (ui.page * PAGE >= cur()->len)
        ui.page = 0;
    ui.dirty = 1;
}

static void shift_action(uint32_t b)             /* SHIFT (HOME held) + a button */
{
    char t[4] = "T";
    t[1] = (char)('1' + ui.track);
    t[2] = 0;
    switch (b) {
    case UB_SEQ:
        ui.clip = *cur();
        ui.has_clip = 1;
        say("COPIED", t);
        break;
    case UB_EDIT:
        if (ui.has_clip) {
            undo_save();
            *cur() = ui.clip;
            say("PASTED", t);
        }
        break;
    case UB_REC: {
        uint32_t i;
        undo_save();
        for (i = 0; i < NSTEPS; i++)
            cur()->step[i].flags = 0;
        say("CLEARED", t);
        break;
    }
    case UB_SAVE:
        if (ui.has_undo) {
            track_t keep = g_proj.pat.trk[ui.undo_track];
            g_proj.pat.trk[ui.undo_track] = ui.undo;
            ui.undo = keep;                        /* SHIFT+SAVE again: redo */
            say("UNDO", "");
        }
        break;
    case UB_GLO:
        ui.view = ui.view == VIEW_HELP ? VIEW_SEQ : VIEW_HELP;
        break;
    case UB_FX:
        ui.view = VIEW_IO;
        break;
    case UB_PLAY:
        core_post(EV_STOP, 0, 0, 0);
        core_load_demo();
        say("DEMO", "LOADED");
        break;
    }
    ui.shift_used = 1;
    ui.dirty = 1;
}

static void button(uint32_t b)                   /* a press, SHIFT not held */
{
    switch (b) {
    case UB_PLAY:
        core_post(core_playing() ? EV_STOP : EV_PLAY, 0, 0, 0);
        break;
    case UB_REC:
        core_rec = !core_rec;
        say("REC", core_rec ? "ARMED" : "OFF");
        break;
    case UB_SEQ:
        if (ui.keys_mode) {
            ui.keys_mode = 0;
            core_post(EV_ALL_OFF, 0, 0, 0);
        }
        ui.view = VIEW_SEQ;
        break;
    case UB_EDIT:
        if (ui.view == VIEW_EDIT)
            ui.edit_page = (ui.edit_page + 1) % ui_npages(cur());
        ui.view = VIEW_EDIT;
        break;
    case UB_ENV:                                  /* straight to the page with DECAY */
        ui.view = VIEW_EDIT;
        ui.edit_page = 0;
        break;
    case UB_GLO:
        ui.view = VIEW_GLOBAL;
        break;
    case UB_OCTDN:
    case UB_OCTUP: {
        int d = b == UB_OCTUP ? 1 : -1;
        if (ui.keys_mode) {
            ui.octave = clampi(ui.octave + d, -3, 3);
            core_post(EV_ALL_OFF, 0, 0, 0);
        } else {
            int np = (cur()->len + PAGE - 1) / PAGE;
            ui.page = (ui.page + d + np) % np;
        }
        break;
    }
    case UB_FX: case UB_SEL: case UB_LFO: case UB_ARP: case UB_SAVE:
        say("PHASE 4-5", "");                      /* FX, scales, p-locks, patterns, saving: coming */
        break;
    }
    ui.dirty = 1;
}

static void black_key(int k, int shift)
{
    if (k < NTRACKS) {
        uint32_t bit = 1u << k;
        if (ui.view == VIEW_STACK || shift) {
            if (shift && ui.view == VIEW_STACK)
                core_solo ^= bit;
            else
                core_mute ^= bit;
            if (shift)
                ui.shift_used = 1;
        } else {
            select_track(k);
        }
    } else if (k == BK_PAGE) {
        int np = (cur()->len + PAGE - 1) / PAGE;
        ui.page = (ui.page + 1) % (np ? np : 1);
    } else if (k == BK_KEYS) {
        ui.keys_mode = 1;
        core_post(EV_ALL_OFF, 0, 0, 0);
        say("KEYS", "SEQ: BACK TO STEPS");
    }
    ui.dirty = 1;
}

static int key_note(uint32_t k) { return clampi(53 + (int)k + 12 * ui.octave, 0, 127); }   /* key 0 = F3 */

static void keys_input(const ui_input_t *in)
{
    uint32_t down = in->keys & ~ui.prev.keys & ~ui.ignore_keys;
    uint32_t up = ~in->keys & ui.prev.keys & ~ui.ignore_keys;
    uint32_t k;
    int shift = (in->buttons >> UB_HOME) & 1u;
    for (k = 0; k < UI_NKEYS; k++) {
        int w = -1, b = -1, i;
        for (i = 0; i < 16; i++)
            if (UI_WHITE[i] == k)
                w = i;
        for (i = 0; i < 11; i++)
            if (UI_BLACK[i] == k)
                b = i;
        if (ui.keys_mode) {                        /* chromatic */
            if (down >> k & 1u)
                core_post(EV_NOTE_ON, (uint32_t)ui.track, (uint32_t)key_note(k), 100);
            else if (up >> k & 1u)
                core_post(EV_NOTE_OFF, (uint32_t)ui.track, (uint32_t)key_note(k), 0);
            continue;
        }
        if (b >= 0) {
            if (b == BK_FILL)
                continue;                          /* level-based, below */
            if (down >> k & 1u)
                black_key(b, shift);
            continue;
        }
        /* a white key: a step of the page */
        if (w < 0)
            continue;
        if (down >> k & 1u) {
            ui.held_step = ui.page * PAGE + w;
            ui.held_edited = 0;
            ui.step_page = 0;
        } else if ((up >> k & 1u) && ui.held_step == ui.page * PAGE + w) {
            if (!ui.held_edited && ui.held_step < cur()->len) {
                step_t *s = &cur()->step[ui.held_step];
                s->flags ^= STEP_ON;
                if (s->flags & STEP_ON && !s->vel)
                    s->vel = 100;
            }
            ui.held_step = -1;
        }
    }
    core_fill = !ui.keys_mode && ((in->keys >> UI_BLACK[BK_FILL]) & 1u);
    ui.dirty = 1;
}

static void knobs(const ui_input_t *in)
{
    track_t *t = cur();
    int k;
    if (in->enc[UE_SELECT])
        select_track(ui.track + (in->enc[UE_SELECT] > 0 ? 1 : -1));
    if (in->enc[UE_PRESET]) {
        int p = ((int)t->preset + (in->enc[UE_PRESET] > 0 ? 1 : -1) + (int)NPRESETS) % (int)NPRESETS;
        preset_apply(t, (uint32_t)p);
        if (ui.edit_page >= ui_npages(t))
            ui.edit_page = 0;
        say(PRESETS[p].name, "");
    }
    if (in->enc[UE_ALGO]) {
        int d = in->enc[UE_ALGO] > 0 ? 1 : -1;
        if (ui.held_step >= 0)
            ui.step_page ^= 1;
        else if (ui.view == VIEW_GLOBAL)
            ui.glo_page ^= 1;
        else if (ui.view == VIEW_EDIT || ui.view == VIEW_SEQ)
            ui.edit_page = (ui.edit_page + d + ui_npages(t)) % ui_npages(t);
    }
    for (k = 0; k < 4; k++) {
        int d = accel(in->enc[UE_K1 + k]);
        if (!d)
            continue;
        if (ui.held_step >= 0) {                   /* a held step: its fields */
            step_edit(&t->step[ui.held_step], ui.step_page, k, d);
            ui.held_edited = 1;
        } else if (ui.view == VIEW_GLOBAL && ui.glo_page == 1) {
            if (k == 0)
                ui.scanlines = d > 0;
        } else if (ui.view == VIEW_GLOBAL || ui.view == VIEW_STACK) {
            if (k == 0)
                g_proj.bpm10 = (uint16_t)clampi(g_proj.bpm10 + d * 10, BPM_MIN, BPM_MAX);
            else if (k == 1)
                g_proj.swing = (uint8_t)clampi(g_proj.swing + d, 50, 75);
            else if (k == 2 && ui.view == VIEW_GLOBAL)
                g_proj.bpm10 = (uint16_t)clampi(g_proj.bpm10 + d, BPM_MIN, BPM_MAX);   /* fine tempo */
            else if (k == 3 && ui.view == VIEW_GLOBAL)
                g_proj.seed = (uint8_t)(g_proj.seed + d);
        } else {                                   /* SEQ and EDIT: the track's page */
            int id = ui_page_param(t, ui.edit_page, k), lo, hi;
            int v = ui_param_get(t, id, &lo, &hi);
            param_set(t, id, v + (hi < 20 ? (d > 0 ? 1 : -1) : d));
        }
        ui.dirty = 1;
    }
}

/* ------------------------------------------------------------------ I/O --- */
static void io_input(const ui_input_t *in)
{
    static const char *const BTN[UI_NBUTTONS] = {"FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME",
                                                 "SAVE", "ARP", "SEQ", "PLAY", "REC", "OCT-", "OCT+"};
    static const char *const ENC[UI_NENC] = {"SEL", "ALG", "PRE", "K1", "K2", "K3", "K4"};
    uint32_t i, down = in->buttons & ~ui.prev.buttons, up = ~in->buttons & ui.prev.buttons & ~ui.ignore_buttons;
    uint32_t kd = in->keys & ~ui.prev.keys, ku = ~in->keys & ui.prev.keys & ~ui.ignore_keys;
    char t[12];
    for (i = 0; i < UI_NBUTTONS; i++) {
        if (down >> i & 1u) {
            ui_log("BTN", BTN[i], "DOWN");
            if (i == UB_HOME) {
                ui.view = VIEW_STACK;
                core_post(EV_ALL_OFF, 0, 0, 0);
                ui.ignore_buttons |= 1u << UB_HOME;
            }
        } else if (up >> i & 1u) {
            ui_log("BTN", BTN[i], "UP");
        }
    }
    for (i = 0; i < UI_NKEYS; i++) {
        if (kd >> i & 1u) {
            ui_note_text(t, key_note(i), sizeof t);
            core_post(EV_NOTE_ON, (uint32_t)ui.track, (uint32_t)key_note(i), 100);
            ui_log("KEY", t, "ON");
        } else if (ku >> i & 1u) {
            ui_note_text(t, key_note(i), sizeof t);
            core_post(EV_NOTE_OFF, (uint32_t)ui.track, (uint32_t)key_note(i), 0);
            ui_log("KEY", t, "OFF");
        }
    }
    for (i = 0; i < UI_NENC; i++)
        if (in->enc[i]) {
            ui.enc_total[i] += in->enc[i];
            t[0] = 0;
            if (in->enc[i] > 0)
                ui_cat(t, "+", sizeof t);
            ui_int(t, in->enc[i], sizeof t);
            ui_log("ENC", ENC[i], t);
        }
    ui.dirty = 1;
}

/* ------------------------------------------------------------------- API --- */
void ui_init(void)
{
    uint32_t i;
    for (i = 0; i < sizeof ui; i++)
        ((uint8_t *)&ui)[i] = 0;
    ui.view = VIEW_SPLASH;
    ui.held_step = -1;
    ui.dirty = 1;
}

static int any_enc(const ui_input_t *in)
{
    int i;
    for (i = 0; i < UI_NENC; i++)
        if (in->enc[i])
            return 1;
    return 0;
}

static void leave_splash(const ui_input_t *in)
{
    ui.view = VIEW_STACK;
    ui.ignore_keys = in->keys;                     /* what left the flyer does nothing else */
    ui.ignore_buttons = in->buttons;
    ui.dirty = 1;
}

int ui_frame(const ui_input_t *in, uint32_t now)
{
    uint32_t i, down, up;
    int shift;
    trig_t tg;
    ui.now = now;
    if (!ui.started) {
        ui.started = 1;
        ui.t0 = now;
    }
    while (core_trig_pop(&tg)) {                   /* what played: the lamps (the cones from Phase 3) */
        if (tg.track < NTRACKS) {
            ui.flash_t[tg.track] = now;
            ui.flash_vel[tg.track] = tg.vel;
            stack_trigger(tg.track, tg.vel, tg.flags, now);
        }
        ui.dirty = 1;
    }
    if (ui.view == VIEW_SPLASH) {
        if ((in->buttons & ~ui.prev.buttons) || (in->keys & ~ui.prev.keys) || any_enc(in) ||
            now - ui.t0 >= UI_SPLASH_MS)
            leave_splash(in);
        ui.prev = *in;
        ui_draw(now);
        return 1;
    }
    ui.ignore_buttons &= in->buttons;
    if (ui.view == VIEW_IO) {
        if (in->buttons != ui.prev.buttons || in->keys != ui.prev.keys || any_enc(in))
            io_input(in);
    } else {
        shift = (in->buttons >> UB_HOME) & 1u;
        down = in->buttons & ~ui.prev.buttons & ~ui.ignore_buttons;
        up = ~in->buttons & ui.prev.buttons & ~ui.ignore_buttons;
        if (down >> UB_HOME & 1u)
            ui.shift_used = 0;
        for (i = 0; i < UI_NBUTTONS; i++) {
            if (!(down >> i & 1u) || i == UB_HOME)
                continue;
            if (shift)
                shift_action(i);
            else
                button(i);
        }
        if ((up >> UB_HOME & 1u) && !ui.shift_used) {   /* a HOME tap: the sound system */
            ui.view = VIEW_STACK;
            ui.dirty = 1;
        }
        if (in->keys != ui.prev.keys || (ui.prev.keys & ui.ignore_keys))
            keys_input(in);
        if (any_enc(in))
            knobs(in);
    }
    ui.ignore_keys &= in->keys;
    /* what moves on its own: the playheads, the lamps fading, a message ending */
    for (i = 0; i < NTRACKS; i++) {
        int p = core_play_pos(i);
        if (p != ui.shown_pos[i]) {
            ui.shown_pos[i] = p;
            ui.dirty = 1;
        }
        if (ui.flash_t[i] && now - ui.flash_t[i] > UI_FLASH_MS) {
            ui.flash_t[i] = 0;
            ui.dirty = 1;
        }
    }
    if (stack_update(now) && ui.view == VIEW_STACK)   /* (kept up to date in every view: no stale cabinets) */
        ui.dirty = 1;
    if (ui.view == VIEW_STACK && now / 300u != ui.blink) {   /* the selected cabinet blinks */
        ui.blink = now / 300u;
        ui.dirty = 1;
    }
    if (ui.msg_until && now >= ui.msg_until) {
        ui.msg_until = 0;
        ui.dirty = 1;
    }
    if (in->master / 41u != ui.prev.master / 41u)
        ui.dirty = 1;
    if (core_playing() != ui.shown_play || core_rec != ui.shown_rec) {
        ui.shown_play = core_playing();
        ui.shown_rec = core_rec;
        ui.dirty = 1;
    }
    ui.prev = *in;
    if (!ui.dirty)
        return 0;
    ui.dirty = 0;
    ui_draw(now);
    return 1;
}

void ui_leds(ui_leds_t *out)
{
    uint32_t i;
    out->buttons = ui.prev.buttons;
    out->keys = ui.prev.keys;
    if (ui.view == VIEW_SPLASH) {                  /* the flyer: a light chasing along the white keys at 128 BPM */
        uint32_t step = ((ui.now - ui.t0) * 128u * 4u / 60000u) % 16u;
        out->keys |= 1u << UI_WHITE[step];
        return;
    }
    if (core_playing())
        out->buttons |= 1u << UB_PLAY;
    if (core_rec)
        out->buttons |= 1u << UB_REC;
    if (ui.keys_mode)
        return;
    /* step mode: the page's steps that are on, the playhead; the selected track's black key */
    for (i = 0; i < PAGE; i++) {
        int s = ui.page * PAGE + (int)i;
        if (s < cur()->len && (cur()->step[s].flags & STEP_ON))
            out->keys |= 1u << UI_WHITE[i];
    }
    {
        int p = core_play_pos((uint32_t)ui.track);
        if (p >= ui.page * PAGE && p < ui.page * PAGE + PAGE)
            out->keys ^= 1u << UI_WHITE[p - ui.page * PAGE];
    }
    out->keys |= 1u << UI_BLACK[ui.track];
}

int ui_view(void) { return ui.view; }
int ui_track(void) { return ui.track; }
int ui_keys_mode(void) { return ui.keys_mode; }

void ui_set_stats(uint32_t cpu_pct, uint32_t xruns)
{
    if (cpu_pct != ui.cpu_pct || xruns != ui.xruns) {
        ui.cpu_pct = cpu_pct;
        ui.xruns = xruns;
        if (ui.view == VIEW_IO || ui.view == VIEW_GLOBAL)
            ui.dirty = 1;
    }
}

void ui_say(const char *a, const char *b)
{
    ui_log("SAY", a ? a : "", b);
    say(a ? a : "", b);
}
