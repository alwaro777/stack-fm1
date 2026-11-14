/* SPDX-License-Identifier: GPL-3.0-or-later
 * STACK — the project's data: steps, tracks, the pattern, the global settings. Plain structs, fixed sizes, no pointers
 * (so the whole thing can be saved to flash and exported as a file as it is, from Phase 5).
 *
 * Sharing: the UI (main loop) edits these; the audio interrupt reads them. Every field the audio reads is one byte or
 * one aligned 32-bit word, so a read never sees half a write; a step being edited may play with its old or new values
 * for one step, never anything else. */
#ifndef STK_MODEL_H
#define STK_MODEL_H
#include <stdint.h>

#define NTRACKS 8
#define NSTEPS 64                /* per track, in pages of 16 */
#define PAGE 16

/* ------------------------------------------------------------------ step --- */
enum { STEP_ON = 1, STEP_ACCENT = 2, STEP_SLIDE = 4 };

/* trig conditions: when a step that is on actually plays */
enum {
    COND_ALWAYS, COND_FILL, COND_NOT_FILL, COND_FIRST, COND_NOT_FIRST,
    COND_1_2, COND_2_2, COND_1_3, COND_2_3, COND_3_3, COND_1_4, COND_2_4, COND_3_4, COND_4_4,
    NCONDS
};

typedef struct {
    uint8_t flags;               /* STEP_* */
    uint8_t note;                /* MIDI note; drums: 60 = the preset's own pitch */
    uint8_t vel;                 /* 1..127 */
    uint8_t gate;                /* length in 1/16 of a step: 16 = one step, up to 64 (four steps) */
    uint8_t prob;                /* 0..100 % */
    uint8_t ratchet;             /* hits within the step, 1..4 */
    int8_t micro;                /* nudge in ticks, -11..+11 (24 ticks = one step) */
    uint8_t cond;                /* COND_* */
} step_t;

/* --------------------------------------------------------------- params --- */
/* one track's sound: 0..127 each. Engines read the ones they use (engine.h names them per engine). */
enum {
    PR_LEVEL, PR_PAN, PR_TUNE, PR_DECAY, PR_TONE, PR_SNAP, PR_CUTOFF, PR_RESO, PR_ENVMOD, PR_DRIVE, PR_ACCENT,
    PR_WAVE,
    /* Phase 4 */
    PR_ATTACK, PR_SUSTAIN, PR_RELEASE,   /* the amplitude's ADSR with DECAY (synth, FM, FX) */
    PR_DETUNE,                   /* oscillator spread (supersaw, reese, ensemble) */
    PR_MOD,                      /* modulation depth: FM index, siren and riser depth */
    PR_CHORD,                    /* 0 = single notes, 1 = the track's chord memory, 2.. = a chord from the table */
    PR_SEND_D, PR_SEND_R,        /* sends to the delay and the reverb */
    NPARAMS
};

enum { CAT_SUB, CAT_MID, CAT_TOP, NCATS };   /* where the track's cabinet goes in the stack (Phase 3) */
enum { DIR_FWD, DIR_REV, DIR_PINGPONG, DIR_RANDOM, NDIRS };
/* the track's step length: ticks per step at 96 ticks a quarter note */
enum { SCALE_32, SCALE_16, SCALE_8, SCALE_4, SCALE_16T, SCALE_8T, NSCALES };

typedef struct {
    step_t step[NSTEPS];
    uint8_t len;                 /* 1..64 */
    uint8_t scale;               /* SCALE_* */
    uint8_t dir;                 /* DIR_* */
    uint8_t preset;              /* the preset it was made from (its name, its engine) */
    uint8_t engine, model;       /* ENGINE_* and the engine's model */
    uint8_t category;            /* CAT_* */
    uint8_t midi_ch;             /* 0 = none, 1..16 (Phase 5) */
    uint8_t p[NPARAMS];
    int8_t chord[4];             /* the chord memory: intervals in semitones (the first is 0), -128 = unused */
} track_t;

typedef struct {
    track_t trk[NTRACKS];        /* the kit (each track's sound) and the current pattern's steps */
} pattern_t;

/* ------------------------------------------------------- patterns, locks --- */
#define NPATTERNS 16
#define NLOCKS 128               /* parameter locks per pattern */
#define LOCK_NONE 0xFFu
typedef struct {
    uint8_t trk;                 /* LOCK_NONE = a free slot */
    uint8_t step, param, value;
} plock_t;

/* a stored pattern: everything a pattern has beyond the kit */
typedef struct {
    step_t step[NTRACKS][NSTEPS];
    uint8_t len[NTRACKS], scale[NTRACKS], dir[NTRACKS];
    plock_t lock[NLOCKS];
} bank_t;
extern bank_t g_bank[NPATTERNS];

#define NCHAIN 16
#define NSONG 32
typedef struct {
    uint8_t pat, reps;           /* pattern index, how many times (1..16) */
} songrow_t;

enum { SCALE_CHROMATIC, SCALE_MAJOR, SCALE_MINOR, SCALE_DORIAN, SCALE_PHRYGIAN, SCALE_MIXOLYDIAN, SCALE_HARMMIN,
       SCALE_PENTA_MIN, SCALE_PENTA_MAJ, SCALE_BLUES, NSCALE_TYPES };
enum { PLAY_PATTERN, PLAY_CHAIN, PLAY_SONG };

/* --------------------------------------------------------------- global --- */
/* the master bus: the send effects, the pump, the compressor, the DJ filter (0..127 each) */
enum {
    FX_DLY_TIME,                 /* in 16ths: index into the delay's time list (1/16 .. 1/2, dotted 1/8 the dub one) */
    FX_DLY_FB, FX_DLY_TONE, FX_DLY_LEVEL,
    FX_REV_SIZE, FX_REV_DAMP, FX_REV_LEVEL,
    FX_PUMP,                     /* sidechain depth: the kicks duck everything else */
    FX_PUMP_REL,
    FX_COMP,                     /* master compression amount */
    FX_DJF,                      /* one knob: 64 open, under it low-pass, over it high-pass */
    NFX
};

typedef struct {
    pattern_t pat;
    uint16_t bpm10;              /* tempo in tenths: 200..3000 = 20..300 BPM */
    uint8_t swing;               /* 50..75 %: where the off-beat 16th sits inside its 8th */
    uint8_t seed;                /* the probability's dice start from this at each PLAY (repeatable) */
    uint8_t fx[NFX];
    /* Phase 5 */
    uint8_t cur_pat;             /* the pattern in pat (0..15) */
    plock_t lock[NLOCKS];        /* its parameter locks */
    uint8_t play_mode;           /* PLAY_* */
    uint8_t chain[NCHAIN], chain_len;
    songrow_t song[NSONG];
    uint8_t song_len;
    uint8_t scale, tonic;        /* the keys and the NOTE knob keep to this scale (tonic 0 = C) */
    uint8_t clk_out, clk_in;     /* MIDI clock: send (0/1); follow (0 internal, 1 external) */
    uint8_t transport;           /* bit 0 send start/stop, bit 1 obey it */
} project_t;

extern project_t g_proj;

#define BPM_MIN 200
#define BPM_MAX 3000

#endif
