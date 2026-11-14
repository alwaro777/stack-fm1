/* SPDX-License-Identifier: GPL-3.0-or-later
 * STACK — speaker cabinets, drawn procedurally in the palette (no bitmaps to license, and they scale to any box size
 * the stack layout asks for). Original designs: a scoop bin with one cone (SUB), a horn-loaded mid (MID), a tweeter
 * box (TOP). Phase 3 animates them; F1 uses them on the splash. */
#include "fb.h"
#include "ui.h"

/* the shared shell: black edge, orange face, a darker inner border and corner "bolts" */
static void cab_shell(int32_t x, int32_t y, int32_t w, int32_t h, uint8_t face, uint8_t shade, uint8_t light)
{
    fb_rect(x, y, w, h, P_BLACK);
    fb_rect(x + 1, y + 1, w - 2, h - 2, face);
    fb_hline(x + 1, y + 1, w - 2, light);                 /* top light edge */
    fb_hline(x + 1, y + h - 2, w - 2, shade);             /* bottom shade */
    fb_vline(x + w - 2, y + 1, h - 2, shade);
    if (w >= 24 && h >= 24) {                             /* bolts */
        fb_pixel(x + 3, y + 3, P_BLACK);
        fb_pixel(x + w - 4, y + 3, P_BLACK);
        fb_pixel(x + 3, y + h - 4, P_BLACK);
        fb_pixel(x + w - 4, y + h - 4, P_BLACK);
    }
}

/* a cone: dark surround, a dithered "grille" over it, a dust cap that grows with the excursion */
static void cab_cone(int32_t cx, int32_t cy, int32_t r, int excursion, uint8_t cap, int dim)
{
    int32_t dy, dx;
    fb_disc(cx, cy, r, P_BLACK);
    fb_ring(cx, cy, r - 1, dim ? P_DGRAY : P_GRAY);      /* surround */
    for (dy = -r + 3; dy <= r - 3; dy++)                  /* the grille: a dot screen, densest at the centre */
        for (dx = -r + 3; dx <= r - 3; dx++)
            if (dx * dx + dy * dy <= (r - 3) * (r - 3) && !((dx + dy) & 1) && !(dy & 1))
                fb_pixel(cx + dx, cy + dy, dim ? P_DGRAY : P_PURPLE);
    if (r >= 6)
        fb_disc(cx, cy, r / 3 + excursion, cap);
    if (excursion > 1)                                    /* a push: the surround flashes */
        fb_ring(cx, cy, r, dim ? P_GRAY : P_YELLOW);
}

void ui_draw_cabinet(int32_t x, int32_t y, int32_t w, int32_t h, int kind, int excursion, int dim)
{
    uint8_t face = dim ? P_DGRAY : P_ORANGE, shade = dim ? P_BLACK : P_DORANGE, light = dim ? P_GRAY : P_LORANGE;
    uint8_t cap = dim ? P_GRAY : P_ORANGE;
    int32_t m;
    cab_shell(x, y, w, h, face, shade, light);
    if (kind == 0) {                                      /* SUB: a big cone over a scoop mouth */
        int32_t r = (w < h ? w : h) * 2 / 5;
        int32_t cy = y + h / 2 - h / 10;
        cab_cone(x + w / 2, cy, r, excursion, cap, dim);
        m = h / 7;                                        /* the scoop's mouth: dark slot with ribs */
        if (m >= 3) {
            int32_t i;
            fb_rect(x + 4, y + h - m - 3, w - 8, m, P_BLACK);
            for (i = 1; i < 4; i++)
                fb_vline(x + 4 + (w - 8) * i / 4, y + h - m - 3, m, face);
        }
    } else if (kind == 1) {                               /* MID: a horn mouth, a trapezoid opening onto a cone */
        int32_t i, hw = w / 2 - 4, hh = h - 8;
        for (i = 0; i < hh; i++) {                        /* the horn narrows towards its throat */
            int32_t half = hw - (hw - hw / 3) * i / hh;
            fb_hline(x + w / 2 - half, y + 4 + i, 2 * half, i % 3 == 2 && !dim ? P_DGRAY : P_BLACK);
        }
        cab_cone(x + w / 2, y + h / 2 + 1, (h < w ? h : w) / 5, excursion, cap, dim);
    } else {                                              /* TOP: two tweeters side by side */
        int32_t r = (h < w / 2 ? h : w / 2) * 3 / 10;
        cab_cone(x + w / 4 + 1, y + h / 2, r, excursion, dim ? P_GRAY : P_YELLOW, dim);
        cab_cone(x + 3 * w / 4 - 1, y + h / 2, r, excursion, dim ? P_GRAY : P_YELLOW, dim);
    }
}
