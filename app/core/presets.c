/* SPDX-License-Identifier: GPL-3.0-or-later
 * STACK — the factory library, by group (PRESET turns through it in this order), and the engines' parameter names.
 * Param order: LEVEL PAN TUNE DECAY TONE SNAP CUTOFF RESO ENVMOD DRIVE ACCENT WAVE ATTACK SUSTAIN RELEASE DETUNE MOD
 * CHORD SEND_D SEND_R. The synth's WAVE: ~10 SAW, ~38 SQUARE, ~64 SUPER, ~90 PWM, ~115 SINE; its TONE: ~10 low-pass,
 * ~64 band-pass, ~100 high-pass. CHORD: 0 off, 1 the track's memory, then MAJ MIN MIN7 MAJ7 DOM7 SUS4 FIFTHS MIN9 OCTAVE. */
#include "engine.h"

#define D(name, model, cat, lvl, tune, decay, tone, snap, drive) \
    {name, PG_DRUMS, ENGINE_DRUM, model, cat, {lvl, 64, tune, decay, tone, snap, 0, 0, 0, drive, 0, 0,             \
                                               0, 0, 0, 0, 0, 0, 0, 0}}
#define A(name, grp, cat, lvl, tune, decay, cutoff, reso, envmod, drive, accent, wave, slope)                       \
    {name, grp, ENGINE_ACID, 0, cat, {lvl, 64, tune, decay, 0, slope, cutoff, reso, envmod, drive, accent, wave,   \
                                      0, 0, 0, 0, 0, 0, 0, 0}}
/* the rest, every param: level tune decay tone cutoff reso envmod drive wave attack sustain release detune mod chord
 * send_d send_r (PAN centred, SNAP and ACCENT unused) */
#define P(name, grp, eng, model, cat, lvl, tune, decay, tone, cutoff, reso, envmod, drive, wave, att, sus, rel, det,  \
          mod, chord, sd, sr)                                                                                     \
    {name, grp, eng, model, cat, {lvl, 64, tune, decay, tone, 0, cutoff, reso, envmod, drive, 0, wave, att, sus, rel, \
                                  det, mod, chord, sd, sr}}

const char *const PRESET_GROUP[NPGROUPS] = {"DRUMS", "BASS", "LEAD", "KEYS", "PAD", "FX"};

