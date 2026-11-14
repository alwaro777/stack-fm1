/* SPDX-License-Identifier: GPL-3.0-or-later
 * STACK — the project in bytes: what goes to the flash (one store object each: the settings, the kit, every pattern)
 * and into a backup file. Versioned, every field checked on the way in (a file from elsewhere is not trusted), every
 * object under the store's 3840 bytes:
 *   settings  "STKS" v1: tempo, swing, seed, the bus, the play mode, the chain, the song, scale, MIDI
 *   kit       "STKK" v1: each track's preset, engine, model, category, MIDI channel, params (with their count, so a
 *             later version with more params still reads an older kit), chord memory
 *   pattern   "STKP" v1: per track its length, scale and direction and a 64-bit map of the steps that differ from the
 *             default, then those steps at 6 bytes each, then the locks at 4 bytes each.
 *             Worst case 7 + 8 * 11 + 512 * 6 + 1 + 128 * 4 = 3680 bytes */
#include "core.h"
#include "engine.h"

#define VER 1u

static void put16(uint8_t *b, uint32_t v) { b[0] = (uint8_t)v; b[1] = (uint8_t)(v >> 8); }
static uint32_t get16(const uint8_t *b) { return (uint32_t)b[0] | (uint32_t)b[1] << 8; }
static int magic(const uint8_t *b, const char *m) { return b[0] == m[0] && b[1] == m[1] && b[2] == m[2] && b[3] == m[3]; }
static uint8_t lim(uint32_t v, uint32_t hi) { return (uint8_t)(v > hi ? hi : v); }

/* ---------------------------------------------------------------- settings --- */
uint32_t proj_encode_settings(uint8_t *b)
{
    uint32_t n = 0, i;
    b[n++] = 'S'; b[n++] = 'T'; b[n++] = 'K'; b[n++] = 'S';
    b[n++] = VER;
    put16(&b[n], g_proj.bpm10);
    n += 2;
    b[n++] = g_proj.swing;
    b[n++] = g_proj.seed;
    b[n++] = NFX;
    for (i = 0; i < NFX; i++)
        b[n++] = g_proj.fx[i];
    b[n++] = g_proj.cur_pat;
    b[n++] = g_proj.play_mode;
    b[n++] = g_proj.chain_len;
    for (i = 0; i < NCHAIN; i++)
        b[n++] = g_proj.chain[i];
    b[n++] = g_proj.song_len;
    for (i = 0; i < NSONG; i++) {
        b[n++] = g_proj.song[i].pat;
        b[n++] = g_proj.song[i].reps;
    }
    b[n++] = g_proj.scale;
    b[n++] = g_proj.tonic;
    b[n++] = g_proj.clk_out;
    b[n++] = g_proj.clk_in;
    b[n++] = g_proj.transport;
    return n;
}

int proj_decode_settings(const uint8_t *b, uint32_t len)
{
    uint32_t n = 5, i, nfx;
    if (len < 10u || !magic(b, "STKS") || b[4] != VER)
        return -1;
    nfx = b[9];
    if (len < 10u + nfx + 3u + NCHAIN + 1u + 2u * NSONG + 5u)
        return -1;
    g_proj.bpm10 = (uint16_t)clampi((int32_t)get16(&b[n]), BPM_MIN, BPM_MAX);
    n += 2;
    g_proj.swing = (uint8_t)clampi(b[n++], 50, 75);
    g_proj.seed = b[n++];
    n++;
    for (i = 0; i < nfx; i++, n++)
        if (i < NFX)
            g_proj.fx[i] = lim(b[n], i == FX_DLY_TIME ? 5u : 127u);
    g_proj.cur_pat = lim(b[n++], NPATTERNS - 1u);
    g_proj.play_mode = lim(b[n++], PLAY_SONG);
    g_proj.chain_len = lim(b[n++], NCHAIN);
    for (i = 0; i < NCHAIN; i++)
        g_proj.chain[i] = lim(b[n++], NPATTERNS - 1u);
    g_proj.song_len = lim(b[n++], NSONG);
    for (i = 0; i < NSONG; i++) {
        g_proj.song[i].pat = lim(b[n++], NPATTERNS - 1u);
        g_proj.song[i].reps = (uint8_t)clampi(b[n++], 1, 16);
    }
    g_proj.scale = lim(b[n++], NSCALE_TYPES - 1u);
    g_proj.tonic = lim(b[n++], 11u);
    g_proj.clk_out = lim(b[n++], 1u);
    g_proj.clk_in = lim(b[n++], 1u);
    g_proj.transport = lim(b[n++], 3u);
    return 0;
}

