/* h1 on the FM-1 (fm1-base). The whole firmware is the portable core in ../core; this file only
 * passes the panel in, and the sound, the lights and the changed parts of the picture out. */
#include "app.h"
#include "../core/h1.h"

/* ---- the screen: 8-bit ink through the palette into RGB565, band by band -------------------- */
#define BAND_ROWS 20                     /* the core never asks for a taller rectangle */
static uint16_t band[2][240 * BAND_ROWS] PLAT_POOL;   /* two, so one can go out while the next is filled */
static uint16_t pal_be[256];             /* the palette as the panel wants it: big-endian */
static uint16_t pal_seen[2];
static uint32_t band_i;

static void palette_sync(void)
{
    const uint16_t *pal = h1_palette();
    uint32_t i;
    if (pal[255] == pal_seen[0] && pal[128] == pal_seen[1])
        return;
    pal_seen[0] = pal[255];
    pal_seen[1] = pal[128];
    for (i = 0; i < 256u; i++)
        pal_be[i] = (uint16_t)((pal[i] >> 8) | (pal[i] << 8));
}

/* The panel takes a full screen in 21 ms, as long as a frame takes to draw. So a frame goes out while
 * the next one is drawn: the core queues what changed (h1_present_begin) and calls send_some from inside
 * its drawing, many times a frame. Each call does one small thing: fills the free band from the next
 * queued rectangle, or hands a filled band to the panel if the panel has finished with the last one. */
static int sq[4];                        /* the rectangle in the filled band: x, y, w, h */
static uint8_t sq_full;
static struct { uint32_t bytes, rects; } sent;

#ifndef PLAT_HOST
#include "hw/panel.h"
#endif

static int panel_idle(void)
{
#ifdef PLAT_HOST
    return 1;
#else
    return !lcd_busy || hw_panel_done();     /* (fm1-base's device/lcd.c: no transfer running, or it is done) */
#endif
}

static void band_fill(void)
{
    const uint8_t *src;
    uint16_t *dst = band[band_i];
    int i, j;
    if (!h1_present_next(&sq[0], &sq[1], &sq[2], &sq[3]))
        return;
    src = h1_panel() + sq[1] * H1_W + sq[0];
    for (j = 0; j < sq[3]; j++, src += H1_W)
        for (i = 0; i < sq[2]; i++)
            *dst++ = pal_be[src[i]];
    sq_full = 1;
}

static void band_send(void)              /* (waits for the panel if it is still busy) */
{
    lcd_blit((uint32_t)sq[0], (uint32_t)sq[1], (uint32_t)sq[2], (uint32_t)sq[3], band[band_i]);
    band_i ^= 1u;                        /* the other band is free: the transfer before this one is over */
    sq_full = 0;
}

static void send_some(void)
{
    if (!sq_full)
        band_fill();
    else if (panel_idle())
        band_send();
}

static void send_rest(void)
{
    for (;;) {
        if (!sq_full)
            band_fill();
        if (!sq_full)
            return;
        band_send();
    }
}

/* ---- how long things take ---------------------------------------------------------------------
 * Measured all the time (three clock reads a frame, two a block of sound). MIDI CC 119 from the
 * computer makes a listening screen reader say the numbers since the last time it was asked:
 *   PERF. FPS n. DRAW avg MAX max. SEND avg MAX max. AUDIO avg MAX max. KB n RECTS n. STACK main interrupt
 * DRAW and SEND are milliseconds a frame (the picture drawn, with most of the frame before it sent meanwhile;
 * then the rest of that frame sent and this one's changes found); KB and RECTS are what a frame sends;
 * AUDIO is the share of each block's time the sound took, in percent; STACK is the bytes of the main
 * loop's and the interrupts' stacks that have never been used since the start. */
#define US(t) ((t) / PLAT_TICKS_PER_US)
static struct { uint32_t frames, draw, draw_max, send, send_max, since_ms; } pf;
static volatile uint32_t pf_blocks, pf_audio, pf_audio_max, pf_block_us = 5805u;
static uint8_t pf_ask;

/* ---- the panel ------------------------------------------------------------------------------ */
static uint32_t keys_was, btn_was;

/* MIDI in (USB and the TRS jack): read here, in the main loop, because the core's event queue has one
 * writer and the panel is it */
static void midi_in(void)
{
    uint32_t pkt;
    int port;
    while (plat_midi_in(&pkt, &port)) {
        if (((pkt >> 8) & 0xF0u) == 0xB0u && ((pkt >> 16) & 0xFFu) == 119u)
            pf_ask = 1;                                     /* (see "how long things take") */
        else
            h1_midi((uint8_t)(pkt >> 8), (uint8_t)(pkt >> 16), (uint8_t)(pkt >> 24));
    }
}

