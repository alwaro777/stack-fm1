/* SPDX-License-Identifier: GPL-3.0-or-later
 * STACK — sound engines and the factory presets.
 * Mono engines own one voice per track (drums retrigger, the acid bass and the FX slide or sustain, as the originals
 * do). The polyphonic engines (the VA synth, FM) take voices from a shared pool of 16 (voices.c), up to 4 a track,
 * with deterministic stealing. */
#ifndef STK_ENGINE_H
#define STK_ENGINE_H
#include <stdint.h>
#include "model.h"
#include "dsp.h"

enum { ENGINE_DRUM, ENGINE_ACID, ENGINE_SYNTH, ENGINE_FM, ENGINE_FX, NENGINES };

/* drum models */
enum { DM_KICK808, DM_KICK909, DM_GABBER, DM_SNARE, DM_CLAP, DM_HAT_C, DM_HAT_O, DM_RIM, DM_TOM, DM_COWBELL,
       DM_SHAKER, DM_RIDE, DM_CRASH, NDMODELS };
#define DM_IS_KICK(m) ((m) <= DM_GABBER)
/* VA synth models: how many voices, and the special behaviours */
enum { SY_POLY, SY_MONO, SY_HOOVER, SY_CHORD, NSYMODELS };
/* the oscillators the synth's WAVE picks */
enum { OSC_SAW, OSC_SQUARE, OSC_SUPER, OSC_PWM, OSC_SINE, NOSCS };
/* FM models: the operator pairs' ratios and envelopes */
enum { FMM_EPIANO, FMM_BELL, FMM_MALLET, FMM_BASS, NFMMODELS };
/* FX models */
enum { FXM_RISER, FXM_DOWN, FXM_SIREN, FXM_LASER, FXM_SWEEP, FXM_IMPACT, FXM_VINYL, NFXMODELS };

typedef struct {
    uint8_t model, active;
    uint32_t ph[6], inc[6];      /* oscillators */
    uint32_t ph_bell;            /* the ride's bell partial */
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

typedef struct {                 /* the FX engine: one voice, held by the gate */
    uint8_t model, active, gate;
    uint32_t ph[2], rnd, t, len;  /* t: samples since the trigger; len: the gesture's length (DECAY) */
    int32_t note256, vel;
    svf_t f;
    adsr_t amp;
    int32_t z[2];
} fxvoice_t;

typedef union {
    dvoice_t d;
    avoice_t a;
    fxvoice_t x;
} voice_t;

/* a pool voice: the VA synth's or FM's */
typedef struct {
    uint32_t ph[6];              /* synth: up to 5 saws + a sub; FM: four operators */
    int32_t note256, target256, vel;
    int32_t pitch_env;           /* hoover: the dip at the start, Q16 */
    uint32_t lfo;
    adsr_t amp, mod;             /* amplitude; FM index / filter envelope */
    svf_t f;
    int32_t fb;                  /* FM feedback memory */
} pvoice_t;

typedef struct {
    uint8_t used, track, note, key;   /* key: the step's note (a chord's voices share it, for the note-off) */
    uint8_t gate, engine, model;
    uint32_t age;
    pvoice_t v;
} pool_t;
#define NPOOL 16
#define POLY_MAX 4

/* the engines: trigger a note (vel 1..127; accent, slide: STEP_* flags), release it, render n samples (Q15, mono,
 * added to out) */
void drum_trigger(dvoice_t *v, const track_t *t, uint32_t note, uint32_t vel, uint32_t flags);
void drum_choke(dvoice_t *v);
void drum_render(dvoice_t *v, const track_t *t, int32_t *out, uint32_t n);
void acid_trigger(avoice_t *v, const track_t *t, uint32_t note, uint32_t vel, uint32_t flags, int legato);
void acid_release(avoice_t *v);
void acid_render(avoice_t *v, const track_t *t, int32_t *out, uint32_t n);
void fx_trigger(fxvoice_t *v, const track_t *t, uint32_t note, uint32_t vel);
void fx_release(fxvoice_t *v);
void fx_render(fxvoice_t *v, const track_t *t, int32_t *out, uint32_t n);
/* pool engines (voices.c drives them) */
void synth_start(pvoice_t *v, const track_t *t, int32_t note256, uint32_t vel, int legato);
void synth_render(pvoice_t *v, const track_t *t, int32_t *out, uint32_t n);
void fm_start(pvoice_t *v, const track_t *t, int32_t note256, uint32_t vel);
void fm_render(pvoice_t *v, const track_t *t, int32_t *out, uint32_t n);

/* the voice pool (voices.c) */
void pool_init(void);
void pool_note_on(uint32_t track, const track_t *t, uint32_t note, uint32_t vel, uint32_t flags, int legato);
void pool_note_off(uint32_t track, uint32_t note);   /* note 255: every note of the track */
int pool_render(uint32_t track, const track_t *t, int32_t *out, uint32_t n);   /* 1 if a voice sounded */
uint32_t pool_active(uint32_t track);
extern pool_t g_pool[NPOOL];
/* the chord a note becomes on a track: intervals, n of them (1..4) */
uint32_t chord_of(const track_t *t, int8_t iv[4]);
extern const char *const CHORD_NAME[];
extern const uint32_t NCHORDS;

/* ---------------------------------------------------------------- presets --- */
enum { PG_DRUMS, PG_BASS, PG_LEAD, PG_KEYS, PG_PAD, PG_FX, NPGROUPS };
typedef struct {
    char name[12];
    uint8_t group, engine, model, category;
    uint8_t p[NPARAMS];
} preset_t;
extern const char *const PRESET_GROUP[NPGROUPS];
int preset_find(const char *name);   /* index by name, -1 if none */

extern const preset_t PRESETS[];
extern const uint32_t NPRESETS;
void preset_apply(track_t *t, uint32_t preset);

/* what each engine calls its parameters (NULL: unused, shown as "--"), for the UI */
extern const char *const PARAM_NAME[NENGINES][NPARAMS];

#endif
