/* SPDX-License-Identifier: GPL-3.0-or-later
 * STACK — 4 bpp framebuffer, drawing primitives and the pixel font. Integer only. */
#include "fb.h"

const uint8_t fb_palette_rgb[P_NCOLORS][3] = {
    {0, 0, 0},       /* P_BLACK   */
    {18, 14, 30},    /* P_NIGHT   the stage's sky */
    {54, 50, 64},    /* P_DGRAY   */
    {112, 106, 122}, /* P_GRAY    */
    {182, 178, 192}, /* P_LGRAY   */
    {250, 246, 236}, /* P_WHITE   */
    {255, 98, 18},   /* P_ORANGE  the cabinets */
    {168, 52, 10},   /* P_DORANGE their shade */
    {255, 164, 76},  /* P_LORANGE their light */
    {255, 232, 40},  /* P_YELLOW  acid accents */
    {150, 255, 40},  /* P_LIME    */
    {255, 40, 184},  /* P_MAGENTA */
    {40, 220, 255},  /* P_CYAN    */
    {232, 30, 40},   /* P_RED     record */
    {78, 26, 128},   /* P_PURPLE  */
    {132, 92, 56},   /* P_WOOD    the stage floor */
};

uint8_t fb_px[FB_H * FB_STRIDE];
static uint32_t band_sent[FB_NBANDS];      /* hash of each band as last sent */
static uint32_t band_ok;                   /* bit b: band_sent[b] holds what the panel shows (0 after invalidate) */

static inline void fb_put(int32_t x, int32_t y, uint8_t c)
{
    uint8_t *p = &fb_px[y * FB_STRIDE + (x >> 1)];
    *p = (x & 1) ? (uint8_t)((*p & 0xF0u) | (c & 15u)) : (uint8_t)((*p & 0x0Fu) | (uint8_t)(c << 4));
}

void fb_pixel(int32_t x, int32_t y, uint8_t c)
{
    if ((uint32_t)x < FB_W && (uint32_t)y < FB_H)
        fb_put(x, y, c);
}

uint8_t fb_get(int32_t x, int32_t y)
{
    uint8_t b;
    if ((uint32_t)x >= FB_W || (uint32_t)y >= FB_H)
        return 0;
    b = fb_px[y * FB_STRIDE + (x >> 1)];
    return (x & 1) ? (uint8_t)(b & 15u) : (uint8_t)(b >> 4);
}

void fb_clear(uint8_t c)
{
    uint32_t i;
    uint8_t b = (uint8_t)((c & 15u) << 4 | (c & 15u));
    for (i = 0; i < sizeof fb_px; i++)
        fb_px[i] = b;
}

/* clip a rectangle to the screen; 0 if nothing is left */
static int fb_clip(int32_t *x, int32_t *y, int32_t *w, int32_t *h)
{
    if (*x < 0) { *w += *x; *x = 0; }
    if (*y < 0) { *h += *y; *y = 0; }
    if (*x + *w > FB_W) *w = FB_W - *x;
    if (*y + *h > FB_H) *h = FB_H - *y;
    return *w > 0 && *h > 0;
}

void fb_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint8_t c)
{
    int32_t j;
    uint8_t b = (uint8_t)((c & 15u) << 4 | (c & 15u));
    if (!fb_clip(&x, &y, &w, &h))
        return;
    for (j = y; j < y + h; j++) {         /* an odd first pixel, whole bytes, an odd last pixel */
        int32_t x0 = x, x1 = x + w;
        uint8_t *row = &fb_px[j * FB_STRIDE];
        if (x0 & 1)
            fb_put(x0++, j, c);
        if (x1 & 1)
            fb_put(--x1, j, c);
        for (; x0 < x1; x0 += 2)
            row[x0 >> 1] = b;
    }
}

void fb_hline(int32_t x, int32_t y, int32_t w, uint8_t c) { fb_rect(x, y, w, 1, c); }
void fb_vline(int32_t x, int32_t y, int32_t h, uint8_t c) { fb_rect(x, y, 1, h, c); }

