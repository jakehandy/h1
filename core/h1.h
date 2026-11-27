/* h1 - a firmware for the M-VAVE FM-1. The public face of the portable core.
 *
 * The core knows nothing about hardware. A port feeds it the panel (keys, buttons, encoder
 * detents), asks it for audio in the audio interrupt and for a frame in the main loop, and
 * sends the dirty parts of the framebuffer to the panel. Three ports share it unchanged:
 * port/rfm1 (the build the browser simulator runs: github.com/jakehandy/fm1-browser-simulator),
 * port/host (tests: WAV and image dumps) and app/ (the device, on fm1-base). */
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
void h1_midi(uint8_t status, uint8_t d1, uint8_t d2);   /* MIDI in: notes with their velocity, CC 20-23 the
                                                  * knobs, CC 123 all notes off */
/* MIDI out: fn is called with each message h1 sends, on channel 1: the notes played on the keys, by the
 * arpeggiator and by the sequencer (note-on with its velocity, note-off), CC 20-23 as the four home knobs
 * turn, and CC 123 on a hush. Never what came in over MIDI. The notes are sent from inside h1_render, the
 * knobs from h1_encoder (the main loop): fn must be safe from both. NULL: nothing is sent (the default). */
void h1_midi_out(void (*fn)(uint8_t status, uint8_t d1, uint8_t d2));
void h1_master(int level);                       /* where the volume knob stands, 0..1000, every frame. The
                                                  * core shows it (a ring while it turns) and, with EDIT held,
                                                  * takes it as the volume of the instrument in hand */
int h1_global_volume(void);                      /* The port owns MASTER's gain and asks this first: -1, the
                                                  * gain follows the knob; 0..1000, it is held there (the knob
                                                  * was last the instrument's and has not come back through
                                                  * it). Safe to read from the audio side. */
void h1_frame(uint32_t now_ms);                  /* advance the animation and draw the frame */

/* audio interrupt side: n stereo frames, signed 24-bit in int32 (fm1-base's app_render format) */
void h1_render(int32_t *lr, uint32_t n);
/* Stems, for a USB audio input with 8 channels of the port's own beside the master: get gives the frame off
 * frames into the render being made (8 int16s, the next frame stride on, for *run frames), or NULL while nobody
 * records. h1_render then writes four stereo stems there, at the master's scale before MASTER: the instruments of
 * the recorded layers in the layer view's order, then the one in hand, a pair each, and everything else on the
 * fourth; each after its own volume, without the effects over the whole mix. Set it before the audio starts. */
void h1_stems(int16_t *(*get)(uint32_t off, uint32_t *run), uint32_t stride);

/* A second core, where a port has one. run hands it fn(arg) and returns 1, or returns 0 (then the core
 * that asked does the work); wait returns 0 when the job is back, anything else if it was dropped (then
 * too). h1_render gives it every other voice and does the rest meanwhile, so run and wait are called
 * from wherever h1_render is. The sound is the same to the bit with or without. Call it before the
 * audio starts; never is fine. */
void h1_two_cores(int (*run)(void (*fn)(uint32_t), uint32_t arg), int (*wait)(void));

/* the picture: 240 x 240 intensities, and the ink ramp that colours them (RGB565, 256 entries) */
const uint8_t *h1_framebuffer(void);
const uint16_t *h1_palette(void);

/* presenting: calls blit once per changed rectangle (in screen order) and returns the bytes a
 * 16-bit panel would be sent, the number that decides the frame rate on the device */
typedef void (*h1_blit_fn)(int x, int y, int w, int h);
uint32_t h1_present(h1_blit_fn blit);
/* The same in pieces, for a panel that is slow to take its pixels (the FM-1's: a full screen is 21 ms):
 * begin() finds what changed and queues it, next() hands out one rectangle at a time, and the pixels
 * of a queued rectangle are read from h1_panel(), which the next h1_frame does not touch. So the queue
 * can go out while the next frame is drawn: h1_while_drawing(fn) has fn called many times a frame from
 * inside the drawing, and fn sends a rectangle when the panel is ready for one. Empty the queue before
 * the next begin(). */
void h1_present_begin(void);
int h1_present_next(int *x, int *y, int *w, int *h);   /* 0: the queue is empty */
int h1_present_left(void);                       /* rectangles still queued */
int h1_present_rects(void);                      /* rectangles the last begin() queued */
uint32_t h1_present_bytes(void);                 /* what the last begin() queued costs to send, in bytes */
const uint8_t *h1_panel(void);                   /* the picture as the panel has it once the queue is out */
void h1_while_drawing(void (*fn)(void));

/* lights */
uint32_t h1_key_leds(void);                      /* bit k = key k */
uint32_t h1_button_leds(void);                   /* bit b = H1_BTN_b */