const preset_t PRESETS[] = {
    /*  name          model        cat      lvl tune dec tone snap drive */
    D("808 BOOM",     DM_KICK808,  CAT_SUB, 118, 64, 98, 70, 20, 10),
    D("909 PUNCH",    DM_KICK909,  CAT_SUB, 112, 64, 80, 50, 70, 40),
    D("GABBER",       DM_GABBER,   CAT_SUB, 96,  64, 82, 40, 60, 100),
    D("SNARE",        DM_SNARE,    CAT_MID, 104, 64, 64, 50, 58, 25),
    D("CLAP",         DM_CLAP,     CAT_MID, 104, 64, 60, 40, 0, 20),
    D("CL HAT",       DM_HAT_C,    CAT_TOP, 86,  64, 48, 40, 30, 0),
    D("OP HAT",       DM_HAT_O,    CAT_TOP, 80,  64, 84, 40, 30, 0),
    D("RIM",          DM_RIM,      CAT_MID, 92,  64, 34, 50, 60, 10),
    D("TOM",          DM_TOM,      CAT_MID, 104, 64, 74, 50, 20, 10),
    D("COWBELL",      DM_COWBELL,  CAT_MID, 96,  64, 78, 50, 0, 10),
    D("SHAKER",       DM_SHAKER,   CAT_TOP, 100, 64, 46, 60, 0, 0),
    D("RIDE",         DM_RIDE,     CAT_TOP, 76,  64, 100, 50, 20, 0),
    D("CRASH",        DM_CRASH,    CAT_TOP, 80,  64, 108, 40, 40, 0),
    /*  name          group    cat      lvl tune dec  cut  res  env  drv  acc wave slope */
    A("ACID SAW",     PG_BASS, CAT_SUB, 100, 64, 76, 52,  100, 92,  30,  90, 0,   0),
    A("ACID SQUARE",  PG_BASS, CAT_SUB, 100, 64, 70, 48,  104, 96,  40,  90, 60,  0),
    A("SUB BASS",     PG_BASS, CAT_SUB, 112, 64, 40, 110, 0,   0,   10,  0,  110, 0),
    /*  name          group    engine        model       cat      lvl tune dec tone cut res env drv wave att sus rel det mod chd sd  sr */
    P("REESE",        PG_BASS, ENGINE_SYNTH, SY_MONO,    CAT_SUB, 104, 64, 60, 10, 55, 20, 20, 40, 10,  0, 100, 40, 70, 0,  0, 0,  0),
    P("FM BASS",      PG_BASS, ENGINE_FM,    FMM_BASS,   CAT_SUB, 104, 64, 50, 50, 0,  0,  0,  30, 0,   0, 70,  30, 0,  90, 0, 0,  0),
    P("HOOVER",       PG_LEAD, ENGINE_SYNTH, SY_HOOVER,  CAT_MID, 96,  64, 70, 10, 90, 10, 30, 30, 64,  2, 90,  50, 90, 0,  0, 20, 20),
    P("RAVE STAB",    PG_LEAD, ENGINE_SYNTH, SY_CHORD,   CAT_MID, 100, 64, 66, 10, 80, 30, 60, 30, 64,  0, 0,   50, 70, 0,  4, 40, 20),
    P("SQUARE LEAD",  PG_LEAD, ENGINE_SYNTH, SY_MONO,    CAT_MID, 92,  64, 60, 10, 85, 20, 30, 20, 38,  0, 90,  40, 10, 0,  0, 30, 10),
    P("PLUCK",        PG_LEAD, ENGINE_SYNTH, SY_POLY,    CAT_MID, 100, 64, 70, 10, 50, 40, 90, 10, 10,  0, 0,   55, 0,  0,  0, 50, 20),
    A("ACID LEAD",    PG_LEAD, CAT_MID, 92,  96, 72, 60,  110, 88,  50,  90, 0,   64),
    P("E.PIANO",      PG_KEYS, ENGINE_FM,    FMM_EPIANO, CAT_MID, 100, 64, 90, 60, 0,  0,  0,  10, 0,   0, 30,  60, 20, 60, 0, 10, 30),
    P("FM BELL",      PG_KEYS, ENGINE_FM,    FMM_BELL,   CAT_TOP, 92,  64, 105,70, 0,  0,  0,  0,  0,   0, 0,   100,10, 80, 0, 20, 60),
    P("MALLET",       PG_KEYS, ENGINE_FM,    FMM_MALLET, CAT_MID, 100, 64, 70, 40, 0,  0,  0,  10, 0,   0, 0,   60, 0,  70, 0, 10, 30),
    P("WARM PAD",     PG_PAD,  ENGINE_SYNTH, SY_POLY,    CAT_MID, 104, 64, 100,10, 60, 10, 10, 0,  10,  95,100, 100,50, 0,  0, 0,  70),
    P("STRINGS",      PG_PAD,  ENGINE_SYNTH, SY_POLY,    CAT_MID, 104, 64, 90, 10, 75, 0,  10, 0,  90,  85,110, 95, 40, 0,  0, 0,  60),
    P("CHORD MEM",    PG_PAD,  ENGINE_SYNTH, SY_CHORD,   CAT_MID, 92,  64, 70, 10, 70, 20, 40, 10, 10,  5, 60,  60, 30, 0,  1, 50, 30),
    /*  name          group    engine        model       cat      lvl tune dec tone cut res env drv wave att sus rel det mod chd sd  sr */
    P("RISER",        PG_FX,   ENGINE_FX,    FXM_RISER,  CAT_TOP, 92,  64, 110,0,  0,  60, 0,  10, 0,   0, 127, 40, 0,  60, 0, 0,  70),
    P("DOWNLIFTER",   PG_FX,   ENGINE_FX,    FXM_DOWN,   CAT_TOP, 92,  64, 100,0,  0,  40, 0,  10, 0,   0, 127, 40, 0,  40, 0, 0,  60),
    P("DUB SIREN",    PG_FX,   ENGINE_FX,    FXM_SIREN,  CAT_MID, 84,  64, 127,50, 0,  0,  0,  20, 0,   0, 127, 50, 0,  90, 0, 100,30),
    P("LASER",        PG_FX,   ENGINE_FX,    FXM_LASER,  CAT_TOP, 92,  64, 66, 0,  0,  0,  0,  10, 0,   0, 0,   50, 0,  0,  0, 60, 20),
    P("SWEEP",        PG_FX,   ENGINE_FX,    FXM_SWEEP,  CAT_TOP, 88,  64, 105,0,  0,  100,0,  0,  0,   0, 127, 50, 0,  0,  0, 0,  50),
    P("IMPACT",       PG_FX,   ENGINE_FX,    FXM_IMPACT, CAT_SUB, 108, 64, 95, 0,  0,  0,  0,  70, 0,   0, 0,   95, 0,  0,  0, 0,  80),
    P("VINYL",        PG_FX,   ENGINE_FX,    FXM_VINYL,  CAT_TOP, 80,  64, 127,40, 0,  0,  0,  0,  0,   0, 127, 60, 0,  0,  0, 0,  0),
};
const uint32_t NPRESETS = sizeof PRESETS / sizeof PRESETS[0];