static void speak(void);

static void input(void)
{
    uint32_t keys = plat_keys(), btn = plat_buttons(), k;
    for (k = 0; k < NKEYS; k++)
        if ((keys ^ keys_was) >> k & 1u)
            h1_key((int)k, (int)(keys >> k & 1u));
    for (k = 0; k < NB; k++)
        if ((btn ^ btn_was) >> k & 1u)
            h1_button((int)k, (int)(btn >> k & 1u));       /* B_* and H1_BTN_* are the same order */
    for (k = 0; k < NE; k++) {
        int32_t d = plat_enc((int)k);                       /* EN_* and H1_ENC_* too */
        if (d)
            h1_encoder((int)k, (int)d);
    }
    keys_was = keys;
    btn_was = btn;
    midi_in();
}

/* ---- fm1-base's calls ----------------------------------------------------------------------- */
/* A start that crashes or hangs in its first 30 seconds is counted by the platform, and two in a row are
 * safe mode. After one, the next start leaves the saved state alone and comes up as new, so nothing h1
 * has saved can keep it from starting. (The save itself stays until the next change is saved over it.) */
#ifdef PLAT_HOST
static int host_bad_start;                                  /* the tests' "badstart": the next start is one */
int app_host_cmd(const char *cmd, const char *a, const char *b, const char *c, const char *d)
{
    (void)a; (void)b; (void)c; (void)d;
    if (strcmp(cmd, "badstart"))
        return 0;
    host_bad_start = 1;
    return 1;
}
static int last_start_failed(void)
{
    int f = host_bad_start;
    host_bad_start = 0;
    return f;
}
#else
static int last_start_failed(void) { return bootguard.failed != 0; }
#endif

/* The second core, in a build that has it (fm1.toml cpu2 = true; FM1B_CPU2=0 in the build's environment makes
 * one without): the core's render hands it every other voice. Where it does not start, or once it has been given up, the
 * first core does all of them, as in a build without it. The sound is the same either way. */
#ifndef FM1B_CPU2
#define FM1B_CPU2 0
#endif
#define TWO_CORES FM1B_CPU2
#if TWO_CORES
static int core2_run(void (*fn)(uint32_t), uint32_t arg) { return plat_cpu2_on() && plat_cpu2_run(fn, arg); }
static int core2_wait(void) { return plat_cpu2_wait(); }
#endif

/* MIDI out, to the computer over USB and to a computer or phone over Bluetooth (the FM-1's TRS jack is an input
 * only): the core's notes from the render, its knobs from the main loop; plat_midi_send takes both. Each link has
 * its own queue and is asked on its own, so a slow Bluetooth link never holds back USB. With nobody listening on a
 * link there is no room, and nothing is queued there. A note-on or a knob goes only while there is room to spare,
 * so a queue never fills with them and leaves a note-off out (a note the computer would then hold for ever).
 * In a build without Bluetooth, TO_BLE has no room and only USB is sent. */
#define MIDI_SPARE 16u
static void midi_send_to(uint32_t pkt, uint32_t to, int off)
{
    uint32_t room = plat_midi_out_room(to);
    if (off ? room > 0u : room > MIDI_SPARE)
        plat_midi_send(pkt, to);
}
static void midi_send(uint8_t status, uint8_t d1, uint8_t d2)
{
    uint32_t pkt = (uint32_t)(status >> 4) | (uint32_t)status << 8 | (uint32_t)d1 << 16 | (uint32_t)d2 << 24;
    /* a note-off, an all-notes-off and the clock's real-time messages (clock, Start, Stop: cable 0's CIN F, one
     * byte) go whenever there is a slot: a follower that misses a clock drifts, and one that misses a Stop runs on */
    int off = (status & 0xF0u) == 0x80u || ((status & 0xF0u) == 0xB0u && d1 == 123u) || status >= 0xF8u ||
              status == 0xFAu || status == 0xFCu;
    midi_send_to(pkt, TO_USB, off);
    midi_send_to(pkt, TO_BLE, off);
}

