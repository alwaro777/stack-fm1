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
#define APP_OVERLOAD                     /* a render over 90 % of its time: the core sheds load (core.h) */
void app_overload(void);
void app_splash(void);

/* the store: the project as objects (core/project.c: the settings, the kit, the 16 patterns) and the platform's
 * panel calibration, with their flash offsets (two 4 KiB sectors each, A/B, CRC, torn-write safe). OBJ_PANEL keeps
 * fm1-base's template offset, so a calibration survives moving between fm1-base firmwares; the kit and the patterns
 * sit at the bottom of the data area (0x97000 .. 0xB9000), clear of the panel's pair at 0xDC000. */
enum { OBJ_SETTINGS, OBJ_PANEL, OBJ_KIT, OBJ_PAT0, OBJ_NOBJ = OBJ_PAT0 + 16 };
#define APP_STORE_MAP {0xFC000u, 0xDC000u, 0x97000u, \
    0x99000u, 0x9B000u, 0x9D000u, 0x9F000u, 0xA1000u, 0xA3000u, 0xA5000u, 0xA7000u, \
    0xA9000u, 0xAB000u, 0xAD000u, 0xAF000u, 0xB1000u, 0xB3000u, 0xB5000u, 0xB7000u}

/* backup and restore over USB, and the emulator's Save backup / Load backup (fm1.toml backup = true): the project's
 * objects, all or nothing */
#define APP_BACKUP_OBJS {OBJ_SETTINGS, OBJ_KIT, OBJ_PAT0, OBJ_PAT0 + 1, OBJ_PAT0 + 2, OBJ_PAT0 + 3, OBJ_PAT0 + 4, \
    OBJ_PAT0 + 5, OBJ_PAT0 + 6, OBJ_PAT0 + 7, OBJ_PAT0 + 8, OBJ_PAT0 + 9, OBJ_PAT0 + 10, OBJ_PAT0 + 11, \
    OBJ_PAT0 + 12, OBJ_PAT0 + 13, OBJ_PAT0 + 14, OBJ_PAT0 + 15}
#define APP_BACKUP_MAGIC 0x314B5453u        /* "STK1" */
#define APP_BACKUP_VERSION 1u
#define APP_BACKUP_WHAT "replaces the whole project"
#define APP_BACKUP_BUF stk_io
extern uint8_t stk_io[];
int app_backup_playing(void);
void app_backup_flush(void);
void app_backup_hold(int on);
void app_backup_restored(void);

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
