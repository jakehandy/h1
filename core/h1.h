/* H1 - a firmware for the M-VAVE FM-1. The public face of the portable core.
 *
 * The core knows nothing about hardware. A port feeds it the panel (keys, buttons, encoder
 * detents), asks it for audio in the audio interrupt and for a frame in the main loop, and
 * sends the dirty parts of the framebuffer to the panel. Three ports share it unchanged:
 * port/web (the browser simulator), port/host (tests: WAV and image dumps) and port/fm1
 * (the device, on fm1-base). */
#pragma once
#include <stdint.h>

#define H1_W 240
#define H1_H 240
#define H1_NKEYS 27              /* F3 .. G5 */

/* the panel, in fm1-base's order (plat.h B_* and EN_*) so the device port is a pass-through */
enum { H1_BTN_FX, H1_BTN_SEL, H1_BTN_ENV, H1_BTN_LFO, H1_BTN_EDIT, H1_BTN_GLO, H1_BTN_HOME, H1_BTN_SAVE,
       H1_BTN_ARP, H1_BTN_SEQ, H1_BTN_PLAY, H1_BTN_REC, H1_BTN_OCTDN, H1_BTN_OCTUP, H1_NBTN };
enum { H1_ENC_SELECT, H1_ENC_ALGO, H1_ENC_PRESET, H1_ENC_K1, H1_ENC_K2, H1_ENC_K3, H1_ENC_K4, H1_NENC };

void h1_init(uint32_t sample_rate);

/* main loop side */
void h1_key(int key, int down);                  /* key 0 = F3 */
void h1_button(int btn, int down);
void h1_encoder(int enc, int detents);           /* + = clockwise */
void h1_midi(uint8_t status, uint8_t d1, uint8_t d2);
void h1_frame(uint32_t now_ms);                  /* advance the animation and draw the frame */

/* audio interrupt side: n stereo frames, signed 24-bit in int32 (fm1-base's app_render format) */
void h1_render(int32_t *lr, uint32_t n);

/* the picture: 240 x 240 intensities, and the ink ramp that colours them (RGB565, 256 entries) */
const uint8_t *h1_framebuffer(void);
const uint16_t *h1_palette(void);

/* presenting: calls blit once per changed rectangle (in screen order) and returns the bytes a
 * 16-bit panel would be sent, the number that decides the frame rate on the device */
typedef void (*h1_blit_fn)(int x, int y, int w, int h);
uint32_t h1_present(h1_blit_fn blit);

/* lights */
uint32_t h1_key_leds(void);                      /* bit k = key k */
uint32_t h1_button_leds(void);                   /* bit b = H1_BTN_b */

/* saved state: one blob. dirty() says it changed and may be written (then call saved()). */
const void *h1_state(uint32_t *size);
int h1_state_load(const void *blob, uint32_t size);   /* 0 = taken */
int h1_state_dirty(void);
void h1_state_saved(void);

/* introspection for the simulator and tests */
int h1_instrument(void);
int h1_algorithm(void);                          /* of the current instrument, 0..7 */
int h1_page(void);                               /* 0 = home, else a settings page */
int h1_instrument_count(void);
const char *h1_instrument_name(int i);
const char *h1_knob_name(int inst, int k);       /* the four home-screen modulations */
const char *h1_algo_name(int inst, int a);       /* eight per instrument */
void h1_ink(int inst, uint8_t rgb[3]);
void h1_set_ink(int inst, uint8_t r, uint8_t g, uint8_t b);
uint32_t h1_voices_active(void);

/* Every encoder as a number: returns what it is set to right now and (where the pointers are not
 * null) its range, the name of what it is tied to and its value as text. The simulator draws its
 * sliders from this; set() moves a control there outright, the way a slider wants. */
int h1_control(int enc, int *lo, int *hi, const char **name, const char **text);
void h1_control_set(int enc, int value);
