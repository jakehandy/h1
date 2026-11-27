/* h1 as an .rfm1: the core compiled to WebAssembly behind the fm1core ABI (version 6), the format
 * FM-1 host apps load (RemoteFM1IOS docs/RFM1-FORMAT.md). The same file runs in the FM-1 browser
 * simulator (github.com/jakehandy/fm1-browser-simulator), RemoteFM1 and anything else that hosts .rfm1.
 *
 * Like the other ports this one only moves bytes. The host owns the clock: tick(ms) every device
 * millisecond, frame() every 15 ms or idle(), audio() for each 128-frame block at 44.1 kHz. Input
 * arrives in the panel struct between calls; the picture leaves through the panel's lcd array
 * (RGB565, high byte first) and the lights through its led matrix.
 *
 * Besides the format's exports this module has the simulator's optional "sim1" extension
 * (rfm1_sim1_*): every encoder as a value with a range, a name and a text (h1_control), so a host can
 * draw sliders and knobs that show what each control is tied to, and the current instrument's ink
 * as an accent colour. A host that does not know the extension ignores it. */
#include "../../core/h1.h"

#define EXPORT(name) __attribute__((used, visibility("default"), export_name(#name)))
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

#ifndef H1_RELEASE
#define H1_RELEASE "dev"
#endif
#ifndef H1_SOURCE_URL
#define H1_SOURCE_URL "https://github.com/jakehandy/h1"
#endif

/* ---- the ABI's two structs, laid out as fm1core.h (ABI 6) lays them out ---------------------- */
#define NKEY 27
#define NBTN 14
#define NENC 7
#define NCOL 11
#define NLINE 4
#define NB 14                            /* button labels: FX SEL ENV LFO EDIT GLO HOME SAVE ARP SEQ PLAY REC OCT- OCT+ */
#define NE 8                             /* knob roles: SELECT ALGORITHM PRESETS K1..K4, then MASTER */
#define FLASH_SIZE 0x100000u
#define LOOP_AT 0x8000u                  /* (the state from 0, the loop from here) */
#define ABI 6u

typedef struct {
    volatile uint32_t keys, buttons;
    volatile uint32_t keys_tap, buttons_tap;
    volatile int32_t enc[NENC];
    volatile int32_t master;             /* 0..1023 */
    volatile uint8_t key_vel[NKEY];
    volatile uint8_t key_lean[NKEY];
    uint8_t led[NCOL][NLINE];
    uint8_t led_play_green;
    uint16_t lcd[H1_W * H1_H];
    volatile uint32_t lcd_writes;
    uint8_t btn_id[NB];
    uint8_t enc_id[NE - 1];
    int8_t enc_dir[NE - 1];
    uint8_t ready;
} panel_t;

typedef struct {
    uint32_t abi_version;
    const char *id, *name, *version, *source_url;
    const char *family, *license, *gated;
    uint32_t flash_size;
    const char *button_names[NB];
    const void *params;
    uint32_t nparams;
    panel_t *panel;
    const int8_t *keymap;
    void (*options)(const char *, int, int, int);
    int (*boot_options)(int, const char *, int);
    void (*init)(int);
    void (*tick)(uint32_t);
    void (*frame)(void);
    void (*idle)(void);
    void (*audio)(int16_t *, uint32_t);
    int (*midi_in)(uint32_t);
    int (*midi_out_take)(uint32_t *);
    void (*dump)(void);
    void (*stats)(uint32_t *, uint32_t *);
    void (*ui_info)(char *, uint32_t);
    void (*shutdown)(void);
    int (*halted)(void);
    int (*flash_stage)(const uint8_t *, uint32_t);
    uint8_t *(*flash)(void);
    uint32_t (*flash_dirty)(void);
    int (*flash_sync)(void);
    uint32_t (*param_epoch)(void);
    int32_t (*knob_target)(int);
    const char *credits;
} core_t;

/* The FM-1's switch matrix: key id at (row, column); 0..13 are buttons, 14..40 the note keys. LED
 * line l of a column is row l + 1. Buttons and encoders by label, as the stock panel wires them. */
static const int8_t KEYMAP[6][NCOL] = {
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    { 5, 11,  4, 10,  3,  9,  2,  8, -1, -1, -1},
    {34, 35, 36, 37, 38, 40, 39, 13,  7,  6, 12},
    {23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33},
    { 0,  1, 15, 14, 17, 16, 19, 18, 20, 21, 22},
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
};
static const uint8_t BTN_ID[NB] = {2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 0, 1};
static const uint8_t ENC_ID[NE - 1] = {0, 1, 6, 2, 3, 4, 5};

