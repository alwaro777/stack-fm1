/* SPDX-License-Identifier: GPL-3.0-or-later
 * STACK — the demos, built in: RAVE 138 (the one the FM-1 starts with), DUB 140, GABBER 180. Each also says in what
 * order its tracks come in, for the "build it up" mode (SHIFT+PLAY), where the sound system grows a layer every two
 * bars. Original material: patterns and sounds made for STACK. */
#include "core.h"
#include "engine.h"

const char *const DEMO_NAME[NDEMOS] = {"RAVE 138", "DUB 140", "GABBER 180"};
static const uint8_t DEMO_ORDER[NDEMOS][NTRACKS] = {
    {0, 2, 1, 6, 3, 4, 5, 7},    /* kick, hats, clap, acid, open hats, rim, stab */
    {0, 7, 6, 1, 2, 5, 4, 3},    /* kick, vinyl, sub, rim, shaker, chords, clap, siren */
    {0, 2, 1, 6, 3, 5, 4, 7},    /* gabber, hats, clap, acid, hoover, stab, ride */
};

uint32_t demo_order(uint32_t d, uint8_t order[NTRACKS])
{
    uint32_t i;
    for (i = 0; i < NTRACKS; i++)
        order[i] = DEMO_ORDER[d % NDEMOS][i];
    return NTRACKS;
}

static void on(uint32_t ti, uint32_t s, uint32_t note, uint32_t vel, uint32_t flags)
{
    step_t *st = &g_proj.pat.trk[ti].step[s];
    st->flags = (uint8_t)(STEP_ON | flags);
    st->note = (uint8_t)note;
    st->vel = (uint8_t)vel;
}

static void demo_rave(void)
{
    /* an A-minor acid line (0 = rest), with accents and slides */
    static const uint8_t ACID[16] = {45, 45, 0, 57, 48, 45, 55, 57, 45, 0, 60, 52, 45, 57, 43, 47};
    static const uint8_t ACID_F[16] = {0, STEP_ACCENT, 0, 0, STEP_SLIDE, STEP_SLIDE, 0, STEP_ACCENT,
                                       0, 0, 0, STEP_ACCENT, STEP_SLIDE, 0, 0, STEP_ACCENT};
    uint32_t i;
    core_clear_project();
    g_proj.swing = 54;
    for (i = 0; i < 16; i += 4)
        on(0, i, 60, 120, 0);                     /* kick: four to the floor */
    on(0, 14, 60, 100, 0);                        /* and a roll into the bar's end, on FILL only */
    g_proj.pat.trk[0].step[14].ratchet = 2;
    g_proj.pat.trk[0].step[14].cond = COND_FILL;
    on(1, 4, 60, 110, 0);                         /* clap on 2 and 4 */
    on(1, 12, 60, 115, 0);
    for (i = 0; i < 16; i++)                      /* closed hats on the 16ths but the off-beats */
        if ((i & 3u) != 2u)
            on(2, i, 60, (i & 1u) ? 64 : 96, 0);
    for (i = 2; i < 16; i += 4)                   /* open hats on the off-beats, choked by the next closed one */
        on(3, i, 60, 100, 0);
    on(4, 7, 60, 90, 0);                          /* rim, the last one on probability */
    on(4, 13, 60, 80, 0);
    on(4, 15, 60, 70, 0);
    g_proj.pat.trk[4].step[15].prob = 50;
    for (i = 0; i < 16; i++)
        if (ACID[i])
            on(6, i, ACID[i], ACID_F[i] & STEP_ACCENT ? 120 : 96, ACID_F[i]);
    /* Phase 4: a minor-seventh rave stab on track 6 into the dub delay, the clap into the reverb, the rim into the
     * delay, and a little pump from the kick */
    preset_apply(&g_proj.pat.trk[5], (uint32_t)preset_find("RAVE STAB"));
    on(5, 0, 57, 110, 0);
    on(5, 10, 57, 96, 0);
    g_proj.pat.trk[5].step[10].cond = COND_1_2;
    g_proj.pat.trk[5].p[PR_SEND_D] = 70;
    g_proj.pat.trk[1].p[PR_SEND_R] = 50;
    g_proj.pat.trk[4].p[PR_SEND_D] = 80;
    g_proj.fx[FX_PUMP] = 50;
    /* pattern 2, the breakdown: no kick, the stab on every bar and the acid an octave up with a p-locked filter
     * climbing; then the chain P1 P1 P1 P2 (play mode CHAIN to hear it) */
    {
        bank_t *b = &g_bank[1];
        uint32_t k;
        for (k = 0; k < 16; k++) {
            b->step[2][k] = g_proj.pat.trk[2].step[k];
            b->step[6][k] = g_proj.pat.trk[6].step[k];
            if (b->step[6][k].flags & STEP_ON)
                b->step[6][k].note = (uint8_t)(b->step[6][k].note + 12);
        }
        b->step[1][12] = g_proj.pat.trk[1].step[12];
        b->step[5][0] = g_proj.pat.trk[5].step[0];
        b->step[5][8] = g_proj.pat.trk[5].step[0];
        for (k = 0; k < 4; k++) {
            b->lock[k].trk = 6;
            b->lock[k].step = (uint8_t)(k * 4u);
            b->lock[k].param = PR_CUTOFF;
            b->lock[k].value = (uint8_t)(40 + k * 18);
        }
    }
    g_proj.chain[0] = g_proj.chain[1] = g_proj.chain[2] = 0;
    g_proj.chain[3] = 1;
    g_proj.chain_len = 4;
    g_proj.song[0].pat = 0;
    g_proj.song[0].reps = 3;
    g_proj.song[1].pat = 1;
    g_proj.song[1].reps = 1;
    g_proj.song_len = 2;
}