void app_init(void)
{
    static uint32_t blob[PLAT_STORE_MAX / 4];               /* words: the store fills it, the core reads it */
    uint32_t size;
    int n;
    h1_init(44100);
    h1_while_drawing(send_some);
    h1_midi_out(midi_send);
#if TWO_CORES
    if (plat_cpu2_set(1) == 0)                              /* (app_init is the one moment it can start) */
        h1_two_cores(core2_run, core2_wait);
#endif
    if (last_start_failed())
        return;
    h1_state(&size);
    n = size <= sizeof blob ? plat_store_load(OBJ_STATE, blob, sizeof blob) : -1;
    if (n > 0)
        h1_state_load(blob, (uint32_t)n);                   /* refused unless it is this version's, or one it knows */
    n = plat_store_load(OBJ_LOOP, blob, sizeof blob);       /* then the loop (the state's load empties it) */
    if (n > 0)
        h1_loop_load(blob, (uint32_t)n);
}

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    static float gain_now;
    int held = h1_global_volume();                          /* (the knob may be the instrument's just now) */
    float m = held < 0 ? (float)master_q12 * (1.0f / 4096.0f) : (float)held * 0.001f;
    float gain = m * m;                                     /* MASTER, as a square */
    uint32_t i, t0 = plat_ticks24(), us;
    h1_render(lr, n);                                       /* 24-bit, already limited */
    for (i = 0; i < n; i++) {
        gain_now += (gain - gain_now) * 0.004f;             /* ~5 ms: turning MASTER does not click */
        lr[2 * i] = (int32_t)((float)lr[2 * i] * gain_now);
        lr[2 * i + 1] = (int32_t)((float)lr[2 * i + 1] * gain_now);
    }
    us = US(plat_ticks24() - t0);
    pf_audio += us;
    pf_blocks++;
    if (us > pf_audio_max)
        pf_audio_max = us;
    pf_block_us = n * 1000000u / 44100u;
}

void app_panic(void) { h1_midi(0xB0, 123, 0); }

void app_splash(void) { lcd_fill(0, 0, 240, 240, 0); }

void app_ui_init(void) { lcd_fill(0, 0, 240, 240, 0); }

/* the MIDI page's BT: the radio on or off as it is set (it starts at power-on as it was left: APP_BLE_AT_BOOT) */
static void bluetooth_follow(void)
{
    static int was = -1;
    int on = h1_bluetooth();
    if (on != was) {
        if (was >= 0)
            plat_ble_enable(on);
        was = on;
    }
}

void app_frame(void)
{
    uint32_t t0, t1, t2;
    input();
    bluetooth_follow();
    h1_master((int)(plat_master() * 1000u / 4096u));        /* shown while it turns; the gain is app_render's */
    t0 = plat_ticks24();
    h1_frame(plat_ms());                                    /* (and most of the last frame goes out meanwhile) */
    t1 = plat_ticks24();
    send_rest();                                            /* what is left of the last frame */
    palette_sync();
    h1_present_begin();                                     /* this frame, queued: it goes out during the next */
    t2 = plat_ticks24();
    sent.bytes += h1_present_bytes();
    sent.rects += (uint32_t)h1_present_rects();
    pf.frames++;
    pf.draw += US(t1 - t0);
    pf.send += US(t2 - t1);
    if (US(t1 - t0) > pf.draw_max) pf.draw_max = US(t1 - t0);
    if (US(t2 - t1) > pf.send_max) pf.send_max = US(t2 - t1);
    plat_glow(1);
    plat_leds(h1_button_leds(), h1_key_leds());
    speak();
    /* saving erases flash, which stalls the sound for a moment: only when nothing is sounding, and one object a
     * frame. The loop only once it has stopped (h1_loop_dirty): stop it with PLAY and it is kept. */
    if (h1_state_dirty() && !h1_voices_active()) {
        uint32_t size;
        const void *p = h1_state(&size);
        if (plat_store_save(OBJ_STATE, p, size) == 0)
            h1_state_saved();
    } else if (h1_loop_dirty() && !h1_voices_active()) {
        uint32_t size;
        const void *p = h1_loop(&size);
        if (size && plat_store_save(OBJ_LOOP, p, size) == 0)
            h1_loop_saved();
    }
}

void app_input_only(void) { input(); }

/* ---- a screen reader (fm1-reader) ------------------------------------------------------------
 * The picture is not text, so the reader hears what the controls are set to: the instrument and
 * its algorithm when either changes, the four knobs' names when a page opens, and a control's
 * name and value once it has stopped turning. */
static char *say_cat(char *d, char *end, const char *s)
{
    while (s && *s && d < end)
        *d++ = *s++;
    *d = 0;
    return d;
}

static char *say_num(char *d, char *end, uint32_t v)
{
    char b[10];
    int n = 0;
    do
        b[n++] = (char)('0' + v % 10u);
    while ((v /= 10u) != 0u && n < 10);
    while (n && d < end)
        *d++ = b[--n];
    *d = 0;
    return d;
}