static panel_t panel;
static uint8_t image[FLASH_SIZE];
static uint8_t led_pos[NB + NKEY];       /* (column << 2) | line for each label, then each key; 0xFF none */
static uint32_t booted, flash_writes, ms_now;

/* ---- the flash image: h1's saved state at 0, framed so a foreign image reads as empty -------- */
#define MAGIC 0x31534831u                /* "1HS1" */

static uint32_t crc32(const uint8_t *p, uint32_t n)
{
    uint32_t c = ~0u;
    int k;
    while (n--) {
        c ^= *p++;
        for (k = 0; k < 8; k++)
            c = c & 1u ? 0xEDB88320u ^ (c >> 1) : c >> 1;
    }
    return ~c;
}
static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static uint32_t get32(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

static void state_out(void)
{
    uint32_t n, i;
    const uint8_t *s = (const uint8_t *)h1_state(&n);
    if (n > LOOP_AT - 16u)
        return;
    put32(image, MAGIC);
    put32(image + 4, n);
    for (i = 0; i < n; i++)
        image[8 + i] = s[i];
    put32(image + 8 + n, crc32(image, 8 + n));
    h1_state_saved();
    flash_writes++;
}
/* the loop recorder's loop, as the device keeps it in a store object of its own: here at LOOP_AT */
static void loop_out(void)
{
    uint32_t n, sn, i;
    const uint8_t *s = (const uint8_t *)h1_loop(&n);
    const uint8_t *g = (const uint8_t *)h1_song(&sn);   /* (the steps and the layers after it, as the device's ring) */
    if (!n || n + sn > 0x8000u - 16u)
        return;
    for (i = 0; i < sn; i++)
        image[LOOP_AT + 8 + n + i] = g[i];
    for (i = 0; i < n; i++)
        image[LOOP_AT + 8 + i] = s[i];
    n += sn;
    put32(image + LOOP_AT, MAGIC ^ 0x4C4F4F50u);
    put32(image + LOOP_AT + 4, n);
    put32(image + LOOP_AT + 8 + n, crc32(image + LOOP_AT, 8 + n));
    h1_loop_saved();
    flash_writes++;
}
static void state_in(void)
{
    uint32_t n = get32(image + 4);
    if (get32(image) == MAGIC && n <= LOOP_AT - 16u && get32(image + 8 + n) == crc32(image, 8 + n))
        h1_state_load(image + 8, n);     /* refused by the core unless it is this version's or an older one it knows */
    n = get32(image + LOOP_AT + 4);      /* then the loop, which the state's load empties (and kept with no state
                                          * written: a loop recorded before any setting changed) */
    if (get32(image + LOOP_AT) == (MAGIC ^ 0x4C4F4F50u) && n <= 0x8000u - 16u &&
        get32(image + LOOP_AT + 8 + n) == crc32(image + LOOP_AT, 8 + n))
        h1_loop_load(image + LOOP_AT + 8, n);
}

static int c_flash_stage(const uint8_t *bytes, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < FLASH_SIZE; i++)
        image[i] = bytes && i < n ? bytes[i] : 0xFF;
    return 1;
}
static uint8_t *c_flash(void) { return image; }
static uint32_t c_flash_dirty(void) { return flash_writes; }
static int c_flash_sync(void)
{
    if (booted && h1_state_dirty())
        state_out();
    if (booted && h1_loop_dirty())
        loop_out();
    return 1;
}

/* ---- the panel ------------------------------------------------------------------------------ */
static uint32_t keys_was, btn_was;
static uint32_t gain_q12 = 2560;         /* MASTER's gain, 4096 = unity */

