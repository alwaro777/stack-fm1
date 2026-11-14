/* SPDX-License-Identifier: GPL-3.0-or-later
 * STACK — the UI's framebuffer: 240x240, 4 bits per pixel (16-colour palette), 28.1 KiB.
 * Everything the UI draws lands here; the platform side (app.c) sends only the 16-row bands whose content changed
 * since the last flush, converted to the panel's RGB565. Tests read the same buffer (snapshots by hash). */
#ifndef STK_FB_H
#define STK_FB_H
#include <stdint.h>

#define FB_W 240
#define FB_H 240
#define FB_BAND 16                         /* rows per band */
#define FB_NBANDS (FB_H / FB_BAND)         /* 15 */
#define FB_STRIDE (FB_W / 2)               /* bytes per row: two pixels a byte, left pixel in the high nibble */
#define FB_TILE 16                         /* change tracking: tiles of 16 x 16 */
#define FB_NTILES (FB_W / FB_TILE)         /* 15 per band */

/* where the big buffers live: the platform glue puts them in its large RAM pool (fm1-base PLAT_POOL) */
#ifndef STK_BIG
#define STK_BIG
#endif

/* the palette: a rave flyer's limited set. Indices are what the UI draws with. */
enum {
    P_BLACK, P_NIGHT, P_DGRAY, P_GRAY, P_LGRAY, P_WHITE,
    P_ORANGE, P_DORANGE, P_LORANGE, P_YELLOW, P_LIME, P_MAGENTA,
    P_CYAN, P_RED, P_PURPLE, P_WOOD, P_NCOLORS
};
extern const uint8_t fb_palette_rgb[P_NCOLORS][3];

extern uint8_t fb_px[FB_H * FB_STRIDE];

void fb_clear(uint8_t c);
void fb_pixel(int32_t x, int32_t y, uint8_t c);
uint8_t fb_get(int32_t x, int32_t y);
void fb_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint8_t c);           /* filled */
void fb_frame(int32_t x, int32_t y, int32_t w, int32_t h, uint8_t c);          /* outline, 1 px */
void fb_hline(int32_t x, int32_t y, int32_t w, uint8_t c);
void fb_vline(int32_t x, int32_t y, int32_t h, uint8_t c);
/* ordered (Bayer 4x4) dither between two colours: level 0 = all a, 16 = all b */
void fb_dither(int32_t x, int32_t y, int32_t w, int32_t h, uint8_t a, uint8_t b, uint32_t level);
/* the same pattern over what is there: level of 16 pixels become c, the rest stay */
void fb_shade(int32_t x, int32_t y, int32_t w, int32_t h, uint8_t c, uint32_t level);
/* a vertical dithered gradient from a (top) to b (bottom) */
void fb_vgrad(int32_t x, int32_t y, int32_t w, int32_t h, uint8_t a, uint8_t b);
void fb_disc(int32_t cx, int32_t cy, int32_t r, uint8_t c);                    /* filled circle */
void fb_ring(int32_t cx, int32_t cy, int32_t r, uint8_t c);                    /* circle outline */
/* hazard stripes (diagonal, 'period' px), the flyer border */
void fb_stripes(int32_t x, int32_t y, int32_t w, int32_t h, uint8_t a, uint8_t b, int32_t period);

/* text: the built-in 5x7 font, upper case, ' '..'_' (lower case drawn as upper case) */
#define FB_FONT_W 5
#define FB_FONT_H 7
int32_t fb_text(int32_t x, int32_t y, const char *s, uint8_t c, int32_t scale);   /* returns the x after */
int32_t fb_text_w(const char *s, int32_t scale);
/* text with a drop shadow (dx, dy) in another colour: the flyer look */
int32_t fb_text_shadow(int32_t x, int32_t y, const char *s, uint8_t c, uint8_t sh, int32_t scale, int32_t d);
void fb_text_center(int32_t y, const char *s, uint8_t c, int32_t scale);

/* the flush side: which bands differ from what was last sent (by a hash of each 16 x 16 tile), and mark them sent */
uint32_t fb_changed_bands(void);           /* bit b = band b changed */
int fb_band_span(uint32_t b, int32_t *x0, int32_t *w);   /* band b's changed span (whole tiles); 0 if none */
void fb_mark_sent(uint32_t bands);
void fb_invalidate(void);                  /* everything will be sent again (after the platform drew over it) */
uint32_t fb_hash(void);                    /* the whole screen's hash (FNV-1a), for tests */
void fb_copy(uint8_t *dst, const uint8_t *src);   /* a whole screen's bytes (both 4-byte aligned) */
/* scanlines: every other row of [y0, y1) a shade darker (the CRT look) */
void fb_scanlines(int32_t y0, int32_t y1);

#endif