/* saved state: one blob. dirty() says it changed and may be written (then call saved()). */
const void *h1_state(uint32_t *size);
int h1_state_load(const void *blob, uint32_t size);   /* 0 = taken */
int h1_state_dirty(void);
void h1_state_saved(void);
/* The loop recorder's loop, as a blob of its own (at most H1_LOOP_MAX bytes: 2,048 events, a note-on
 * and a note-off a note), so that what was recorded is there
 * after a power-off. Load it after h1_state_load (which empties the loop). dirty() is 1 when it changed and no take
 * is going into it, whether it plays or not; then h1_loop gives it, and saved() takes the dirt off what was given.
 * h1_loop is the main loop's, and may be called while the loop plays: it only reads what is recorded. The blob
 * is the engine's own memory, not a copy, and stays a loadable loop until a take records again. */
const void *h1_loop(uint32_t *size);
int h1_loop_load(const void *blob, uint32_t size);   /* 0 = taken; anything else leaves the loop empty */
int h1_loop_dirty(void);
/* The loop sent out to a computer as a .mid's makings (0.3.1, app.c over SysEx): the same blob as h1_loop's,
 * without moving what h1_loop_saved counts as saved, and what a file needs besides: H1_META_N bytes,
 * [0] 1 (this layout), [1] tempo, [2] meter (SEQ's METER), [3] bars (SEQ's BARS, 2^n), [4] swing, [5] how many
 * instruments, [6..21] each layer's part (0 none; else 1 + instrument * 8 + algorithm), [22..37] each
 * layer's volume cut (0..100), [38..53] each layer's mute and solo (1, 2). */
#define H1_META_N 54
const void *h1_loop_export(uint32_t *size, uint8_t meta[H1_META_N]);
int h1_loop_quiet(void);                         /* 1: the loop is stopped (or empty), no take going */
void h1_loop_saved(void);
#define H1_LOOP_MAX 16396u                       /* 12 + 8 x 2048 */
uint32_t h1_loop_gen(void);                     /* moves whenever what is recorded changes */
int h1_loop_room(void);                          /* notes that still fit in the loop (0: full) */

/* USB host: OCT- and OCT+ held together for a second turn the USB port into a host for a USB-MIDI keyboard,
 * and again back to the computer. The core keeps the choice (not saved: every start is a device) and calls
 * fn(on) from h1_frame; the port switches the port and tells the core what it found with h1_toast. With no
 * fn the hold says NO USB HOST. h1_usb_host: 1 while the core has asked for host. */
void h1_usb_host_fn(void (*fn)(int on));
int h1_usb_host(void);
void h1_toast(const char *text);                 /* a word from the port, shown as a toast (upper case, 16 at most) */

/* introspection for the simulator and tests */
int h1_instrument(void);
int h1_algorithm(void);                          /* of the current instrument, 0..7 (LAVA 0..8) */
int h1_page(void);                               /* 0 = home, else a settings page */
const char *h1_toast_now(void);                  /* the toast showing, "" when none */
int h1_layers(void);                             /* 1 while LAYERS is on: recorded notes keep their instrument */
int h1_instrument_volume(int inst);              /* 0..100: the instrument's own volume (EDIT held, and SELECT) */
int h1_volume_ring(void);                        /* the volume ring: 0 not up, 1 GLOBAL (MASTER), 2 INSTRUMENT */
int h1_instrument_count(void);
const char *h1_instrument_name(int i);
const char *h1_knob_name(int inst, int k);       /* the four home-screen modulations */
const char *h1_algo_name(int inst, int a);       /* eight per instrument, LAVA nine; "" past the last */
void h1_ink(int inst, uint8_t rgb[3]);
void h1_set_ink(int inst, uint8_t r, uint8_t g, uint8_t b);
uint32_t h1_voices_active(void);
int h1_state_asked(void);                         /* SAVE was pressed: save at once, silent or not */
int h1_silent(void);                             /* no voice, and the output silent for half a second: flash may stall the sound */
void h1_load(int pct);                           /* the port: the share of the block's time the last render took, for the CPU guard */
int h1_screen(void);                             /* SETUP's SCREEN: 0 AUTO, 1 FAST, 2 SAFE (the port sets the panel's clock) */
int h1_midi_channel(void);                       /* MIDI's CHAN: 0 every channel (out on 1), else 1..16, in and out */

/* Every encoder as a number: returns what it is set to right now and (where the pointers are not
 * null) its range, the name of what it is tied to and its value as text. The simulator draws its
 * sliders from this; set() moves a control there outright, the way a slider wants. */
int h1_control(int enc, int *lo, int *hi, const char **name, const char **text);
void h1_control_set(int enc, int value);
