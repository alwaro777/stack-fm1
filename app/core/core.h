/* SPDX-License-Identifier: GPL-3.0-or-later
 * STACK — portable audio core. No hardware, no malloc, no float in the audio path.
 * Built three ways from the same source: the FM-1 (inside fm1-base's unity build), the browser (Emscripten) and the
 * host (plain cc, tests). */
#ifndef STK_CORE_H
#define STK_CORE_H
#include <stdint.h>

#define STK_SR 44100u            /* nominal rate (the FM-1 really runs at 24 MHz / 544 = 44 117.6 Hz) */
#define STK_MAX_BLOCK 256u       /* frames per render call, upper bound (app.h APP_BLOCK) */
#define STK_TEST_VOICES 4u       /* F1: the test voice's polyphony */

/* Events from the UI (main loop) to the audio interrupt: a single-producer / single-consumer ring, no locks.
 * The UI is the only writer of head, the audio the only writer of tail. */
enum { EV_NOTE_ON = 1, EV_NOTE_OFF = 2, EV_ALL_OFF = 3 };
#define STK_EVQ_LEN 64u          /* power of two */
typedef struct {
    volatile uint32_t head, tail;
    uint32_t ev[STK_EVQ_LEN];    /* type << 24 | note << 16 | velocity << 8 */
} stk_evq_t;

void core_init(void);
/* main loop side: queue an event; 0 if the ring is full (the event is dropped, never blocks) */
int core_post(uint32_t type, uint32_t note, uint32_t vel);
/* audio side: n stereo frames into lr (interleaved, signed 24-bit in int32), MASTER gain in Q12 (4096 = unity) */
void core_render(int32_t *lr, uint32_t n, uint32_t master_q12);
/* silence now, from the main loop (before the flash is rewritten): every voice cut, no release */
void core_panic(void);
uint32_t core_voices_active(void);   /* for the UI: how many test voices sound */
uint32_t core_peak(void);        /* output peak since the last call, 0..32767 (Q15) */

#endif
