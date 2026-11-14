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
#include "core/pattern.c"
#include "core/project.c"
#include "core/demo.c"
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

/* ------------------------------------------------------------------ storage --- */
/* One buffer for every object going to or from the flash (and lent to a backup restore). Saving writes only the
 * objects whose bytes changed since they were last saved or loaded (a hash of each): a flash write stops the sound
 * for a moment, so SAVE asks for the transport to be stopped first. */
uint8_t stk_io[PROJ_IO_MAX] __attribute__((aligned(4))) PLAT_POOL;
static uint32_t obj_hash[OBJ_NOBJ];
static uint8_t backup_held;

static uint32_t io_hash(uint32_t n)
{
    uint32_t h = 2166136261u, i;
    for (i = 0; i < n; i++)
        h = (h ^ stk_io[i]) * 16777619u;
    return h ^ n;
}

static uint32_t encode(uint32_t obj)              /* the object's bytes into stk_io; its length */
{
    if (obj == OBJ_SETTINGS)
        return proj_encode_settings(stk_io);
    if (obj == OBJ_KIT)
        return proj_encode_kit(stk_io);
    return proj_encode_pattern(obj - OBJ_PAT0, stk_io);
}

static int project_dirty(void)                    /* anything not saved yet (for SAVE's light) */
{
    uint32_t o;
    pattern_store_all();
    for (o = 0; o < OBJ_NOBJ; o++) {
        if (o == OBJ_PANEL)
            continue;
        if (io_hash(encode(o)) != obj_hash[o])
            return 1;
    }
    return 0;
}

static int project_save(void)                     /* the objects that changed; -1 on a failed write */
{
    uint32_t o, n, wrote = 0;
    if (backup_held)
        return 0;
    pattern_store_all();
    for (o = 0; o < OBJ_NOBJ; o++) {
        uint32_t h;
        if (o == OBJ_PANEL)
            continue;
        n = encode(o);
        h = io_hash(n);
        if (h == obj_hash[o])
            continue;
        if (plat_store_save(o, stk_io, n) != 0)
            return -1;
        obj_hash[o] = h;
        wrote++;
    }
    return (int)wrote;
}

static int project_load(void)                     /* 0 = a saved project is in; -1 = none (the demo stays) */
{
    uint32_t o;
    int n = plat_store_load(OBJ_SETTINGS, stk_io, PROJ_IO_MAX);
    if (n <= 0 || proj_decode_settings(stk_io, (uint32_t)n) != 0)
        return -1;
    n = plat_store_load(OBJ_KIT, stk_io, PROJ_IO_MAX);
    if (n <= 0 || proj_decode_kit(stk_io, (uint32_t)n) != 0)
        return -1;
    for (o = 0; o < 16u; o++) {
        n = plat_store_load(OBJ_PAT0 + o, stk_io, PROJ_IO_MAX);
        if (n <= 0 || proj_decode_pattern(o, stk_io, (uint32_t)n) != 0)
            bank_clear(&g_bank[o]);               /* (a pattern never saved: empty) */
    }
    pattern_load_current();
    return 0;
}

static void hashes_now(int loaded)                /* loaded: what is in RAM is what is in the flash; else nothing
                                                   * is (the first SAVE writes every object) */
{
    uint32_t o;
    pattern_store_all();
    for (o = 0; o < OBJ_NOBJ; o++)
        obj_hash[o] = o == OBJ_PANEL || !loaded ? 0 : io_hash(encode(o));
}

int app_backup_playing(void) { return core_playing(); }
void app_backup_flush(void) { project_save(); }
void app_backup_hold(int on) { backup_held = (uint8_t)on; }
void app_backup_restored(void)
{
    int ok;
    core_init();
    ok = project_load() == 0;
    if (!ok)
        core_load_demo();
    hashes_now(ok);
    stack_reset();
}

void app_init(void)
{
    core_init();                          /* (the demo) */
    hashes_now(project_load() == 0);      /* a saved project replaces it, if there is one */
}

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    uint32_t pkt;
    int port;
    while (plat_midi_in(&pkt, &port))     /* MIDI in, before the block: notes, clock, transport */
        core_midi_in((pkt >> 8) & 0xFFu, (pkt >> 16) & 0x7Fu, (pkt >> 24) & 0x7Fu);
    core_render(lr, n, master_q12);
    while (core_midi_pop(&pkt))           /* and out: the tracks' notes, the clock */
        plat_midi_send(pkt, TO_USB);
}

void app_overload(void)
{
    core_overload();
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
    if (ui_take_save()) {                 /* SAVE: stopped only (a flash write stops the sound for a moment) */
        int r;
        if (core_playing()) {
            ui_say("STOP", "TO SAVE");
        } else if ((r = project_save()) < 0) {
            ui_say("SAVE", "FAILED");
        } else {
            char t[8] = "";
            ui_int(t, r, sizeof t);
            ui_say("SAVED", r ? t : "NO CHANGE");
        }
    }
    {
        static uint32_t dirty_at;         /* SAVE lit while something is not saved: checked twice a second, stopped
                                           * only (it copies the working pattern into the bank, which a switch under
                                           * way in the audio interrupt also writes) */
        if (!core_playing() && plat_ms() - dirty_at >= 500u) {
            dirty_at = plat_ms();
            ui_set_unsaved(project_dirty());
        }
    }
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
    else if (!strcmp(what, "pattern"))
        *got = core_pattern();
    else if (!strcmp(what, "step0"))     /* the selected track's first step: on? */
        *got = g_proj.pat.trk[ui.track].step[0].flags & STEP_ON;
    else
        return 0;
    return 1;
}
#endif
