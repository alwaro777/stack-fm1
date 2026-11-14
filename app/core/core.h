/* SPDX-License-Identifier: GPL-3.0-or-later
 * STACK — portable audio core: the sequencer, the engines' voices, the mixer. No hardware, no malloc, no float.
 * Built three ways from the same source: the FM-1 (inside fm1-base's unity build), the browser (Emscripten) and the
 * host (plain cc, tests). */
#ifndef STK_CORE_H
#define STK_CORE_H
#include <stdint.h>
#include "model.h"

/* where the big buffers live: the platform glue puts them in its large RAM pool (fm1-base PLAT_POOL) */
#ifndef STK_BIG
#define STK_BIG
#endif

#define STK_SR 44100u            /* nominal rate (the FM-1 really runs at 24 MHz / 544 = 44 117.6 Hz) */
#define STK_MAX_BLOCK 256u       /* frames per render call, upper bound (app.h APP_BLOCK) */
#define PPQN 96                  /* sequencer ticks per quarter note: a 16th is 24 */

/* UI -> audio events: a single-producer / single-consumer ring, no locks (the UI writes head, the audio tail) */
enum { EV_NOTE_ON = 1, EV_NOTE_OFF, EV_ALL_OFF, EV_PLAY, EV_STOP, EV_PATTERN };   /* EV_PATTERN: note = it */
#define STK_EVQ_LEN 64u

/* audio -> UI: every step that played (the stack's cones, the playhead's flash, the tests' timing checks) */
typedef struct {
    uint32_t sample;             /* when, in samples since the last PLAY */
    uint8_t track, note, vel, flags;
} trig_t;
#define STK_TRIGQ_LEN 64u

/* shared switches the UI sets and the audio reads (one word each) */
extern volatile uint32_t core_mute, core_solo;   /* bit t = track t */
extern volatile uint8_t core_fill;               /* FILL held */
extern volatile uint8_t core_rec;                /* live recording armed: notes played while running are written */
extern volatile uint8_t core_fx_bypass;          /* tests: the master bus skipped (to prove neutral is transparent) */

void core_init(void);            /* the default project: the demo pattern, stopped */
void core_clear_project(void);   /* an empty pattern on the default kit */
void core_load_demo(void);       /* a 138 BPM rave pattern on tracks 1-5 and 7 */

/* main loop side: queue an event; 0 if the ring is full (dropped, never blocks) */
int core_post(uint32_t type, uint32_t track, uint32_t note, uint32_t vel);
/* audio side: n stereo frames into lr (interleaved, signed 24-bit in int32), MASTER gain in Q12 (4096 = unity) */
void core_render(int32_t *lr, uint32_t n, uint32_t master_q12);
void core_panic(void);           /* silence now, from the main loop (before the flash is rewritten) */

int core_playing(void);
int core_play_pos(uint32_t track);   /* the step sounding now, -1 when stopped */
uint32_t core_loops(uint32_t track); /* completed loops of the track since PLAY */
int core_trig_pop(trig_t *t);        /* 1 = one taken */
uint32_t core_voices_active(void);   /* tracks whose voice is sounding */
uint32_t core_peak(void);            /* output peak since the last call, Q15 */

/* patterns (pattern.c) and where the sequencer is */
int core_pattern(void);              /* the current pattern */
int core_cue(void);                  /* the one coming at the boundary, -1 none */
int core_song_row(void);
void step_default(step_t *s);
void bank_clear(bank_t *b);
void locks_clear(plock_t *l);
void pattern_swap_track(uint32_t t, uint32_t from, uint32_t to);
void pattern_store_all(void);
void pattern_load_current(void);
void pattern_copy_out(bank_t *b);
void pattern_copy_in(const bank_t *b);

/* the project in bytes (project.c): one store object each; decode returns 0, or -1 for a bad or foreign object */
#define PROJ_IO_MAX 3840u
uint32_t proj_encode_settings(uint8_t *b);
int proj_decode_settings(const uint8_t *b, uint32_t len);
uint32_t proj_encode_kit(uint8_t *b);
int proj_decode_kit(const uint8_t *b, uint32_t len);
uint32_t proj_encode_pattern(uint32_t p, uint8_t *b);   /* from the bank: pattern_store_all() first */
int proj_decode_pattern(uint32_t p, const uint8_t *b, uint32_t len);
void pattern_switch_now(uint32_t to);
uint32_t bank_ticks(const bank_t *b);
uint32_t pattern_ticks(void);
int bank_used(uint32_t p);
int lock_get(uint32_t t, uint32_t step, uint32_t param);
int lock_set(uint32_t t, uint32_t step, uint32_t param, uint32_t value);
uint32_t lock_clear_step(uint32_t t, uint32_t step);
int step_has_locks(uint32_t t, uint32_t step);
extern const char *const SCALE_NAME[NSCALE_TYPES];
int scale_has(int note);
int scale_snap(int note);
int scale_step(int note, int d);

/* MIDI: the platform hands what comes in to core_midi_in (in the audio interrupt, before core_render) and sends what
 * core_midi_pop gives (USB-MIDI event packets: cable/CIN, status, data 1, data 2) */
void core_midi_in(uint32_t status, uint32_t d1, uint32_t d2);
int core_midi_pop(uint32_t *pkt);
uint32_t core_ext_bpm10(void);       /* the followed clock's tempo (tenths), 0 if none */

/* the master bus (fx.c) */
void fx_init(void);
void fx_pump_kick(void);
int fx_pump_fill(int32_t *duck, uint32_t n);
void fx_bus(int32_t *l, int32_t *r, const int32_t *sd, const int32_t *sr, uint32_t n, int32_t spt_q16, int any_dly,
            int any_rev);
extern const uint8_t DLY_STEPS[6];
extern const char *const DLY_NAME[6];

#endif
