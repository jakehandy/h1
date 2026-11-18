/* H1 on the FM-1: the app's half of fm1-base's contract (device/unity.c includes this, then app.c).
 * NOT YET BUILT FOR THE DEVICE: written against fm1-base's template, to be compiled and checked
 * with the JieLi toolchain once the design is approved in the simulator. */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "H1"
#define APP_SPLASH_RGB RGB(0, 0, 0)      /* the bezel's black: the first thing on screen is the instrument */
#define APP_BLOCK 256u                   /* frames per audio block: 5.8 ms */

/* the store: the saved state, and the platform's panel calibration */
enum { OBJ_STATE, OBJ_PANEL, OBJ_NOBJ };
#define APP_STORE_MAP {0xFC000u, 0xDC000u}

void app_init(void);
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);
void app_panic(void);
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *a, const char *b);