static void input(void)
{
    uint32_t keys = panel.keys | panel.keys_tap, raw = panel.buttons | panel.buttons_tap, btn = 0, k;
    uint32_t tapk = panel.keys_tap & ~panel.keys, tapb = 0;
    int32_t m = panel.master;
    for (k = 0; k < NB; k++) {
        if (raw >> BTN_ID[k] & 1u) btn |= 1u << k;
        if ((panel.buttons_tap & ~panel.buttons) >> BTN_ID[k] & 1u) tapb |= 1u << k;
    }
    panel.keys_tap = panel.buttons_tap = 0;
    for (k = 0; k < NKEY; k++)
        if ((keys ^ keys_was) >> k & 1u)
            h1_key((int)k, (int)(keys >> k & 1u));
    for (k = 0; k < NB; k++)
        if ((btn ^ btn_was) >> k & 1u)
            h1_button((int)k, (int)(btn >> k & 1u));     /* the labels and H1_BTN_* are the same order */
    keys_was = keys & ~tapk;             /* a tap was down for this millisecond only */
    btn_was = btn & ~tapb;
    for (k = 0; k < NKEY; k++)
        if (tapk >> k & 1u) h1_key((int)k, 0);
    for (k = 0; k < NB; k++)
        if (tapb >> k & 1u) h1_button((int)k, 0);
    for (k = 0; k < NE - 1; k++) {
        int32_t d = panel.enc[ENC_ID[k]];
        if (d) {
            panel.enc[ENC_ID[k]] = 0;
            h1_encoder((int)k, (int)d);  /* the roles and H1_ENC_* are the same order too */
        }
    }
    if (m < 0) m = 0;
    if (m > 1023) m = 1023;
    h1_master((int)(m * 1000 / 1023));   /* the picture shows it while it turns */
    if (h1_global_volume() >= 0)         /* (held: the knob was the instrument's and has not come back) */
        m = h1_global_volume() * 1023 / 1000;
    gain_q12 = (uint32_t)(m * m) >> 8;   /* the square law fm1-base gives MASTER: m * m >> 12 on 0..4096 */
}

static void lights(void)
{
    uint32_t b = h1_button_leds(), kl = h1_key_leds(), i;
    memset(panel.led, 0, sizeof panel.led);
    for (i = 0; i < NB + NKEY; i++) {
        uint8_t q = led_pos[i];
        int on = i < NB ? (int)(b >> i & 1u) : (int)(kl >> (i - NB) & 1u);
        if (q != 0xFF && on)
            panel.led[q >> 2][q & 3u] = 255;
    }
}

/* the core's palette into RGB565 as the panel is sent it, high byte first */
static uint16_t pal_be[256];
static void blit(int x, int y, int w, int h)
{
    const uint8_t *fb = h1_framebuffer();
    int i, j;
    for (j = y; j < y + h; j++)
        for (i = x; i < x + w; i++)
            panel.lcd[j * H1_W + i] = pal_be[fb[j * H1_W + i]];
}
static uint32_t link_bytes;
static void picture(void)
{
    const uint16_t *pal = h1_palette();
    uint32_t i;
    for (i = 0; i < 256u; i++)
        pal_be[i] = (uint16_t)((pal[i] >> 8) | (pal[i] << 8));
    h1_frame(ms_now);
    link_bytes = h1_present(blit);
    if (link_bytes)
        panel.lcd_writes++;
}

