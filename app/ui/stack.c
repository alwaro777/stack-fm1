/* SPDX-License-Identifier: GPL-3.0-or-later
 * STACK — the main screen: the sound system that builds itself from the pattern.
 *
 * Every track with at least one step on brings a cabinet. Its category picks the row: SUB at the bottom, MID in the
 * middle, TOP on top (a track's CAT, set by its preset and changeable on its EDIT page). Layout, deterministic:
 *   - rows bottom-up: the SUB rows, then MID, then TOP; a category with more cabinets than a row holds (SUB 4, MID 4,
 *     TOP 5) stacks a second row of its own
 *   - within a row, tracks in order left to right; every row centred on the same axis, left of the flightcase
 * A new cabinet drops into its slot with a short bounce; the others slide to their new slots; a track that empties
 * lifts its cabinet away. A trigger pushes the cone out in proportion to its velocity. Muted: powered off (grey).
 * Solo: the others dimmed. The selected track: a blinking frame.
 *
 * Cost: the scenery is drawn once into a cached background; a frame copies it and draws the cabinets over it. The
 * flush then sends only the 16 x 16 tiles that changed. */
#include "ui_int.h"

#define FLOOR_Y 196              /* the stage's top edge: cabinets stand on it */
#define AXIS_X 104               /* the stack's centre line (the flightcase stands to its right) */
#define GAP 2
#define FALL_MS 520u
#define MOVE_MS 260u
#define LEAVE_MS 260u
#define EXC_STEP_MS 45u          /* a pushed cone falls back one level every 45 ms */

static const uint8_t BOX_W[NCATS] = {50, 44, 36}, BOX_H[NCATS] = {52, 40, 28}, ROW_CAP[NCATS] = {4, 4, 5};

enum { A_IDLE, A_FALL, A_MOVE, A_LEAVE };
typedef struct {
    uint8_t present, state, kind, exc;
    int16_t x, y, fx, fy, tx, ty;
    uint32_t t0, exc_t;
} sbox_t;

static sbox_t box[NTRACKS];
static uint8_t stage_bg[FB_H * FB_STRIDE] STK_BIG __attribute__((aligned(4)));
static uint8_t bg_ready;

/* ---------------------------------------------------------------- easing --- */
/* t and the results in Q12 (4096 = 1) */
static int32_t ease_out_cubic(int32_t t)
{
    int32_t u = 4096 - t;
    return 4096 - (((u * u) >> 12) * u >> 12);
}

static int32_t ease_in_quad(int32_t t) { return (t * t) >> 12; }

static int32_t ease_out_bounce(int32_t t)          /* the classic piecewise bounce: two small hops after landing */
{
    static const int32_t N1 = 30976;               /* 7.5625 in Q12 */
    int32_t u;
    if (t < 1489) {                                /* 1 / 2.75 */
        return (N1 * ((t * t) >> 12)) >> 12;
    } else if (t < 2979) {                         /* 2 / 2.75 */
        u = t - 2234;                              /* 1.5 / 2.75 */
        return ((N1 * ((u * u) >> 12)) >> 12) + 3072;
    } else if (t < 3724) {                         /* 2.5 / 2.75 */
        u = t - 3351;                              /* 2.25 / 2.75 */
        return ((N1 * ((u * u) >> 12)) >> 12) + 3840;
    }
    u = t - 3910;                                  /* 2.625 / 2.75 */
    return ((N1 * ((u * u) >> 12)) >> 12) + 4032;
}

static int32_t lerp(int32_t a, int32_t b, int32_t e) { return a + (((b - a) * e) >> 12); }

/* ---------------------------------------------------------------- layout --- */
static int track_active(uint32_t t)
{
    const track_t *tr = &g_proj.pat.trk[t];
    uint32_t i, n = tr->len ? tr->len : 1u;
    for (i = 0; i < n && i < NSTEPS; i++)
        if (tr->step[i].flags & STEP_ON)
            return 1;
    return 0;
}

