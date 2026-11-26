/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 swtdr34mz (FIKH-S) */
/* FIKH-S by swtdr34mz: ein experimenteller Drone-, Bass- und Textur-Synth für das M-VAVE FM-1 (auf fm1-base) nach dem
 * Konzept des Forge TME VHIKK X. Dieser Header ist die App-Hälfte des Vertrags mit der Plattform (device/unity.c,
 * host/plat_host.c). */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "FIKH-S"
#define APP_SPLASH_RGB RGB(206, 170, 98)
#define APP_SPLASH                       /* app_splash: das eigene Panel */
#define APP_BLOCK 128u                   /* Frames pro Audioblock: 2.9 ms bei 44.1 kHz */
#define APP_TOUCH                        /* Anschlagdynamik von den Tasten (lib/touch.h) */
#define APP_BATTERY_RAW                  /* plat_battery_raw: der Akku stufenlos */
#define APP_CANVAS_PX (240u * 120u)      /* der Bildschirm in zwei Hälften (der Rest des Pools gehört dem Klang) */

/* der Speicher: die Einstellungen (jeder Algorithmus mit seinen Reglern) und die Panel-Kalibrierung (die der Plattform) */
enum { OBJ_SETTINGS, OBJ_PANEL, OBJ_NOBJ };
#define APP_STORE_MAP {0xFC000u, 0xDC000u}

void app_init(void);
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);
void app_panic(void);
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *a, const char *b);
void app_splash(void);

#define APP_HOST_EXPECT
int app_expect(const char *what, int *got);