/* ---- the entry points ----------------------------------------------------------------------- */
static void c_options(const char *path, int no_flash, int save, int headless)
{
    (void)path; (void)no_flash; (void)save; (void)headless;
}
static int c_boot_options(int fail, const char *reason, int stage) { (void)fail; (void)reason; (void)stage; return 1; }
static void c_init(int demo)
{
    uint32_t i, r, c;
    (void)demo;
    for (i = 0; i < NB + NKEY; i++) {
        int id = i < NB ? BTN_ID[i] : 14 + (int)(i - NB);
        led_pos[i] = 0xFF;
        for (r = 1; r <= NLINE; r++)
            for (c = 0; c < NCOL; c++)
                if (KEYMAP[r][c] == id)
                    led_pos[i] = (uint8_t)(c << 2 | (r - 1u));
    }
    memcpy(panel.btn_id, BTN_ID, sizeof panel.btn_id);
    memcpy(panel.enc_id, ENC_ID, sizeof panel.enc_id);
    memset(panel.enc_dir, 1, sizeof panel.enc_dir);
    if (!panel.master)
        panel.master = 820;
    h1_init(44100);
    state_in();
    keys_was = btn_was = 0;
    ms_now = 0;
    booted = 1;
    input();
    picture();
    lights();
}
static void c_tick(uint32_t ms)
{
    if (!booted) return;
    ms_now = ms;
    input();
}
static void c_frame(void)
{
    if (!booted) return;
    picture();
    lights();
}
static void c_idle(void)
{
    if (!booted) return;
    if (h1_state_dirty())
        state_out();                     /* the device would write its store now */
    else if (h1_loop_dirty())
        loop_out();
}
#define CHUNK 512u
static int32_t abuf[CHUNK * 2];
static void c_audio(int16_t *out, uint32_t frames)
{
    while (frames) {
        uint32_t n = frames < CHUNK ? frames : CHUNK, i;
        if (booted) h1_render(abuf, n);
        else memset(abuf, 0, sizeof abuf);
        for (i = 0; i < n * 2u; i++) {
            int64_t s = ((int64_t)abuf[i] * gain_q12) >> 20;   /* 24-bit, through MASTER, to 16 */
            out[i] = (int16_t)(s > 32767 ? 32767 : s < -32768 ? -32768 : s);
        }
        out += n * 2u;
        frames -= n;
    }
}
static int c_midi_in(uint32_t pkt)
{
    uint32_t cin = pkt & 0xFu;
    if (cin >= 0x8u && cin <= 0xEu)
        h1_midi((uint8_t)(pkt >> 8), (uint8_t)(pkt >> 16), (uint8_t)(pkt >> 24));
    return 1;
}
static int c_midi_out_take(uint32_t *pkt) { (void)pkt; return 0; }
static void c_dump(void) {}
static void c_stats(uint32_t *shed, uint32_t *cpu) { if (shed) *shed = 0; if (cpu) *cpu = 0; }
static void put_str(char *buf, uint32_t n, const char *s)
{
    uint32_t i = 0;
    if (!buf || !n) return;
    while (s[i] && i + 1 < n) { buf[i] = s[i]; i++; }
    buf[i] = 0;
}
static void c_ui_info(char *buf, uint32_t n) { put_str(buf, n, booted ? h1_instrument_name(h1_instrument()) : "h1"); }
static void c_shutdown(void)
{
    if (booted && h1_state_dirty()) state_out();
    if (booted && h1_loop_dirty()) loop_out();
}
static int c_halted(void) { return 0; }

static const core_t core = {
    ABI, "h1", "h1", H1_RELEASE, H1_SOURCE_URL, "h1", "MIT", 0, FLASH_SIZE,
    {"FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC", "OCT-", "OCT+"},
    0, 0, &panel, &KEYMAP[0][0],
    c_options, c_boot_options, c_init, c_tick, c_frame, c_idle, c_audio, c_midi_in, c_midi_out_take, c_dump,
    c_stats, c_ui_info, c_shutdown, c_halted, c_flash_stage, c_flash, c_flash_dirty, c_flash_sync, 0, 0,
    "h1 by Jake Handy.",
};

/* ---- the format's exports ------------------------------------------------------------------- */
EXPORT(rfm1_version) int32_t rfm1_version(void) { return 1; }
EXPORT(rfm1_get) const core_t *rfm1_get(uint32_t abi) { return abi == ABI ? &core : 0; }
EXPORT(rfm1_layout) int32_t rfm1_layout(int32_t i)
{
#define P(f) (int32_t)__builtin_offsetof(panel_t, f)
#define C(f) (int32_t)__builtin_offsetof(core_t, f)
    static const int32_t L[] = {
        sizeof(panel_t), P(keys), P(buttons), P(keys_tap), P(buttons_tap), P(enc), P(master), P(key_vel),
        P(key_lean), P(led), P(led_play_green), P(lcd), P(lcd_writes), P(btn_id), P(enc_id), P(enc_dir), P(ready),
        sizeof(core_t), C(abi_version), C(id), C(name), C(version), C(source_url), C(family), C(license), C(gated),
        C(flash_size), C(button_names), C(params), C(nparams), C(panel), C(keymap), C(options), C(credits),
    };
#undef P
#undef C
    return i >= 0 && i < (int32_t)(sizeof L / sizeof L[0]) ? L[i] : -1;
}
/* buffers the host passes in (a path, a staged flash image of up to 1 MiB): a bump allocator over the
 * memory past the linker's heap base, which the fixed 4 MiB memory leaves free */
