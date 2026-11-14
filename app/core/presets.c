/* SPDX-License-Identifier: GPL-3.0-or-later
 * STACK — factory presets (Phase 2: drums and the acid bass; Phase 4 adds leads, keys, pads, FX and FM) and the
 * engines' parameter names. Param order: LEVEL PAN TUNE DECAY TONE SNAP CUTOFF RESO ENVMOD DRIVE ACCENT WAVE. */
#include "engine.h"

#define D(name, model, cat, lvl, tune, decay, tone, snap, drive) \
    {name, ENGINE_DRUM, model, cat, {lvl, 64, tune, decay, tone, snap, 0, 0, 0, drive, 0, 0}}
#define A(name, cat, lvl, decay, cutoff, reso, envmod, drive, accent, wave, slope) \
    {name, ENGINE_ACID, 0, cat, {lvl, 64, 64, decay, 0, slope, cutoff, reso, envmod, drive, accent, wave}}

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
    /*  name          cat      lvl dec  cut  res  env  drive acc wave slope */
    A("ACID SAW",     CAT_SUB, 100, 76, 52,  100, 92,  30,   90,  0,  0),
    A("ACID SQUARE",  CAT_SUB, 100, 70, 48,  104, 96,  40,   90,  60, 0),
    A("SUB BASS",     CAT_SUB, 112, 40, 110, 0,   0,   10,   0,   110, 0),
};
const uint32_t NPRESETS = sizeof PRESETS / sizeof PRESETS[0];

const char *const PARAM_NAME[NENGINES][NPARAMS] = {
    /* LEVEL    PAN    TUNE    DECAY    TONE    SNAP    CUTOFF  RESO  ENVMOD  DRIVE    ACCENT    WAVE */
    {"LEVEL", "PAN", "TUNE", "DECAY", "TONE", "SNAP", 0, 0, 0, "DRIVE", 0, 0},
    {"LEVEL", "PAN", "TUNE", "DECAY", 0, "SLOPE", "CUTOFF", "RESO", "ENVMOD", "DRIVE", "ACCENT", "WAVE"},
};

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
