/* SPDX-License-Identifier: GPL-3.0-or-later
 * STACK — the UI: hardware-free. It reads a snapshot of the controls, edits the project (core/model.h), sends events
 * to the core and draws into fb. The platform side (app.c) fills ui_input_t from plat.h; tests fill it by hand. */
#ifndef STK_UI_H
#define STK_UI_H
#include <stdint.h>

/* the FM-1's controls, in fm1-base's plat.h order (B_*, EN_*) */
enum { UB_FX, UB_SEL, UB_ENV, UB_LFO, UB_EDIT, UB_GLO, UB_HOME, UB_SAVE, UB_ARP, UB_SEQ, UB_PLAY, UB_REC,
       UB_OCTDN, UB_OCTUP, UI_NBUTTONS };
enum { UE_SELECT, UE_ALGO, UE_PRESET, UE_K1, UE_K2, UE_K3, UE_K4, UI_NENC };
#define UI_NKEYS 27              /* F3 .. G5: 16 white, 11 black */

/* key k by colour, left to right (plat.h PLAT_WHITE_KEYS / PLAT_BLACK_KEYS) */
extern const uint8_t UI_WHITE[16], UI_BLACK[11];
/* the black keys' jobs in step mode: 1-8 tracks, then these */
enum { BK_PAGE = 8, BK_FILL = 9, BK_KEYS = 10 };

typedef struct {
    uint32_t buttons;            /* held, bit UB_* */
    uint32_t keys;               /* held, bit k = key k (0 = F3) */
    int32_t enc[UI_NENC];        /* detents since the last frame, + = clockwise */
    uint32_t master;             /* MASTER, 0..4096 */
} ui_input_t;

typedef struct {                 /* what the UI asks of the panel's lights */
    uint32_t buttons, keys;
} ui_leds_t;

/* VIEW_STACK: the sound system, the main screen */
enum { VIEW_SPLASH, VIEW_SEQ, VIEW_EDIT, VIEW_GLOBAL, VIEW_STACK, VIEW_HELP, VIEW_IO };

void ui_init(void);
/* one UI frame: input, then a redraw into fb when something changed. now in ms. Returns 1 if fb changed. */
int ui_frame(const ui_input_t *in, uint32_t now);
void ui_leds(ui_leds_t *out);
int ui_view(void);
int ui_track(void);              /* the selected track */
int ui_keys_mode(void);
void ui_set_stats(uint32_t cpu_pct, uint32_t xruns);
void ui_say(const char *a, const char *b);   /* a short message from the platform ("SAVED", errors) */
void ui_draw_splash(uint32_t t); /* the flyer at time t (ms): also APP_SPLASH's, before the main loop runs */

/* a speaker cabinet (the stack's building block): kind 0 = SUB (one big cone), 1 = MID (horn), 2 = TOP (tweeters).
 * excursion 0..3 pushes the cone out; dim draws it powered off (grey) */
void ui_draw_cabinet(int32_t x, int32_t y, int32_t w, int32_t h, int kind, int excursion, int dim);

#endif