extern unsigned char __heap_base;
static uint32_t heap_top;
EXPORT(rfm1_alloc) void *rfm1_alloc(uint32_t n)
{
    uint32_t end = (uint32_t)__builtin_wasm_memory_size(0) * 65536u, p;
    if (!heap_top) heap_top = ((uint32_t)&__heap_base + 15u) & ~15u;
    n = (n + 15u) & ~15u;
    if (!n || n > end - heap_top) return 0;
    p = heap_top;
    heap_top += n;
    return (void *)p;
}
EXPORT(rfm1_free) void rfm1_free(void *p) { (void)p; }   /* the pool is never reused: a host allocates a handful of buffers once */
EXPORT(rfm1_options) void rfm1_options(const char *path, int no_flash, int save, int headless) { c_options(path, no_flash, save, headless); }
EXPORT(rfm1_boot_options) int32_t rfm1_boot_options(int fail, const char *reason, int stage) { return c_boot_options(fail, reason, stage); }
EXPORT(rfm1_init) void rfm1_init(int demo) { c_init(demo); }
EXPORT(rfm1_tick) void rfm1_tick(uint32_t ms) { c_tick(ms); }
EXPORT(rfm1_frame) void rfm1_frame(void) { c_frame(); }
EXPORT(rfm1_idle) void rfm1_idle(void) { c_idle(); }
EXPORT(rfm1_audio) void rfm1_audio(int16_t *out, uint32_t frames) { c_audio(out, frames); }
EXPORT(rfm1_midi_in) int32_t rfm1_midi_in(uint32_t pkt) { return c_midi_in(pkt); }
EXPORT(rfm1_midi_out_take) int32_t rfm1_midi_out_take(uint32_t *pkt) { return c_midi_out_take(pkt); }
EXPORT(rfm1_halted) int32_t rfm1_halted(void) { return c_halted(); }
EXPORT(rfm1_shutdown) void rfm1_shutdown(void) { c_shutdown(); }
EXPORT(rfm1_flash_stage) int32_t rfm1_flash_stage(const uint8_t *bytes, uint32_t n) { return c_flash_stage(bytes, n); }
EXPORT(rfm1_flash) uint8_t *rfm1_flash(void) { return c_flash(); }
EXPORT(rfm1_flash_dirty) int32_t rfm1_flash_dirty(void) { return (int32_t)c_flash_dirty(); }
EXPORT(rfm1_flash_sync) int32_t rfm1_flash_sync(void) { return c_flash_sync(); }
EXPORT(rfm1_stats) void rfm1_stats(uint32_t *shed, uint32_t *cpu) { c_stats(shed, cpu); }
EXPORT(rfm1_ui_info) void rfm1_ui_info(char *buf, uint32_t n) { c_ui_info(buf, n); }

/* ---- sim1: the simulator's optional extension ------------------------------------------------
 * rfm1_sim1_ctl(role, out) fills out with {int32 value, lo, hi; char name[32]; char text[32]} for the
 * knob role (SELECT ALGORITHM PRESETS K1..K4) and returns 1 when the role has a value a slider can
 * set outright; 0 leaves it a relative encoder. rfm1_sim1_ctl_set(role, v) sets it. */
typedef struct { int32_t value, lo, hi; char name[32]; char text[32]; } sim1_ctl_t;
EXPORT(rfm1_sim1_version) int32_t rfm1_sim1_version(void) { return 1; }
EXPORT(rfm1_sim1_ctl) int32_t rfm1_sim1_ctl(int32_t role, sim1_ctl_t *out)
{
    const char *nm = "", *tx = "";
    int lo = 0, hi = 0;
    if (!booted || role < 0 || role >= NE - 1 || !out) return 0;
    out->value = h1_control(role, &lo, &hi, &nm, &tx);
    out->lo = lo;
    out->hi = hi;
    put_str(out->name, sizeof out->name, nm);
    put_str(out->text, sizeof out->text, tx);
    return hi > lo;
}
EXPORT(rfm1_sim1_ctl_set) void rfm1_sim1_ctl_set(int32_t role, int32_t v)
{
    if (booted && role >= 0 && role < NE - 1) h1_control_set(role, v);
}
EXPORT(rfm1_sim1_accent) int32_t rfm1_sim1_accent(void)
{
    uint8_t c[3];
    if (!booted) return -1;
    h1_ink(h1_instrument(), c);
    return ((int32_t)c[0] << 16) | ((int32_t)c[1] << 8) | c[2];
}
EXPORT(rfm1_sim1_link_bytes) int32_t rfm1_sim1_link_bytes(void) { return (int32_t)link_bytes; }