/* --------------------------------------------------------------------- kit --- */
uint32_t proj_encode_kit(uint8_t *b)
{
    uint32_t n = 0, t, i;
    b[n++] = 'S'; b[n++] = 'T'; b[n++] = 'K'; b[n++] = 'K';
    b[n++] = VER;
    b[n++] = NTRACKS;
    b[n++] = NPARAMS;
    for (t = 0; t < NTRACKS; t++) {
        const track_t *tr = &g_proj.pat.trk[t];
        b[n++] = tr->preset;
        b[n++] = tr->engine;
        b[n++] = tr->model;
        b[n++] = tr->category;
        b[n++] = tr->midi_ch;
        for (i = 0; i < NPARAMS; i++)
            b[n++] = tr->p[i];
        for (i = 0; i < 4; i++)
            b[n++] = (uint8_t)tr->chord[i];
    }
    return n;
}

int proj_decode_kit(const uint8_t *b, uint32_t len)
{
    uint32_t n = 7, t, i, np;
    static const uint8_t NMODELS[NENGINES] = {NDMODELS, 1, NSYMODELS, NFMMODELS, NFXMODELS};
    if (len < 7u || !magic(b, "STKK") || b[4] != VER || b[5] != NTRACKS)
        return -1;
    np = b[6];
    if (len < 7u + NTRACKS * (5u + np + 4u))
        return -1;
    for (t = 0; t < NTRACKS; t++) {
        track_t *tr = &g_proj.pat.trk[t];
        uint32_t eng = b[n + 1];
        if (eng >= NENGINES)
            return -1;
        tr->preset = lim(b[n], NPRESETS - 1u);
        tr->engine = (uint8_t)eng;
        tr->model = lim(b[n + 2], NMODELS[eng] - 1u);
        tr->category = lim(b[n + 3], NCATS - 1u);
        tr->midi_ch = lim(b[n + 4], 16u);
        n += 5;
        for (i = 0; i < np; i++, n++)
            if (i < NPARAMS)
                tr->p[i] = lim(b[n], i == PR_CHORD ? NCHORDS - 1u : 127u);
        for (i = 0; i < 4; i++, n++) {
            int8_t c = (int8_t)b[n];
            tr->chord[i] = c == -128 || (c >= -24 && c <= 36) ? c : -128;
        }
    }
    return 0;
}

/* ----------------------------------------------------------------- patterns --- */
static int step_plain(const step_t *s)
{
    step_t d;
    step_default(&d);
    return s->flags == d.flags && s->note == d.note && s->vel == d.vel && s->gate == d.gate && s->prob == d.prob &&
           s->ratchet == d.ratchet && s->micro == d.micro && s->cond == d.cond;
}

/* 6 bytes: flags (3) | ratchet-1 (2) | cond low (3); note | cond high << 7; vel; gate; prob; micro + 11 */
static void step_pack(const step_t *s, uint8_t *b)
{
    b[0] = (uint8_t)((s->flags & 7u) | (((uint32_t)clampi(s->ratchet, 1, 4) - 1u) << 3) | ((s->cond & 7u) << 5));
    b[1] = (uint8_t)((s->note & 0x7Fu) | ((s->cond >> 3) & 1u) << 7);
    b[2] = s->vel;
    b[3] = s->gate;
    b[4] = s->prob;
    b[5] = (uint8_t)(clampi(s->micro, -11, 11) + 11);
}