/* the slots of every cabinet that stays (not leaving) */
static void layout(void)
{
    int32_t y = FLOOR_Y, cat;
    for (cat = 0; cat < NCATS; cat++) {
        uint8_t members[NTRACKS];
        int32_t n = 0, start, t;
        for (t = 0; t < NTRACKS; t++)
            if (box[t].present && box[t].state != A_LEAVE && g_proj.pat.trk[t].category == cat)
                members[n++] = (uint8_t)t;
        for (start = 0; start < n; start += ROW_CAP[cat]) {
            int32_t k = n - start < ROW_CAP[cat] ? n - start : ROW_CAP[cat], i;
            int32_t w = k * BOX_W[cat] + (k - 1) * GAP, x = AXIS_X - w / 2;
            y -= BOX_H[cat];
            for (i = 0; i < k; i++) {
                sbox_t *b = &box[members[start + i]];
                b->tx = (int16_t)(x + i * (BOX_W[cat] + GAP));
                b->ty = (int16_t)y;
                b->kind = (uint8_t)cat;
            }
            y -= 1;
        }
    }
}

int stack_update(uint32_t now)
{
    int16_t otx[NTRACKS], oty[NTRACKS];
    uint32_t t;
    int busy = 0;
    for (t = 0; t < NTRACKS; t++) {
        sbox_t *b = &box[t];
        int act = track_active(t);
        otx[t] = b->tx;
        oty[t] = b->ty;
        if (act && (!b->present || b->state == A_LEAVE)) {
            if (!b->present)
                b->state = A_FALL, b->t0 = now, b->x = b->y = -1000;
            else
                b->state = A_MOVE, b->fx = b->x, b->fy = b->y, b->t0 = now;   /* caught on its way out */
            b->present = 1;
            otx[t] = oty[t] = -1000;
        } else if (!act && b->present && b->state != A_LEAVE) {
            b->state = A_LEAVE;
            b->fx = b->x;
            b->fy = b->y;
            b->t0 = now;
        }
    }
    layout();
    for (t = 0; t < NTRACKS; t++) {
        sbox_t *b = &box[t];
        int32_t e, el;
        if (!b->present)
            continue;
        if (b->state == A_FALL && b->x == -1000) {  /* a new cabinet: from above its slot */
            b->fx = b->tx;
            b->fy = (int16_t)(b->ty - 150);
        } else if ((b->tx != otx[t] || b->ty != oty[t]) && b->state != A_LEAVE && otx[t] != -1000) {
            b->state = A_MOVE;                     /* its slot moved: slide there from where it is */
            b->fx = b->x;
            b->fy = b->y;
            b->t0 = now;
        }
        el = (int32_t)(now - b->t0);
        switch (b->state) {
        case A_FALL:
            e = el >= (int32_t)FALL_MS ? 4096 : ease_out_bounce(el * 4096 / (int32_t)FALL_MS);
            b->x = b->tx;
            b->y = (int16_t)lerp(b->fy, b->ty, e);
            if (el >= (int32_t)FALL_MS)
                b->state = A_IDLE;
            break;
        case A_MOVE:
            e = el >= (int32_t)MOVE_MS ? 4096 : ease_out_cubic(el * 4096 / (int32_t)MOVE_MS);
            b->x = (int16_t)lerp(b->fx, b->tx, e);
            b->y = (int16_t)lerp(b->fy, b->ty, e);
            if (el >= (int32_t)MOVE_MS)
                b->state = A_IDLE;
            break;
        case A_LEAVE:
            e = el >= (int32_t)LEAVE_MS ? 4096 : ease_in_quad(el * 4096 / (int32_t)LEAVE_MS);
            b->x = b->fx;
            b->y = (int16_t)lerp(b->fy, -80, e);
            if (el >= (int32_t)LEAVE_MS)
                b->present = 0;
            break;
        default:
            b->x = b->tx;
            b->y = b->ty;
        }
        if (b->state != A_IDLE)
            busy = 1;
        if (b->exc && now - b->exc_t >= EXC_STEP_MS) {   /* the cone settles */
            b->exc--;
            b->exc_t = now;
            busy = 1;
        }
    }
    return busy;
}