static void say_perf(void)
{
    char t[150], *d, *end = t + sizeof t - 1;
    uint32_t f, ms = plat_ms() - pf.since_ms, n = pf.frames ? pf.frames : 1u, nb, au, am;
    f = plat_lock();
    nb = pf_blocks; au = pf_audio; am = pf_audio_max;
    pf_blocks = pf_audio = pf_audio_max = 0;
    plat_unlock(f);
    d = say_cat(t, end, "PERF. FPS ");
    d = say_num(d, end, ms ? (pf.frames * 1000u + ms / 2u) / ms : 0u);
    d = say_cat(d, end, ". DRAW ");
    d = say_num(d, end, (pf.draw / n + 500u) / 1000u);
    d = say_cat(d, end, " MAX ");
    d = say_num(d, end, (pf.draw_max + 500u) / 1000u);
    d = say_cat(d, end, ". SEND ");
    d = say_num(d, end, (pf.send / n + 500u) / 1000u);
    d = say_cat(d, end, " MAX ");
    d = say_num(d, end, (pf.send_max + 500u) / 1000u);
    d = say_cat(d, end, ". AUDIO ");
    d = say_num(d, end, nb ? (au / nb) * 100u / pf_block_us : 0u);
    d = say_cat(d, end, " MAX ");
    d = say_num(d, end, am * 100u / pf_block_us);
    d = say_cat(d, end, ". KB ");                           /* sent to the panel a frame, and in how many rectangles */
    d = say_num(d, end, (sent.bytes / n + 512u) / 1024u);
    d = say_cat(d, end, " RECTS ");
    d = say_num(d, end, (sent.rects + n / 2u) / n);
    sent.bytes = sent.rects = 0;
    if (plat_stack_left(PLAT_STK_MAIN) >= 0 && plat_stack_left(PLAT_STK_IRQ) >= 0) {
        d = say_cat(d, end, ". STACK ");                    /* bytes never used since the start */
        d = say_num(d, end, (uint32_t)plat_stack_left(PLAT_STK_MAIN));
        d = say_cat(d, end, " ");
        d = say_num(d, end, (uint32_t)plat_stack_left(PLAT_STK_IRQ));
    }
#if TWO_CORES
    /* the cores making the sound, then: with two, how deep the second's stack has gone; with one, why
     * (plat_cpu2_gave_up) */
    d = say_cat(d, end, plat_cpu2_on() ? ". CORES 2 " : ". CORES 1 ");
    say_num(d, end, plat_cpu2_on() ? plat_cpu2_stack() : (uint32_t)plat_cpu2_gave_up());
#endif
    ann_say(t, 0, ANN_NOW);
    pf.frames = pf.draw = pf.draw_max = pf.send = pf.send_max = 0;
    pf.since_ms = plat_ms();
}

static void speak(void)
{
    static int inst_was = -1, algo_was = -1, page_was = -1, lay_was = -1, val_was[H1_NENC];
    char t[96], *d, *end = t + sizeof t - 1;
    int inst = h1_instrument(), algo = h1_algorithm(), page = h1_page(), lay = h1_layers(), k, quiet = 0;
    if (ann_on()) {
        int where = ann_where();
        if (lay != lay_was && lay_was >= 0)                 /* SEQ and REC together */
            ann_say(lay ? "LAYERS ON" : "LAYERS OFF", 0, ANN_NOW);
        if (where || inst != inst_was || algo != algo_was) {
            d = say_cat(t, end, where ? APP_NAME ". " : "");
            d = say_cat(d, end, h1_instrument_name(inst));
            d = say_cat(d, end, ". ");
            say_cat(d, end, h1_algo_name(inst, algo));
            ann_say(t, 0, ANN_NOW);
            quiet = 1;                                      /* the knobs all changed with it: not said */
        }
        if (where || page != page_was) {
            d = say_cat(t, end, page ? "PAGE" : "HOME");
            for (k = H1_ENC_K1; k <= H1_ENC_K4; k++) {
                const char *name = 0;
                h1_control(k, 0, 0, &name, 0);
                d = say_cat(d, end, ". ");
                d = say_cat(d, end, name);
            }
            ann_say(t, 0, quiet ? 0 : ANN_NOW);
            quiet = 1;
        }
        for (k = 0; k < H1_NENC; k++) {
            const char *name = 0, *text = 0;
            int v = h1_control(k, 0, 0, &name, &text);
            if (v != val_was[k] && !quiet && k != H1_ENC_PRESET && k != H1_ENC_ALGO) {
                d = say_cat(t, end, name);
                d = say_cat(d, end, " ");
                say_cat(d, end, text);
                ann_later(t, ANN_NOW, 250u, plat_ms());
            }
            val_was[k] = v;
        }
    }
    inst_was = inst;
    algo_was = algo;
    page_was = page;
    lay_was = lay;
    if (pf_ask) {
        pf_ask = 0;
        if (ann_on())
            say_perf();
    }
    ann_tick(plat_ms());
}

/* the platform's own short messages (an update starting, a save that failed): to the reader */
void app_say(const char *a, const char *b)
{
    if (ann_on())
        ann_say(a, b, ANN_NOW | ANN_KEEP);
}
