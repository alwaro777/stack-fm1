/* SPDX-License-Identifier: GPL-3.0-or-later
 * STACK — the app's half of the contract with fm1-base (base/device/unity.c on the FM-1, base/host/plat_host.c on
 * the computer, base/web/emu in the browser). Everything below the contract is in core/ and ui/. */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "STACK"
#define APP_SPLASH_RGB RGB(255, 98, 18)
#define APP_BLOCK 256u                   /* frames per audio block: 5.8 ms at 44.1 kHz */
#define APP_SPLASH                       /* our flyer instead of the platform's name screen */
void app_splash(void);

/* the store: the app's objects (settings from Phase 5) and the platform's panel calibration, and their flash
 * offsets (two 4 KiB sectors each). OBJ_PANEL keeps fm1-base's template offset, so a calibration survives moving
 * between fm1-base firmwares. */
enum { OBJ_SETTINGS, OBJ_PANEL, OBJ_NOBJ };
#define APP_STORE_MAP {0xFC000u, 0xDC000u}

/* the contract: the platform calls these */
void app_init(void);                     /* boot: the engine */
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);   /* audio interrupt: n stereo frames, 24-bit */
void app_panic(void);                    /* silence now (before an update rewrites the flash) */
void app_ui_init(void);                  /* the first screen, after the splash */
void app_frame(void);                    /* the UI: input and a redraw, about 60 times a second */
void app_input_only(void);               /* between frames: input only */
void app_say(const char *a, const char *b);   /* a short message from the platform */

/* the host simulator's checks (base/host/plat_host.c "expect WHAT VALUE"): view, voices, octave */
#define APP_HOST_EXPECT
int app_expect(const char *what, int *got);
