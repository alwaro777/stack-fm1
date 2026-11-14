/* SPDX-License-Identifier: GPL-3.0-or-later
 * STACK — the unit tests' harness: checks, golden values, PNG snapshots of the framebuffer, WAV files. */
#ifndef STK_HARNESS_H
#define STK_HARNESS_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../app/ui/fb.h"
#include "png.h"

static int fails, checks;
static const char *golden_dir, *out_dir;
static int record;

#define CHECK(cond, ...)                                                                    \
    do {                                                                                    \
        checks++;                                                                           \
        if (!(cond)) {                                                                      \
            fails++;                                                                        \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                     \
            printf(__VA_ARGS__);                                                            \
            printf("\n");                                                                   \
        }                                                                                   \
    } while (0)

/* golden values: "name value" lines in GOLDEN_DIR/golden.txt; a missing one is recorded, --record rewrites */
#define MAXG 128
static char gname[MAXG][48];
static uint32_t gval[MAXG];
static int gn, gdirty;

static void golden_load(void)
{
    char path[512], name[48];
    unsigned v;
    FILE *f;
    snprintf(path, sizeof path, "%s/golden.txt", golden_dir);
    if (!(f = fopen(path, "r")))
        return;
    while (gn < MAXG && fscanf(f, "%47s %x", name, &v) == 2) {
        strcpy(gname[gn], name);
        gval[gn++] = v;
    }
    fclose(f);
}

static void golden_save(void)
{
    char path[512];
    FILE *f;
    int i;
    if (!gdirty)
        return;
    snprintf(path, sizeof path, "%s/golden.txt", golden_dir);
    if (!(f = fopen(path, "w")))
        return;
    for (i = 0; i < gn; i++)
        fprintf(f, "%s %08x\n", gname[i], gval[i]);
    fclose(f);
}

static void golden(const char *name, uint32_t v)
{
    int i;
    for (i = 0; i < gn; i++)
        if (!strcmp(gname[i], name)) {
            if (record && gval[i] != v) {
                gval[i] = v;
                gdirty = 1;
                printf("golden %s re-recorded %08x\n", name, v);
            }
            CHECK(gval[i] == v, "golden %s: got %08x, expected %08x", name, v, gval[i]);
            return;
        }
    if (gn < MAXG) {
        strcpy(gname[gn], name);
        gval[gn++] = v;
        gdirty = 1;
        printf("golden %s recorded %08x\n", name, v);
    }
}

static void snapshot(const char *name)
{
    static uint8_t rgb[FB_W * FB_H * 3];
    char path[512];
    int32_t x, y;
    for (y = 0; y < FB_H; y++)
        for (x = 0; x < FB_W; x++)
            memcpy(&rgb[(y * FB_W + x) * 3], fb_palette_rgb[fb_get(x, y)], 3);
    snprintf(path, sizeof path, "%s/%s.png", out_dir, name);
    png_write_rgb(path, rgb, FB_W, FB_H);
    golden(name, fb_hash());
}

static uint32_t hash32(const int32_t *p, uint32_t n)
{
    uint32_t h = 2166136261u, i;
    for (i = 0; i < n; i++) {
        uint32_t v = (uint32_t)p[i];
        h = (h ^ (v & 0xFF)) * 16777619u;
        h = (h ^ (v >> 8 & 0xFF)) * 16777619u;
        h = (h ^ (v >> 16 & 0xFF)) * 16777619u;
        h = (h ^ (v >> 24)) * 16777619u;
    }
    return h;
}

/* stereo Q23 frames to a 16-bit WAV, to listen to */
static void wav_write(const char *name, const int32_t *lr, uint32_t frames)
{
    char path[512];
    FILE *f;
    uint32_t i, bytes = frames * 4u;
    uint8_t h[44] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 2, 0,
                     0x44, 0xAC, 0, 0, 0x10, 0xB1, 2, 0, 4, 0, 16, 0, 'd', 'a', 't', 'a', 0, 0, 0, 0};
    snprintf(path, sizeof path, "%s/%s.wav", out_dir, name);
    if (!(f = fopen(path, "wb")))
        return;
    h[4] = (uint8_t)(bytes + 36);
    h[5] = (uint8_t)((bytes + 36) >> 8);
    h[6] = (uint8_t)((bytes + 36) >> 16);
    h[7] = (uint8_t)((bytes + 36) >> 24);
    h[40] = (uint8_t)bytes;
    h[41] = (uint8_t)(bytes >> 8);
    h[42] = (uint8_t)(bytes >> 16);
    h[43] = (uint8_t)(bytes >> 24);
    fwrite(h, 1, 44, f);
    for (i = 0; i < frames * 2u; i++) {
        int16_t s = (int16_t)(lr[i] >> 8);
        fwrite(&s, 2, 1, f);
    }
    fclose(f);
}

#endif