static void kit(uint32_t ti, const char *name)
{
    int p = preset_find(name);
    preset_apply(&g_proj.pat.trk[ti], (uint32_t)(p < 0 ? 0 : p));
}

/* DUB 140: half-time, the space between the hits is the point; everything goes through the delay */
static void demo_dub(void)
{
    static const uint8_t SUB[16] = {45, 0, 0, 0, 0, 0, 45, 0, 48, 0, 0, 0, 43, 0, 0, 0};
    uint32_t i;
    core_clear_project();
    g_proj.bpm10 = 1400;
    g_proj.swing = 58;
    kit(0, "808 BOOM");
    kit(1, "RIM");
    kit(2, "SHAKER");
    kit(3, "DUB SIREN");
    kit(4, "CLAP");
    kit(5, "RAVE STAB");
    kit(6, "SUB BASS");
    kit(7, "VINYL");
    on(0, 0, 60, 120, 0);
    on(0, 10, 60, 100, 0);
    on(1, 8, 60, 110, 0);
    g_proj.pat.trk[1].p[PR_SEND_D] = 110;
    for (i = 0; i < 16; i++)
        on(2, i, 60, (i & 1u) ? 50 : 80, 0);
    g_proj.pat.trk[2].p[PR_LEVEL] = 80;
    on(3, 0, 64, 100, 0);                         /* the siren: one bar in four */
    g_proj.pat.trk[3].step[0].gate = 48;
    g_proj.pat.trk[3].step[0].cond = COND_4_4;
    on(4, 8, 60, 90, 0);
    g_proj.pat.trk[4].p[PR_SEND_R] = 90;
    g_proj.pat.trk[5].p[PR_CHORD] = 3;            /* MIN */
    g_proj.pat.trk[5].p[PR_SEND_D] = 100;
    for (i = 2; i < 16; i += 4)
        on(5, i, 57, 90, 0);
    for (i = 0; i < 16; i++)
        if (SUB[i])
            on(6, i, SUB[i], 110, 0);
    on(7, 0, 60, 100, 0);
    g_proj.pat.trk[7].step[0].gate = 64;
    g_proj.pat.trk[7].len = 4;                    /* (four steps: the crackle never stops) */
    g_proj.fx[FX_DLY_TIME] = 2;
    g_proj.fx[FX_DLY_FB] = 92;
    g_proj.fx[FX_DLY_TONE] = 60;
    g_proj.fx[FX_DLY_LEVEL] = 110;
    g_proj.fx[FX_REV_SIZE] = 100;
}

/* GABBER 180: distorted kicks on every beat, a hoover, stabs; the pump and the compressor working hard */
static void demo_gabber(void)
{
    static const uint8_t ACID[16] = {38, 38, 50, 38, 41, 38, 50, 43, 38, 38, 50, 38, 45, 43, 41, 36};
    uint32_t i;
    core_clear_project();
    g_proj.bpm10 = 1800;
    kit(0, "GABBER");
    kit(1, "CLAP");
    kit(2, "CL HAT");
    kit(3, "HOOVER");
    kit(4, "RIDE");
    kit(5, "RAVE STAB");
    kit(6, "ACID SQUARE");
    kit(7, "CRASH");
    for (i = 0; i < 16; i += 4)
        on(0, i, 60, 127, 0);
    on(1, 4, 60, 110, 0);
    on(1, 12, 60, 110, 0);
    for (i = 2; i < 16; i += 4)
        on(2, i, 60, 100, 0);
    on(3, 0, 50, 110, STEP_SLIDE);                /* the hoover: a sliding phrase */
    on(3, 6, 53, 110, STEP_SLIDE);
    on(3, 8, 48, 110, 0);
    g_proj.pat.trk[3].step[8].gate = 48;
    for (i = 0; i < 16; i += 2)
        on(4, i, 60, (i & 2u) ? 70 : 90, 0);
    g_proj.pat.trk[5].p[PR_CHORD] = 3;            /* MIN */
    on(5, 0, 62, 110, 0);
    on(5, 3, 62, 100, 0);
    on(5, 6, 65, 100, 0);
    for (i = 0; i < 16; i++)
        on(6, i, ACID[i], (i % 3u) ? 90 : 120, (i % 3u) ? 0 : STEP_ACCENT);
    on(7, 0, 60, 100, 0);
    g_proj.pat.trk[7].step[0].cond = COND_1_4;
    g_proj.fx[FX_PUMP] = 80;
    g_proj.fx[FX_COMP] = 70;
}

void core_load_demo_n(uint32_t d)
{
    switch (d % NDEMOS) {
    case 1: demo_dub(); break;
    case 2: demo_gabber(); break;
    default: demo_rave(); break;
    }
}

void core_load_demo(void)
{
    demo_rave();
}