static void step_unpack(step_t *s, const uint8_t *b)
{
    s->flags = b[0] & 7u;
    s->ratchet = (uint8_t)(((b[0] >> 3) & 3u) + 1u);
    s->cond = (uint8_t)(((b[0] >> 5) & 7u) | (uint32_t)(b[1] >> 7) << 3);
    if (s->cond >= NCONDS)
        s->cond = COND_ALWAYS;
    s->note = b[1] & 0x7Fu;
    s->vel = (uint8_t)clampi(b[2], 1, 127);
    s->gate = (uint8_t)clampi(b[3], 1, 64);
    s->prob = lim(b[4], 100u);
    s->micro = (int8_t)(clampi(b[5], 0, 22) - 11);
}

uint32_t proj_encode_pattern(uint32_t p, uint8_t *b)
{
    const bank_t *bk = &g_bank[p & 15u];
    uint32_t n = 0, t, i, nl = 0, nlpos;
    b[n++] = 'S'; b[n++] = 'T'; b[n++] = 'K'; b[n++] = 'P';
    b[n++] = VER;
    b[n++] = (uint8_t)(p & 15u);
    b[n++] = NTRACKS;
    for (t = 0; t < NTRACKS; t++) {
        uint32_t k;
        b[n++] = bk->len[t];
        b[n++] = bk->scale[t];
        b[n++] = bk->dir[t];
        for (k = 0; k < 8; k++) {
            uint8_t m = 0;
            for (i = 0; i < 8; i++)
                if (!step_plain(&bk->step[t][k * 8 + i]))
                    m |= (uint8_t)(1u << i);
            b[n++] = m;
        }
    }
    for (t = 0; t < NTRACKS; t++)
        for (i = 0; i < NSTEPS; i++)
            if (!step_plain(&bk->step[t][i])) {
                step_pack(&bk->step[t][i], &b[n]);
                n += 6;
            }
    nlpos = n++;
    for (i = 0; i < NLOCKS; i++)
        if (bk->lock[i].trk != LOCK_NONE) {
            b[n++] = bk->lock[i].trk;
            b[n++] = bk->lock[i].step;
            b[n++] = bk->lock[i].param;
            b[n++] = bk->lock[i].value;
            nl++;
        }
    b[nlpos] = (uint8_t)nl;
    return n;
}

int proj_decode_pattern(uint32_t p, const uint8_t *b, uint32_t len)
{
    bank_t *bk = &g_bank[p & 15u];
    uint32_t n = 7, t, i, k, nl, head = 7u + NTRACKS * 11u;
    uint8_t map[NTRACKS][8];
    if (len < head + 1u || !magic(b, "STKP") || b[4] != VER || b[6] != NTRACKS)
        return -1;
    bank_clear(bk);
    for (t = 0; t < NTRACKS; t++) {
        bk->len[t] = (uint8_t)clampi(b[n], 1, NSTEPS);
        bk->scale[t] = lim(b[n + 1], NSCALES - 1u);
        bk->dir[t] = lim(b[n + 2], NDIRS - 1u);
        n += 3;
        for (k = 0; k < 8; k++)
            map[t][k] = b[n++];
    }
    for (t = 0; t < NTRACKS; t++)
        for (i = 0; i < NSTEPS; i++)
            if ((map[t][i >> 3] >> (i & 7u)) & 1u) {
                if (n + 6u > len)
                    return -1;
                step_unpack(&bk->step[t][i], &b[n]);
                n += 6;
            }
    if (n + 1u > len)
        return -1;
    nl = b[n++];
    if (nl > NLOCKS || n + nl * 4u > len)
        return -1;
    for (i = 0; i < nl; i++, n += 4) {
        if (b[n] >= NTRACKS || b[n + 1] >= NSTEPS || b[n + 2] >= NPARAMS)
            continue;
        bk->lock[i].trk = b[n];
        bk->lock[i].step = b[n + 1];
        bk->lock[i].param = b[n + 2];
        bk->lock[i].value = lim(b[n + 3], 127u);
    }
    return 0;
}