void fb_frame(int32_t x, int32_t y, int32_t w, int32_t h, uint8_t c)
{
    fb_hline(x, y, w, c);
    fb_hline(x, y + h - 1, w, c);
    fb_vline(x, y, h, c);
    fb_vline(x + w - 1, y, h, c);
}

static const uint8_t FB_BAYER4[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};

void fb_dither(int32_t x, int32_t y, int32_t w, int32_t h, uint8_t a, uint8_t b, uint32_t level)
{
    int32_t i, j;
    if (!fb_clip(&x, &y, &w, &h))
        return;
    for (j = y; j < y + h; j++)
        for (i = x; i < x + w; i++)   /* by screen position, so neighbouring areas tile seamlessly */
            fb_put(i, j, FB_BAYER4[(j & 3) * 4 + (i & 3)] < level ? b : a);
}

void fb_vgrad(int32_t x, int32_t y, int32_t w, int32_t h, uint8_t a, uint8_t b)
{
    int32_t j;
    for (j = 0; j < h; j++)
        fb_dither(x, y + j, w, 1, a, b, (uint32_t)((j * 17) / (h > 1 ? h : 1)));
}

void fb_disc(int32_t cx, int32_t cy, int32_t r, uint8_t c)
{
    int32_t dy, dx;
    for (dy = -r; dy <= r; dy++)
        for (dx = -r; dx <= r; dx++)
            if (dx * dx + dy * dy <= r * r + r)   /* +r: rounder small circles */
                fb_pixel(cx + dx, cy + dy, c);
}

void fb_ring(int32_t cx, int32_t cy, int32_t r, uint8_t c)
{
    int32_t dy, dx, lo = (r - 1) * (r - 1) + (r - 1), hi = r * r + r;
    for (dy = -r; dy <= r; dy++)
        for (dx = -r; dx <= r; dx++) {
            int32_t d = dx * dx + dy * dy;
            if (d <= hi && d > lo)
                fb_pixel(cx + dx, cy + dy, c);
        }
}

void fb_stripes(int32_t x, int32_t y, int32_t w, int32_t h, uint8_t a, uint8_t b, int32_t period)
{
    int32_t i, j;
    if (!fb_clip(&x, &y, &w, &h) || period < 2)
        return;
    for (j = y; j < y + h; j++)
        for (i = x; i < x + w; i++)
            fb_put(i, j, ((i + j) % period) < period / 2 ? a : b);
}

