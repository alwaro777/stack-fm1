/* SPDX-License-Identifier: GPL-3.0-or-later
 * STACK — the platform glue (our "hal"): fm1-base's plat.h on one side, core/ and ui/ on the other.
 * Part of fm1-base's unity build, after the platform (gfx.c, lcd.c are in scope). core/ and ui/ never include plat.h:
 * they build alone for the unit tests (tests/unit). */
#define STK_BIG PLAT_POOL                /* the framebuffer and the stage's cached background: the large RAM pool */
#include "core/core.c"
#include "core/drums.c"
#include "core/acid.c"
#include "core/synth.c"
#include "core/fm.c"
#include "core/fxsyn.c"
#include "core/voices.c"
#include "core/fx.c"
#include "core/presets.c"
#include "ui/fb.c"
#include "ui/cabinet.c"
#include "ui/ui.c"
#include "ui/ui_draw.c"
#include "ui/stack.c"

/* the plat.h order is the UI's order: the input is copied bit for bit */
_Static_assert(B_FX == UB_FX && B_HOME == UB_HOME && B_PLAY == UB_PLAY && B_OCTDN == UB_OCTDN && NB == UI_NBUTTONS,
               "plat.h buttons in ui.h order");
_Static_assert(EN_SELECT == UE_SELECT && EN_K4 == UE_K4 && NE == UI_NENC, "plat.h encoders in ui.h order");
_Static_assert(NKEYS == UI_NKEYS, "27 keys");

/* --------------------------------------------------------------- display --- */
/* Two band buffers (240 x 16 RGB565, 7.5 KiB each): lcd_blit leaves its DMA running, so a band is converted into one
 * buffer while the other may still be on its way to the panel; lcd_blit waits for the previous transfer before it
 * starts the next, so by the time a buffer is reused its transfer has ended. */
static uint16_t band_buf[2][FB_W * FB_BAND] PLAT_POOL;
static uint16_t pal_be[16];              /* the palette as the panel takes it: RGB565, high byte first */
#define FLUSH_MAX_BANDS 8u               /* per frame; the rest goes next frame (the boot screen: two frames) */

static void palette_init(void)
{
    uint32_t i;
    for (i = 0; i < P_NCOLORS; i++) {
        uint32_t c = RGB(fb_palette_rgb[i][0], fb_palette_rgb[i][1], fb_palette_rgb[i][2]);
        pal_be[i] = (uint16_t)((c >> 8) | ((c & 0xFFu) << 8));
    }
}

static void flush(uint32_t max_bands)
{
    static uint32_t which;
    uint32_t m = fb_changed_bands(), sent = 0, b, n = 0;
    for (b = 0; b < FB_NBANDS && n < max_bands; b++) {
        uint16_t *dst;
        int32_t x0, w, r, i;
        if (!(m >> b & 1u) || !fb_band_span(b, &x0, &w))
            continue;
        dst = band_buf[which];
        which ^= 1u;
        for (r = 0; r < FB_BAND; r++) {           /* the changed span only: two pixels a byte, left in the high nibble */
            const uint8_t *src = &fb_px[(b * FB_BAND + (uint32_t)r) * FB_STRIDE + (uint32_t)x0 / 2u];
            uint16_t *d = &dst[r * w];
            for (i = 0; i < w / 2; i++) {
                d[2 * i] = pal_be[src[i] >> 4];
                d[2 * i + 1] = pal_be[src[i] & 15u];
            }
        }
        lcd_blit((uint32_t)x0, b * FB_BAND, (uint32_t)w, FB_BAND, dst);
        sent |= 1u << b;
        n++;
    }
    fb_mark_sent(sent);
}

/* ----------------------------------------------------------------- input --- */
/* Read between frames too (app_input_only) and handed to the next frame: encoder detents add up, and a button or key
 * seen held at any read counts as held for that frame, so a tap shorter than a frame (~16 ms) is never lost. */
static int32_t enc_pending[UI_NENC];
static uint32_t btn_seen, keys_seen;

static void input_collect(void)
{
    uint32_t i;
    for (i = 0; i < UI_NENC; i++)
        enc_pending[i] += plat_enc((int)i);
    btn_seen |= plat_buttons();
    keys_seen |= plat_keys();
}

/* ------------------------------------------------------------- contract --- */
void app_splash(void)                    /* before the main loop: the flyer at t = 0, the whole screen */
{
    palette_init();
    ui_draw_splash(0);
    fb_invalidate();
    flush(FB_NBANDS);
}

void app_init(void)
{
    core_init();
}

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    core_render(lr, n, master_q12);
}

void app_panic(void)
{
    core_panic();
}

void app_ui_init(void)
{
    palette_init();
    ui_init();
    fb_invalidate();                     /* the platform may have drawn over the screen since the splash */
}

void app_input_only(void)
{
    input_collect();
}

void app_frame(void)
{
    ui_input_t in;
    ui_leds_t leds;
    uint32_t i;
    input_collect();
    in.buttons = btn_seen;
    in.keys = keys_seen;
    btn_seen = keys_seen = 0;
    in.master = plat_master();
    for (i = 0; i < UI_NENC; i++) {
        in.enc[i] = enc_pending[i];
        enc_pending[i] = 0;
    }
    ui_set_stats(plat_cpu_pct(), plat_xruns());
    if (ui_frame(&in, plat_ms()) || fb_changed_bands())
        flush(FLUSH_MAX_BANDS);
    ui_leds(&leds);
    plat_glow(1);                        /* unlit buttons glow, readable in the dark */
    plat_leds(leds.buttons, leds.keys);
}

void app_say(const char *a, const char *b)
{
    ui_say(a, b);
}

#ifdef PLAT_HOST
int app_expect(const char *what, int *got)
{
    if (!strcmp(what, "view"))
        *got = ui_view();
    else if (!strcmp(what, "voices"))
        *got = (int)core_voices_active();
    else if (!strcmp(what, "octave"))
        *got = ui.octave;
    else if (!strcmp(what, "track"))
        *got = ui.track;
    else if (!strcmp(what, "playing"))
        *got = core_playing();
    else if (!strcmp(what, "pos"))
        *got = core_play_pos((uint32_t)ui.track);
    else
        return 0;
    return 1;
}
#endif
