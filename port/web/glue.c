/* H1 web port: the core compiled to WebAssembly for the browser simulator. No libc: the three
 * memory routines the compiler may call are here, and everything the page needs is exported
 * with a w_ prefix. The page owns the canvas, the Web Audio node and the panel. */
#include "../../core/h1.h"

#define EXPORT(name) __attribute__((export_name(#name)))
typedef unsigned long size_t_;

void *memset(void *d, int c, size_t_ n)
{
    unsigned char *p = (unsigned char *)d;
    while (n--) *p++ = (unsigned char)c;
    return d;
}
void *memcpy(void *d, const void *s, size_t_ n)
{
    unsigned char *p = (unsigned char *)d;
    const unsigned char *q = (const unsigned char *)s;
    while (n--) *p++ = *q++;
    return d;
}
void *memmove(void *d, const void *s, size_t_ n)
{
    unsigned char *p = (unsigned char *)d;
    const unsigned char *q = (const unsigned char *)s;
    if (p < q) while (n--) *p++ = *q++;
    else { p += n; q += n; while (n--) *--p = *--q; }
    return d;
}

#define MAXN 4096
static int32_t abuf[MAXN * 2];
static float fbuf[MAXN * 2];
static uint8_t rgba[H1_W * H1_H * 4];
static uint8_t scratch[2048];
static uint32_t rects, rect_px;

static void count_rect(int x, int y, int w, int h)
{
    (void)x; (void)y;
    rects++;
    rect_px += (uint32_t)(w * h);
}

EXPORT(w_init) void w_init(uint32_t sr) { h1_init(sr); }
EXPORT(w_key) void w_key(int k, int down) { h1_key(k, down); }
EXPORT(w_button) void w_button(int b, int down) { h1_button(b, down); }
EXPORT(w_encoder) void w_encoder(int e, int d) { h1_encoder(e, d); }
EXPORT(w_midi) void w_midi(int st, int d1, int d2) { h1_midi((uint8_t)st, (uint8_t)d1, (uint8_t)d2); }

/* n stereo frames as interleaved floats, -1..1 */
EXPORT(w_render) float *w_render(uint32_t n)
{
    uint32_t i;
    if (n > MAXN) n = MAXN;
    h1_render(abuf, n);
    for (i = 0; i < n * 2; i++)
        fbuf[i] = (float)abuf[i] * (1.0f / 8388608.0f);
    return fbuf;
}

/* one frame: returns the bytes the device's panel would have been sent */
EXPORT(w_frame) uint32_t w_frame(uint32_t now_ms)
{
    const uint8_t *fb;
    const uint16_t *pal;
    uint32_t bytes, i;
    h1_frame(now_ms);
    rects = rect_px = 0;
    bytes = h1_present(count_rect);
    fb = h1_framebuffer();
    pal = h1_palette();
    for (i = 0; i < H1_W * H1_H; i++) {
        uint16_t c = pal[fb[i]];
        uint32_t r = (c >> 11) & 31u, g = (c >> 5) & 63u, b = c & 31u;
        rgba[4 * i] = (uint8_t)((r << 3) | (r >> 2));
        rgba[4 * i + 1] = (uint8_t)((g << 2) | (g >> 4));
        rgba[4 * i + 2] = (uint8_t)((b << 3) | (b >> 2));
        rgba[4 * i + 3] = 255;
    }
    return bytes;
}
EXPORT(w_rgba) uint8_t *w_rgba(void) { return rgba; }
EXPORT(w_rects) uint32_t w_rects(void) { return rects; }
EXPORT(w_key_leds) uint32_t w_key_leds(void) { return h1_key_leds(); }
EXPORT(w_button_leds) uint32_t w_button_leds(void) { return h1_button_leds(); }
EXPORT(w_voices) uint32_t w_voices(void) { return h1_voices_active(); }

EXPORT(w_instrument) int w_instrument(void) { return h1_instrument(); }
EXPORT(w_algorithm) int w_algorithm(void) { return h1_algorithm(); }
EXPORT(w_page) int w_page(void) { return h1_page(); }
EXPORT(w_instrument_count) int w_instrument_count(void) { return h1_instrument_count(); }
/* kind 0: instrument name, 1: knob name, 2: algorithm name */
EXPORT(w_name) const char *w_name(int kind, int inst, int i)
{
    return kind == 0 ? h1_instrument_name(inst) : (kind == 1 ? h1_knob_name(inst, i) : h1_algo_name(inst, i));
}
EXPORT(w_ink) uint32_t w_ink(int inst)
{
    uint8_t c[3];
    h1_ink(inst, c);
    return ((uint32_t)c[0] << 16) | ((uint32_t)c[1] << 8) | c[2];
}
EXPORT(w_set_ink) void w_set_ink(int inst, uint32_t rgb) { h1_set_ink(inst, (uint8_t)(rgb >> 16), (uint8_t)(rgb >> 8), (uint8_t)rgb); }

/* every encoder as a number, for the page's sliders */
EXPORT(w_ctl) int w_ctl(int e) { return h1_control(e, 0, 0, 0, 0); }
EXPORT(w_ctl_lo) int w_ctl_lo(int e) { int v = 0; h1_control(e, &v, 0, 0, 0); return v; }
EXPORT(w_ctl_hi) int w_ctl_hi(int e) { int v = 0; h1_control(e, 0, &v, 0, 0); return v; }
EXPORT(w_ctl_name) const char *w_ctl_name(int e) { const char *s = ""; h1_control(e, 0, 0, &s, 0); return s; }
EXPORT(w_ctl_text) const char *w_ctl_text(int e) { const char *s = ""; h1_control(e, 0, 0, 0, &s); return s; }
EXPORT(w_ctl_set) void w_ctl_set(int e, int v) { h1_control_set(e, v); }

/* saved state, the way the device keeps it in flash */
EXPORT(w_scratch) uint8_t *w_scratch(void) { return scratch; }
EXPORT(w_state_size) uint32_t w_state_size(void) { uint32_t n; h1_state(&n); return n; }
EXPORT(w_state_save) uint8_t *w_state_save(void)
{
    uint32_t n, i;
    const uint8_t *p = (const uint8_t *)h1_state(&n);
    for (i = 0; i < n && i < sizeof scratch; i++)
        scratch[i] = p[i];
    h1_state_saved();
    return scratch;
}
EXPORT(w_state_load) int w_state_load(uint32_t n) { return h1_state_load(scratch, n); }
EXPORT(w_state_dirty) int w_state_dirty(void) { return h1_state_dirty(); }
