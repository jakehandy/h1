/* h1 on the FM-1: the app's half of fm1-base's contract (device/unity.c includes this, then app.c). */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "h1"
#define APP_SPLASH                       /* no paper splash: the screen stays the bezel's black until the */
void app_splash(void);                   /* instrument comes up */
#define APP_SPLASH_RGB RGB(0, 0, 0)
/* the platform's own canvas (its update, crash and safe-mode screens draw text a strip at a time): a strip of
 * 48 rows is enough, and the pool needs the rest for the framebuffer and the delay lines */
#define APP_CANVAS_PX (240u * 48u)
#define APP_BLOCK 256u                   /* frames per audio block: 5.8 ms */
/* Bluetooth at power-on: as the MIDI page's BT was left (app_init has loaded the state by then) */
int h1_bluetooth(void);
#define APP_BLE_AT_BOOT (h1_bluetooth())

/* the store: the saved state, the platform's panel calibration and the loop recorder's loop. Each object is an
 * A/B pair of 4 KiB sectors, written by the platform's storage (CRC, a torn write leaves the copy before in
 * charge) through felucca-bsp's flash driver, which knows both chips the FM-1 has shipped with (P25Q80H and
 * UC25HQ80). The loop's pair, 0xDE000..0xDFFFF, is the top of the data area the platform gives apps
 * (0x97000..0xDFFFF): nowhere near the boot loader, the app, the panel's calibration (0xDC000) or the
 * update's staging (0xE0000 up). The numbers are written in the records: never renumber them.
 * OBJ_BLE is Bluetooth's: the radio's address and any bond (fm1.toml ble), in the free pair just under the
 * panel's calibration, 0xDA000..0xDBFFF. */
enum { OBJ_STATE, OBJ_PANEL, OBJ_LOOP, OBJ_BLE, OBJ_NOBJ };
#define APP_STORE_MAP {0xFC000u, 0xDC000u, 0xDE000u, 0xDA000u}

#ifdef PLAT_HOST                         /* the host simulator's script: "badstart" (app.c) */
#define APP_HOST_CMD
int app_host_cmd(const char *cmd, const char *a, const char *b, const char *c, const char *d);
#endif

void app_init(void);
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);
void app_panic(void);
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *a, const char *b);
