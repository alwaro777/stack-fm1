/* SPDX-License-Identifier: GPL-3.0-or-later
 * STACK — sound engines and the factory presets. One voice per track for now (drums retrigger, the acid bass is mono
 * with slide, as the originals are); Phase 4 adds the polyphonic engines and a shared voice pool. */
#ifndef STK_ENGINE_H
#define STK_ENGINE_H
#include <stdint.h>
#include "model.h"
#include "dsp.h"

enum { ENGINE_DRUM, ENGINE_ACID, NENGINES };

/* drum models */
enum { DM_KICK808, DM_KICK909, DM_GABBER, DM_SNARE, DM_CLAP, DM_HAT_C, DM_HAT_O, DM_RIM, DM_TOM, DM_COWBELL,
       NDMODELS };

typedef struct {
    uint8_t model, active;
    uint32_t ph[6], inc[6];      /* oscillators */
    uint32_t rnd;                /* noise state */
    denv_t amp, aux;             /* amplitude; pitch or noise envelope (per model) */
    int32_t z[4];                /* filter states */
    int32_t base_inc, sweep;     /* kick and tom pitch: inc = base + sweep * aux */
    int32_t vel;                 /* Q15 */
    uint32_t t;                  /* samples since the trigger */
    int32_t drive, snap, tone;   /* params at the trigger, scaled */
} dvoice_t;

typedef struct {
    uint32_t ph, rnd;
    int32_t note256, target256;  /* pitch now and where a slide goes, 1/256 semitone */
    int32_t z[4];                /* the ladder's four poles */
    denv_t fenv;                 /* filter envelope */
    int32_t amp;                 /* Q16 */
    int32_t amp_target;
    uint8_t gate, accent, active, n;
    int32_t fcut256, g, k;       /* control-rate filter values */
} avoice_t;

typedef union {
    dvoice_t d;
    avoice_t a;
} voice_t;

/* the engines: trigger a note (vel 1..127; accent, slide: STEP_* flags), release it, render n samples (Q15, mono,
 * added to out) */
void drum_trigger(dvoice_t *v, const track_t *t, uint32_t note, uint32_t vel, uint32_t flags);
void drum_choke(dvoice_t *v);
void drum_render(dvoice_t *v, const track_t *t, int32_t *out, uint32_t n);
void acid_trigger(avoice_t *v, const track_t *t, uint32_t note, uint32_t vel, uint32_t flags, int legato);
void acid_release(avoice_t *v);
void acid_render(avoice_t *v, const track_t *t, int32_t *out, uint32_t n);

/* ---------------------------------------------------------------- presets --- */
typedef struct {
    char name[12];
    uint8_t engine, model, category;
    uint8_t p[NPARAMS];
} preset_t;

extern const preset_t PRESETS[];
extern const uint32_t NPRESETS;
void preset_apply(track_t *t, uint32_t preset);

/* what each engine calls its parameters (NULL: unused, shown as "--"), for the UI */
extern const char *const PARAM_NAME[NENGINES][NPARAMS];

#endif
