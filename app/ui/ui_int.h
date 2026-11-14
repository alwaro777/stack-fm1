/* SPDX-License-Identifier: GPL-3.0-or-later
 * STACK — the UI's internals, shared by ui.c (state, input) and ui_draw.c (the views). */
#ifndef STK_UI_INT_H
#define STK_UI_INT_H
#include "fb.h"
#include "ui.h"
#include "../core/core.h"
#include "../core/engine.h"

#define UI_SPLASH_MS 3500u
#define UI_FLASH_MS 110u         /* a track's lamp after it plays */
#define UI_LOG_LINES 6
#define UI_LOG_LEN 28

/* pseudo-params on the knob pages: the track's sequencer settings */
enum { PX_LEN = NPARAMS, PX_SCALE, PX_DIR, PX_CAT, PX_CH, PX_NONE };

typedef struct {
    int view;
    int started;
    uint32_t t0, now;
    ui_input_t prev;
    uint32_t ignore_keys, ignore_buttons;   /* held when a view changed under them: no event until released */
    int track, page, octave, keys_mode;
    int edit_page, glo_page, fx_page;
    int pat_page, song_cursor;   /* the pattern view: 0 patterns, 1 the song */
    int chain_started;           /* ARP held: the first pattern key since it went down restarts the chain */
    int req_save, unsaved;
    bank_t pclip;                /* a whole pattern copied */
    int has_pclip;
    /* the demo, built up: its full first pattern kept aside, its tracks brought in two bars apart */
    bank_t build_src;
    uint8_t build_order[NTRACKS];
    int building, build_layer, build_bars, build_last_pos, demo_next;
    int scanlines;               /* the CRT look on the sound system */
    uint32_t blink;              /* the selected cabinet's blink phase last drawn */
    int held_step, held_edited, step_page;
    int shift_used;              /* SHIFT did something: its release is not a HOME tap */
    uint32_t flash_t[NTRACKS];
    uint8_t flash_vel[NTRACKS];
    int shown_pos[NTRACKS], shown_play, shown_rec, shown_pat, shown_cue;
    track_t undo, clip;
    int undo_track, has_undo, has_clip;
    char msg[28];
    uint32_t msg_until;
    char log[UI_LOG_LINES][UI_LOG_LEN];
    uint32_t log_n;
    int32_t enc_total[UI_NENC];
    uint32_t cpu_pct, xruns;
    int dirty;
} ui_t;

extern ui_t ui;
extern const char *const UI_STEP_FIELD[2][4];
extern const char *const UI_COND_NAME[NCONDS];

void ui_cat(char *d, const char *s, uint32_t cap);
void ui_note_text(char *d, int32_t note, uint32_t cap);
void ui_log(const char *a, const char *b, const char *c);
int ui_npages(const track_t *t);
int ui_page_param(const track_t *t, int page, int k);
const char *ui_param_name(const track_t *t, int id);
int ui_param_get(const track_t *t, int id, int *lo, int *hi);
void ui_param_text(const track_t *t, int id, char *d, uint32_t cap);
void ui_step_text(const step_t *s, int page, int k, char *d, uint32_t cap);
void ui_draw(uint32_t now);      /* the current view into fb */
void ui_header(const char *left);
void ui_pattern_text(char *d, uint32_t cap);   /* "P01", "P01>P03", with C (chain) or S (song) */
extern const int8_t UI_FX_PAGE[3][4];
extern const char *const UI_FX_NAME[NFX];
void ui_fx_text(int id, char *d, uint32_t cap);
void ui_steps_bar(int32_t y);    /* the selected track's page of steps, small, with the playhead */

/* the sound system (stack.c) */
int stack_update(uint32_t now);  /* the cabinets follow the pattern; 1 while something moves */
void stack_trigger(uint32_t track, uint32_t vel, uint32_t flags, uint32_t now);
void stack_draw(uint32_t now);
void stack_reset(void);
int stack_box(uint32_t t, int32_t *x, int32_t *y, int *kind, int *exc, int *state);   /* 1 if on the stage */

#endif