void stack_trigger(uint32_t t, uint32_t vel, uint32_t flags, uint32_t now)
{
    uint8_t level = (uint8_t)(vel >= 100u || (flags & STEP_ACCENT) ? 3 : vel >= 60u ? 2 : 1);
    if (t >= NTRACKS)
        return;
    if (level >= box[t].exc) {
        box[t].exc = level;
        box[t].exc_t = now;
    }
}

int stack_box(uint32_t t, int32_t *x, int32_t *y, int *kind, int *exc, int *state)
{
    const sbox_t *b = &box[t & 7u];
    *x = b->x;
    *y = b->y;
    *kind = b->kind;
    *exc = b->exc;
    *state = b->state;
    return b->present;
}

void stack_reset(void)           /* everything off the stage (a new project): the next update builds it again */
{
    uint32_t t;
    for (t = 0; t < NTRACKS; t++)
        box[t].present = 0, box[t].exc = 0;
}

/* --------------------------------------------------------------- scenery --- */
static void flightcase(int32_t x, int32_t y, int32_t w, int32_t h)
{
    fb_rect(x, y, w, h, P_BLACK);
    fb_frame(x, y, w, h, P_DGRAY);
    fb_rect(x, y, 4, 4, P_LGRAY);                  /* the corners' steel */
    fb_rect(x + w - 4, y, 4, 4, P_LGRAY);
    fb_rect(x, y + h - 4, 4, 4, P_LGRAY);
    fb_rect(x + w - 4, y + h - 4, 4, 4, P_LGRAY);
    fb_rect(x + w / 2 - 5, y + 9, 10, 3, P_GRAY);  /* the handle */
    fb_rect(x + 4, y + 15, 7, 5, P_YELLOW);        /* stickers */
    fb_rect(x + 13, y + 14, 6, 6, P_LIME);
    fb_rect(x + w - 10, y + 17, 6, 4, P_MAGENTA);
    fb_rect(x + 6, y + h - 9, 8, 3, P_RED);
    fb_rect(x + 3, y + h, 3, 2, P_GRAY);           /* castors */
    fb_rect(x + w - 6, y + h, 3, 2, P_GRAY);
}

static void truss(int32_t x, int32_t y0, int32_t y1)   /* a lattice tower leaning on the back wall */
{
    int32_t y;
    fb_vline(x, y0, y1 - y0, P_GRAY);
    fb_vline(x + 12, y0, y1 - y0, P_GRAY);
    for (y = y0; y + 12 <= y1; y += 12) {
        int32_t i;
        fb_hline(x, y, 13, P_DGRAY);
        for (i = 0; i < 12; i++)
            fb_pixel(x + i, y + i, P_DGRAY);
    }
}