/* ------------------------------------------------------------------- font --- */
/* STACK's own 5x7 pixel font, ' ' (32) .. '_' (95). One byte per row, bit 4 = leftmost column. */
static const uint8_t STK_FONT[64][7] = {
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, /*   */ {0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04}, /* ! */
    {0x0A, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00}, /* " */ {0x0A, 0x0A, 0x1F, 0x0A, 0x1F, 0x0A, 0x0A}, /* # */
    {0x04, 0x0F, 0x14, 0x0E, 0x05, 0x1E, 0x04}, /* $ */ {0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03}, /* % */
    {0x0C, 0x12, 0x14, 0x08, 0x15, 0x12, 0x0D}, /* & */ {0x04, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00}, /* ' */
    {0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02}, /* ( */ {0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08}, /* ) */
    {0x00, 0x04, 0x15, 0x0E, 0x15, 0x04, 0x00}, /* * */ {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00}, /* + */
    {0x00, 0x00, 0x00, 0x00, 0x0C, 0x04, 0x08}, /* , */ {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}, /* - */
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C}, /* . */ {0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10}, /* / */
    {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}, /* 0 */ {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}, /* 1 */
    {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}, /* 2 */ {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E}, /* 3 */
    {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}, /* 4 */ {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}, /* 5 */
    {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}, /* 6 */ {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}, /* 7 */
    {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}, /* 8 */ {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}, /* 9 */
    {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00}, /* : */ {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x04, 0x08}, /* ; */
    {0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02}, /* < */ {0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00}, /* = */
    {0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08}, /* > */ {0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04}, /* ? */
    {0x0E, 0x11, 0x01, 0x0D, 0x15, 0x15, 0x0E}, /* @ */ {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}, /* A */
    {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}, /* B */ {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}, /* C */
    {0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C}, /* D */ {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}, /* E */
    {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}, /* F */ {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}, /* G */
    {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}, /* H */ {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}, /* I */
    {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C}, /* J */ {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}, /* K */
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}, /* L */ {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}, /* M */
    {0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11}, /* N */ {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, /* O */
    {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}, /* P */ {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}, /* Q */
    {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}, /* R */ {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}, /* S */
    {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}, /* T */ {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, /* U */
    {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}, /* V */ {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A}, /* W */
    {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}, /* X */ {0x11, 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04}, /* Y */
    {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}, /* Z */ {0x0E, 0x08, 0x08, 0x08, 0x08, 0x08, 0x0E}, /* [ */
    {0x10, 0x10, 0x08, 0x04, 0x02, 0x01, 0x01}, /* \ */ {0x0E, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0E}, /* ] */
    {0x04, 0x0A, 0x11, 0x00, 0x00, 0x00, 0x00}, /* ^ */ {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F}, /* _ */
};

static const uint8_t *fb_glyph(char ch)
{
    uint32_t c = (uint8_t)ch;
    if (c >= 'a' && c <= 'z')
        c -= 32u;
    if (c < 32u || c > 95u)
        c = '?';
    return STK_FONT[c - 32u];
}

int32_t fb_text_w(const char *s, int32_t scale)
{
    int32_t n = 0;
    while (s[n])
        n++;
    return n ? n * (FB_FONT_W + 1) * scale - scale : 0;
}

int32_t fb_text(int32_t x, int32_t y, const char *s, uint8_t c, int32_t scale)
{
    for (; *s; s++) {
        const uint8_t *g = fb_glyph(*s);
        int32_t r, col;
        for (r = 0; r < FB_FONT_H; r++)
            for (col = 0; col < FB_FONT_W; col++)
                if (g[r] & (0x10u >> col))
                    fb_rect(x + col * scale, y + r * scale, scale, scale, c);
        x += (FB_FONT_W + 1) * scale;
    }
    return x;
}

int32_t fb_text_shadow(int32_t x, int32_t y, const char *s, uint8_t c, uint8_t sh, int32_t scale, int32_t d)
{
    fb_text(x + d, y + d, s, sh, scale);
    return fb_text(x, y, s, c, scale);
}

void fb_text_center(int32_t y, const char *s, uint8_t c, int32_t scale)
{
    fb_text((FB_W - fb_text_w(s, scale)) / 2, y, s, c, scale);
}

/* --------------------------------------------------------------- flushing --- */
static uint32_t band_hash(uint32_t b)
{
    uint32_t h = 2166136261u, i;
    const uint8_t *p = &fb_px[b * FB_BAND * FB_STRIDE];
    for (i = 0; i < (uint32_t)(FB_BAND * FB_STRIDE); i++)
        h = (h ^ p[i]) * 16777619u;
    return h;
}

uint32_t fb_changed_bands(void)
{
    uint32_t b, m = 0;
    for (b = 0; b < FB_NBANDS; b++)
        if (!(band_ok >> b & 1u) || band_hash(b) != band_sent[b])
            m |= 1u << b;
    return m;
}

void fb_mark_sent(uint32_t bands)
{
    uint32_t b;
    for (b = 0; b < FB_NBANDS; b++)
        if (bands >> b & 1u) {
            band_sent[b] = band_hash(b);
            band_ok |= 1u << b;
        }
}

void fb_invalidate(void) { band_ok = 0; }

uint32_t fb_hash(void)
{
    uint32_t h = 2166136261u, i;
    for (i = 0; i < sizeof fb_px; i++)
        h = (h ^ fb_px[i]) * 16777619u;
    return h;
}