const char *const PARAM_NAME[NENGINES][NPARAMS] = {
    /* LEVEL PAN TUNE DECAY TONE SNAP CUTOFF RESO ENVMOD DRIVE ACCENT WAVE ATTACK SUSTAIN RELEASE DETUNE MOD CHORD
     * SEND D, SEND R */
    {"LEVEL", "PAN", "TUNE", "DECAY", "TONE", "SNAP", 0, 0, 0, "DRIVE", 0, 0, 0, 0, 0, 0, 0, 0, "SEND D", "SEND R"},
    {"LEVEL", "PAN", "TUNE", "DECAY", 0, "SLOPE", "CUTOFF", "RESO", "ENVMOD", "DRIVE", "ACCENT", "WAVE",
     0, 0, 0, 0, 0, 0, "SEND D", "SEND R"},
    {"LEVEL", "PAN", "TUNE", "DECAY", "FILTER", 0, "CUTOFF", "RESO", "ENVMOD", "DRIVE", 0, "WAVE",
     "ATTACK", "SUSTAIN", "RELEASE", "DETUNE", 0, "CHORD", "SEND D", "SEND R"},
    {"LEVEL", "PAN", "TUNE", "DECAY", "COLOUR", 0, 0, 0, 0, "DRIVE", 0, 0,
     "ATTACK", "SUSTAIN", "RELEASE", "DETUNE", "INDEX", 0, "SEND D", "SEND R"},
    {"LEVEL", "PAN", "TUNE", "LENGTH", "SPEED", 0, 0, "RESO", 0, "DRIVE", 0, "WAVE",
     "ATTACK", "SUSTAIN", "RELEASE", 0, "DEPTH", 0, "SEND D", "SEND R"},
};

int preset_find(const char *name)
{
    uint32_t i, k;
    for (i = 0; i < NPRESETS; i++) {
        for (k = 0; name[k] && PRESETS[i].name[k] == name[k]; k++)
            ;
        if (!name[k] && !PRESETS[i].name[k])
            return (int)i;
    }
    return -1;
}

void preset_apply(track_t *t, uint32_t preset)
{
    const preset_t *p = &PRESETS[preset < NPRESETS ? preset : 0];
    uint32_t i;
    t->preset = (uint8_t)(preset < NPRESETS ? preset : 0);
    t->engine = p->engine;
    t->model = p->model;
    t->category = p->category;
    for (i = 0; i < NPARAMS; i++)
        t->p[i] = p->p[i];
}