static void build_background(void)
{
    uint32_t seed = 0x2468ACE1u, i;
    fb_vgrad(0, 12, FB_W, FLOOR_Y - 12, P_NIGHT, P_PURPLE);
    for (i = 0; i < 40u; i++) {                    /* stars, the same every time */
        seed = seed * 1664525u + 1013904223u;
        fb_pixel((int32_t)(seed >> 8) % FB_W, 14 + (int32_t)((seed >> 20) % 70u), (seed >> 4) & 1u ? P_GRAY : P_LGRAY);
    }
    /* the banner, hung from two strings: an original name, no real sound system's */
    {
        int32_t bw = 104, bx = AXIS_X - bw / 2, by = 18;
        fb_vline(bx + 6, 12, 6, P_LGRAY);
        fb_vline(bx + bw - 7, 12, 6, P_LGRAY);
        fb_dither(bx, by, bw, 15, P_MAGENTA, P_PURPLE, 3);
        fb_frame(bx, by, bw, 15, P_BLACK);
        fb_text_shadow(bx + (bw - fb_text_w("STACK HI-FI", 1)) / 2, by + 4, "STACK HI-FI", P_YELLOW, P_BLACK, 1, 1);
    }
    truss(220, 40, FLOOR_Y);
    /* the stage: plywood with plank joints */
    fb_dither(0, FLOOR_Y, FB_W, 18, P_WOOD, P_DORANGE, 4);
    fb_hline(0, FLOOR_Y, FB_W, P_BLACK);
    for (i = 0; i < 6u; i++)
        fb_vline(20 + (int32_t)i * 42, FLOOR_Y + 1, 17, P_DORANGE);
    fb_hline(0, FLOOR_Y + 8, FB_W, P_DORANGE);
    flightcase(208, FLOOR_Y - 26, 30, 28);
    fb_copy(stage_bg, fb_px);
    bg_ready = 1;
}

/* ------------------------------------------------------------------ draw --- */
static void draw_box(uint32_t t, uint32_t now)
{
    const sbox_t *b = &box[t];
    int muted = (core_mute >> t) & 1u, solo_out = core_solo && !((core_solo >> t) & 1u);
    int32_t w = BOX_W[b->kind], h = BOX_H[b->kind];
    char n[2];
    ui_draw_cabinet(b->x, b->y, w, h, b->kind, muted ? 0 : b->exc, muted);
    if (solo_out && !muted)                        /* in the shadow of a solo: two pixels in three darkened */
        fb_shade(b->x, b->y, w, h, P_NIGHT, 11);
    n[0] = (char)('1' + t);
    n[1] = 0;
    fb_rect(b->x + 2, b->y + 2, 7, 9, P_BLACK);    /* the track's number, top left */
    fb_text(b->x + 3, b->y + 3, n, muted ? P_GRAY : P_WHITE, 1);
    if ((int)t == ui.track && ((now / 300u) & 1u))
        fb_frame(b->x - 1, b->y - 1, w + 2, h + 2, P_LIME);
}

void stack_draw(uint32_t now)
{
    uint32_t t, n = 0;
    int32_t sub_l = 1000, sub_r = -1000, sub_y = 0;
    if (!bg_ready)
        build_background();
    fb_copy(fb_px, stage_bg);
    for (t = 0; t < NTRACKS; t++)                  /* the settled ones, then those in the air on top */
        if (box[t].present && box[t].state != A_FALL && box[t].state != A_LEAVE) {
            draw_box(t, now);
            n++;
            if (box[t].kind == CAT_SUB && box[t].state == A_IDLE && box[t].y == FLOOR_Y - BOX_H[CAT_SUB]) {
                if (box[t].x < sub_l)
                    sub_l = box[t].x;
                if (box[t].x + BOX_W[CAT_SUB] > sub_r)
                    sub_r = box[t].x + BOX_W[CAT_SUB];
                sub_y = box[t].y;
            }
        }
    if (sub_r - sub_l > BOX_W[CAT_SUB]) {          /* a ratchet strap round the bottom row */
        fb_rect(sub_l - 1, sub_y + 38, sub_r - sub_l + 2, 3, P_YELLOW);
        fb_hline(sub_l - 1, sub_y + 41, sub_r - sub_l + 2, P_DORANGE);
    }
    for (t = 0; t < NTRACKS; t++)
        if (box[t].present && (box[t].state == A_FALL || box[t].state == A_LEAVE)) {
            draw_box(t, now);
            n++;
        }
    if (!n) {
        fb_text_center(110, "AN EMPTY STAGE", P_LGRAY, 1);
        fb_text_center(122, "PUT STEPS ON: EVERY TRACK", P_GRAY, 1);
        fb_text_center(132, "BRINGS ITS SPEAKER", P_GRAY, 1);
    }
}
