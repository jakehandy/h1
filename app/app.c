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

/* USB host (fm1.toml usb_host): the core asks with OCT- and OCT+ held (h1_usb_host_fn), the platform switches
 * the port, and what it finds is told as a toast. A device lost, or the mode changed: its notes let go. */
static void usb_host(int on) { plat_usb_host(on); }
static void usb_tell(const char *s)             /* on the screen, and to the reader */
{
    h1_toast(s);
    if (ann_on())
        ann_say(s, 0, ANN_NOW);
}
static void usb_news(void)
{
    static int host_was;
    if (h1_usb_host() != host_was) {             /* (the core's own toast says it on the screen) */
        host_was = h1_usb_host();
        if (ann_on())
            ann_say(host_was ? "USB HOST" : "USB COMPUTER", 0, ANN_NOW);
    }
    switch (plat_usb_host_msg()) {
    case USBH_MSG_MIDI: {
        const char *nm = plat_usb_host_name();
        usb_tell(nm && nm[0] ? nm : "KEYBOARD");
        break;
    }
    case USBH_MSG_NOMIDI: usb_tell("NOT MIDI"); break;
    case USBH_MSG_LOW: usb_tell("NOT SUPPORTED"); break;
    case USBH_MSG_LOST: usb_tell("UNPLUGGED"); break;
    default: break;
    }
    if (plat_usb_lost())
        h1_midi(0xB0, 123, 0);
}

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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void ring_dump(void);
static int host_bad_start;                                  /* the tests' "badstart": the next start is one */
int app_host_cmd(const char *cmd, const char *a, const char *b, const char *c, const char *d)
{
    (void)c; (void)d;
    if (!strcmp(cmd, "expect") && !strcmp(a, "loop_size")) {   /* expect loop_size N: the loop's saved bytes */
        uint32_t size = 0;
        (void)h1_loop(&size);
        if (size != (uint32_t)strtoul(b, 0, 0)) {
            printf("FAIL expect loop_size == %s, got %u\n", b, (unsigned)size);
            exit(1);
        }
        printf("  ok loop_size == %u\n", (unsigned)size);
        return 1;
    }
    if (!strcmp(cmd, "ring")) {                  /* ring: where the loop's record is, and each slot's fill */
        ring_dump();
        return 1;
    }
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

/* MIDI out, to the computer over USB (the FM-1's TRS jack is an input only): the core's notes from the render,
 * its knobs from the main loop; plat_midi_send takes both. With nobody listening there is no room, and nothing
 * is queued. A note-on or a knob goes only while there is room to spare, so the queue never fills with them
 * and leaves a note-off out (a note the computer would then hold for ever). */
#define MIDI_SPARE 16u
static void midi_send(uint8_t status, uint8_t d1, uint8_t d2)
{
    uint32_t pkt = (uint32_t)(status >> 4) | (uint32_t)status << 8 | (uint32_t)d1 << 16 | (uint32_t)d2 << 24;
    uint32_t room = plat_midi_out_room(TO_USB);
    int off = (status & 0xF0u) == 0x80u || (status == 0xB0u && d1 == 123u);
    if (off ? room > 0u : room > MIDI_SPARE)
        plat_midi_send(pkt, TO_USB);
}

/* ---- the loop, kept through a power-off while it plays (#39) -----------------------------------
 * A store save erases a sector first, and an erase stops the sound for about 50 ms, so the store only ever
 * took the loop once it had been stopped: a loop left playing was lost at power-off. The loop has a ring of
 * its own instead, in the data area: RING_SLOTS slots of RING_SECT sectors each. A save is a record: a header
 * page (magic, a sequence number, the blob's length and its CRC-32) and the blob after it, page-aligned. A
 * slot takes records one after another until it is full, so a loop that goes on playing has room for many
 * saves (a light loop dozens; a loop over about 600 notes, one a slot, so three), not one a slot. A save goes into erased space, a
 * 256-byte page a frame (a page program is short, no erase), with the header written last, so a record whose
 * header reads right holds a whole loop. The newest such record is the loop. A save given up part-way gets a
 * header marked dead, so the records after it can still be found. Slots are erased at start (before the
 * sound) and once the loop is stopped and nothing sounds, all but the one holding the loop.
 * A record holds the song after the loop (h1_song: the steps and the layers, 0.3.3), so they keep while it plays.
 * Before this the loop was store object OBJ_LOOP: read once, when the ring holds none, then emptied once the ring
 * has the loop (it came back after another firmware had the ring's sectors, #71). */
#define RING_SECT 5u                     /* sectors a slot */
#define RING_SLOTS 4u
#define RING_SLOT_BYTES (RING_SECT * 4096u)
#define RING_BASE (0xDC000u - RING_SLOTS * RING_SLOT_BYTES)   /* 0xC8000: just under the panel's calibration */
#define RING_BODY 256u
#define RING_MAGIC 0x524C3148u           /* "H1LR": a record */
#define RING_DEAD 0x584C3148u            /* "H1LX": a save given up; its length says how far it got */
#define RING_PAGE 256u
#define RING_FULL RING_SLOT_BYTES        /* (a slot's fill: no more records go in until it is erased) */
typedef char loop_fits_a_slot[H1_LOOP_MAX + H1_SONG_MAX <= RING_SLOT_BYTES - RING_BODY ? 1 : -1];
typedef char ring_in_the_data_area[RING_BASE >= PLAT_DATA_LO ? 1 : -1];

static struct {
    int cur;                             /* the slot that holds the loop, -1 none */
    uint32_t cur_at;                     /* where in it the loop's record begins */
    uint32_t seq;                        /* the newest record's sequence number */
    uint32_t fill[RING_SLOTS];           /* where a slot's next record goes; RING_FULL: none fits */
    int to;                              /* the slot being written, -1 none */
    uint32_t to_at;                      /* where in it the record begins */
    uint32_t len, at, crc;               /* what is being written: its length, how far, its CRC */
    uint32_t wait;                       /* frames the loop has been dirty: a save begins a moment after */
    uint32_t gen;                        /* h1_loop_gen when it began: h1_loop is the engine's own memory, so a
                                          * take recorded meanwhile moves its events, and that save is dropped */
} ring = {-1, 0, 0, {RING_FULL, RING_FULL, RING_FULL, RING_FULL}, -1, 0, 0, 0, 0, 0, 0};
#define RING_WAIT 20u                    /* (a third of a second: the save keeps clear of the take's own close) */
static const uint8_t *ring_src;          /* the blob being written, as h1_loop gave it: it holds until a take
                                          * records again (then the loop is dirty again, and saved anew) */
static const uint8_t *ring_song;         /* and the song after it (h1_song's copy), from ring_n1 on */
static uint32_t ring_n1;
static int ring_force;                   /* a loop from OBJ_LOOP: into the ring, dirty or not */
#define RING_MAX (RING_SLOT_BYTES - RING_BODY)

static uint32_t ring_off(int slot) { return RING_BASE + (uint32_t)slot * RING_SLOT_BYTES; }
static uint32_t ring_pages(uint32_t n) { return (n + RING_PAGE - 1u) & ~(RING_PAGE - 1u); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static void wr32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

static uint32_t crc32_run(uint32_t c, const uint8_t *p, uint32_t n)   /* c: ~0 to start, ~ of it at the end */
{
    uint32_t k;
    while (n--) {
        c ^= *p++;
        for (k = 0; k < 8u; k++)
            c = c >> 1 ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return c;
}
static uint32_t crc32_of(const uint8_t *p, uint32_t n) { return ~crc32_run(0xFFFFFFFFu, p, n); }

static int ring_blank(const uint8_t *p, uint32_t n)
{
    while (n--)
        if (*p++ != 0xFFu)
            return 0;
    return 1;
}

/* a slot's records, read through: the newest whole one (its place, length and number, when *len is 0 or its
 * number is newer than *seq) and where the next record would go (RING_FULL when the rest is not erased:
 * a save cut off by a power-off, or another firmware's bytes) */
static uint32_t ring_scan(int slot, uint32_t *at, uint32_t *len, uint32_t *seq)
{
    const uint8_t *s = plat_data(ring_off(slot));
    uint32_t o = 0, m, n;
    if (!s)
        return RING_FULL;
    while (o + RING_BODY <= RING_SLOT_BYTES) {
        const uint8_t *h = s + o;
        m = rd32(h);
        if (m == 0xFFFFFFFFu)                        /* no header: the rest erased is room; else a cut-off save */
            return ring_blank(h, RING_SLOT_BYTES - o) ? o : RING_FULL;
        n = rd32(h + 8);
        if ((m != RING_MAGIC && m != RING_DEAD) || n > RING_MAX || o + RING_BODY + ring_pages(n) > RING_SLOT_BYTES)
            return RING_FULL;
        if (m == RING_MAGIC && n >= 12u && crc32_of(h + RING_BODY, n) == rd32(h + 12)
            && (*len == 0 || (int32_t)(rd32(h + 4) - *seq) > 0)) {
            *at = o;
            *len = n;
            *seq = rd32(h + 4);
        }
        o += RING_BODY + ring_pages(n);
    }
    return o;
}

static void ring_erase(int slot)
{
    uint32_t k, at = 0, len = 0, seq = 0;
    for (k = 0; k < RING_SECT; k++)
        if (plat_data_erase(ring_off(slot) + k * 4096u) != 0)
            break;
    ring.fill[slot] = ring_scan(slot, &at, &len, &seq);
}

/* at start, before the sound: the newest whole loop, or 0 */
static const uint8_t *ring_start(uint32_t *len)
{
    uint32_t at, n, s, best = 0;
    int i;
    ring.cur = -1;
    ring.to = -1;                                /* (a save cut off by the restart is not carried on) */
    ring.wait = 0;
    ring.seq = 0;
    for (i = 0; i < (int)RING_SLOTS; i++) {
        at = n = s = 0;
        ring.fill[i] = ring_scan(i, &at, &n, &s);
        if (n && (ring.cur < 0 || (int32_t)(s - ring.seq) > 0)) {
            ring.cur = i;
            ring.cur_at = at;
            ring.seq = s;
            best = n;
        }
    }
    for (i = 0; i < (int)RING_SLOTS; i++)        /* every other slot erased now, while it costs nothing */
        if (i != ring.cur && ring.fill[i] != 0)
            ring_erase(i);
    *len = best;
    return ring.cur >= 0 ? plat_data(ring_off(ring.cur) + ring.cur_at) + RING_BODY : 0;
}

/* a save given up part-way: its header says so, and how far it got, so the records after it can be found */
static void ring_drop(void)
{
    static uint8_t head[16];
    uint32_t used = RING_BODY + ring_pages(ring.at);
    wr32(head, RING_DEAD);
    wr32(head + 4, 0);
    wr32(head + 8, ring.at);
    wr32(head + 12, 0);
    ring.fill[ring.to] = plat_data_write(ring_off(ring.to) + ring.to_at, head, sizeof head) == 0
        ? ring.to_at + used : RING_FULL;
    ring.to = -1;
}

static int er_slot = -1;
static uint32_t er_k;

/* every frame: a page of a save under way, or a save begun, or (stopped and silent) a slot erased */
static void ring_step(void)
{
    int i;
    if (ring.to >= 0) {
        uint32_t off = ring_off(ring.to) + ring.to_at, n = ring.len - ring.at;
        if (n) {
            static uint8_t page[RING_PAGE];      /* (a page copied out first: what is written is what the CRC
                                                  * covers, even where the engine's bytes move meanwhile) */
            uint32_t k;
            if (n > RING_PAGE)
                n = RING_PAGE;
            for (k = 0; k < n; k++)
                page[k] = ring.at + k < ring_n1 ? ring_src[ring.at + k] : ring_song[ring.at + k - ring_n1];
            ring.crc = crc32_run(ring.crc, page, n);
            if (plat_data_write(off + RING_BODY + ring.at, page, n) != 0) {
                ring.at += n;                    /* (a failed write: given up, the save tried anew) */
                ring_drop();
                return;
            }
            ring.at += n;
            if (h1_loop_gen() != ring.gen)
                ring_drop();                     /* (a take moved the events under it: given up, tried anew) */
        } else {
            static uint8_t head[16];
            wr32(head, RING_MAGIC);
            wr32(head + 4, ring.seq + 1u);
            wr32(head + 8, ring.len);
            wr32(head + 12, ~ring.crc);
            ring.fill[ring.to] = ring.to_at + RING_BODY + ring_pages(ring.len);
            if (plat_data_write(off, head, sizeof head) == 0) {
                const uint8_t *h = plat_data(off);
                if (h && rd32(h) == RING_MAGIC && rd32(h + 8) == ring.len
                    && crc32_of(h + RING_BODY, ring.len) == rd32(h + 12)) {
                    ring.cur = ring.to;
                    ring.cur_at = ring.to_at;
                    ring.seq++;
                    ring_force = 0;
                    h1_loop_saved();
                }
            } else
                ring.fill[ring.to] = RING_FULL;
            ring.to = -1;
        }
        return;
    }
    if (!h1_loop_dirty() && !ring_force)
        ring.wait = 0;
    else if (++ring.wait > RING_WAIT) {
        uint32_t size, sn, gen = h1_loop_gen();
        const uint8_t *p = (const uint8_t *)h1_loop(&size);
        ring_song = (const uint8_t *)h1_song(&sn);
        ring_n1 = size;
        size += sn;
        if (sn == size || size > RING_MAX)
            return;
        for (i = 0; i < (int)RING_SLOTS; i++) {  /* the slot holding the loop first, then the others */
            int sl = ring.cur >= 0 ? (ring.cur + i) % (int)RING_SLOTS : i;
            if (ring.fill[sl] + RING_BODY + ring_pages(size) <= RING_SLOT_BYTES) {
                ring_src = p;
                ring.to = sl;
                ring.to_at = ring.fill[sl];
                ring.len = size;
                ring.gen = gen;
                ring.at = 0;
                ring.crc = 0xFFFFFFFFu;
                return;
            }
        }
    }
    /* stopped and silent: a slot back to erased, one 4 KiB sector a frame (each one stalls the sound for
     * ~17 ms: five in a row were a cut long enough to hear, and to see in the LEDs, #58 #64). A slot part
     * erased is full until it is done, so no save goes into it; a power-off part way leaves it to ring_start. */
    if (h1_loop_quiet() && h1_silent()) {
        if (er_slot < 0)
            for (i = 0; i < (int)RING_SLOTS; i++)
                if (ring.fill[i] != 0 && i != ring.cur && i != ring.to) {
                    er_slot = i;
                    er_k = 0;
                    ring.fill[i] = RING_FULL;
                    break;
                }
        if (er_slot >= 0) {
            uint32_t at = 0, len = 0, seq = 0;
            if (plat_data_erase(ring_off(er_slot) + er_k * 4096u) == 0 && ++er_k < RING_SECT)
                return;
            ring.fill[er_slot] = ring_scan(er_slot, &at, &len, &seq);
            er_slot = -1;
        }
    }
}

#ifdef PLAT_HOST
static void ring_dump(void)
{
    printf("ring: loop in slot %d at 0x%X (0x%X), seq %u, fill %X %X %X %X\n", ring.cur, (unsigned)ring.cur_at,
           ring.cur >= 0 ? (unsigned)(ring_off(ring.cur) + ring.cur_at) : 0u, (unsigned)ring.seq,
           (unsigned)ring.fill[0], (unsigned)ring.fill[1], (unsigned)ring.fill[2], (unsigned)ring.fill[3]);
}
#endif

void app_init(void)
{
    static uint32_t blob[PLAT_STORE_MAX / 4];               /* words: the store fills it, the core reads it */
    uint32_t size;
    int n;
    h1_init(44100);
    h1_while_drawing(send_some);
    h1_midi_out(midi_send);
    h1_usb_host_fn(usb_host);
#if TWO_CORES
    if (plat_cpu2_set(1) == 0)                              /* (app_init is the one moment it can start) */
        h1_two_cores(core2_run, core2_wait);
#endif
#if PLAT_USB_CH >= 8
    h1_stems(plat_usb_ch, PLAT_USB_CH_STRIDE);              /* USB audio's channels 3..10: four stereo stems */
#endif
    if (last_start_failed())
        return;
    h1_state(&size);
    n = size <= sizeof blob ? plat_store_load(OBJ_STATE, blob, sizeof blob) : -1;
    if (n > 0)
        h1_state_load(blob, (uint32_t)n);                   /* refused unless it is this version's, or one it knows */
    {                                                       /* then the loop (the state's load empties it) */
        uint32_t len;
        const uint8_t *lp = ring_start(&len);
        if (lp) {
            h1_loop_load(lp, len);
            if (plat_store_size(OBJ_LOOP) > 0)              /* the ring has it: the old one gone, or it would come */
                plat_store_save(OBJ_LOOP, blob, 0);         /* back whenever the ring holds none (#71) */
        } else if ((n = plat_store_load(OBJ_LOOP, blob, sizeof blob)) > 0) {
            h1_loop_load(blob, (uint32_t)n);                /* (one saved before the ring: it goes there next) */
            ring_force = 1;
        }
    }
}

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    static float gain_now;
    int held = h1_global_volume();                          /* (the knob may be the instrument's just now) */
    float p = (float)held * 0.001f;                         /* (held: a position, 0..1000, as h1_master was given it) */
    /* master_q12 is already fm1-base's square of the knob's position (APP_GAIN), so a held position is squared
     * here first: the held volume is then the one the knob gave a moment ago. (It was not, and holding made
     * everything louder: #38.) */
    float m = held < 0 ? (float)master_q12 * (1.0f / 4096.0f) : p * p;
    float gain = m * m;                                     /* and the square of that: MASTER's curve on the FM-1 */
    uint32_t i, t0 = plat_ticks24(), us;
#if PLAT_USB_CH >= 8
    uint32_t run = 0;
    int16_t *u = plat_usb_ch(0, &run);                      /* the stems, at MASTER too (NULL: nobody records) */
#endif
    h1_render(lr, n);                                       /* 24-bit, already limited */
    for (i = 0; i < n; i++) {
        gain_now += (gain - gain_now) * 0.004f;             /* ~5 ms: turning MASTER does not click */
        lr[2 * i] = (int32_t)((float)lr[2 * i] * gain_now);
        lr[2 * i + 1] = (int32_t)((float)lr[2 * i + 1] * gain_now);
#if PLAT_USB_CH >= 8
        if (u) {
            int32_t g = (int32_t)(gain_now * 32768.0f), k;
            if (!run && !(u = plat_usb_ch(i, &run)))
                continue;
            for (k = 0; k < 8; k++)
                u[k] = (int16_t)(u[k] * g >> 15);
            u += PLAT_USB_CH_STRIDE;
            run--;
        }
#endif
    }
    us = US(plat_ticks24() - t0);
    pf_audio += us;
    pf_blocks++;
    if (us > pf_audio_max)
        pf_audio_max = us;
    pf_block_us = n * 1000000u / 44100u;
}

void app_panic(void) { h1_midi(0xB0, 123, 0); }

/* ---- the panel's setup (0.3.1) ----------------------------------------------------------------
 * fm1-base's lcd_init resets the ST7789V and sets it up (fm1.toml lcd_panel: M-VAVE's register values
 * too). Newer FM-1s, the ones with the FM-1/M-VAVE logo under the screen, came back washed out, without
 * reds, or blank. So the same register values go once more here in the order M-VAVE's own firmware sends
 * them (display off, sleep out, porch, gate, VCOM, power, gammas, then pixel format, scan, inversion and
 * display on), with its two extra settings: the second data line off (E7) and full brightness (51).
 *
 * The panel's clock: M-VAVE's firmware runs it at 12 MHz, fm1-base at 60 (about 44 measured). SETUP's SCREEN:
 * AUTO and FAST are 60, SAFE is 12 for a screen that shows noise. (0.3.1's AUTO was 12 on the FM-1s with the
 * newer flash chip, the UC25HQ80: a busy picture then took three times as long to send, 0.3.2.) */
#ifndef PLAT_HOST
static const uint8_t SCR_SEQ[] = {         /* command, count, the bytes; command 0: wait that many ms */
    0x28, 0, 0x00, 100,                      /* DISPOFF */
    0x11, 0, 0x00, 120,                      /* SLPOUT */
    0xB2, 5, 0x0C, 0x0C, 0x0C, 0x00, 0x33,   /* PORCTRL */
    0xB7, 1, 0x56,                           /* GCTRL */
    0xBB, 1, 0x18,                           /* VCOMS */
    0xC0, 1, 0x2C,                           /* LCMCTRL */
    0xC2, 1, 0x01,                           /* VDVVRHEN */
    0xC3, 1, 0x1F,                           /* VRHS */
    0xC4, 1, 0x20,                           /* VDVS */
    0xC6, 1, 0x0F,                           /* FRCTRL2: 60 Hz */
    0xD0, 2, 0xA6, 0xA1,                     /* PWCTRL1 */
    0xE0, 14, 0xD0, 0x0D, 0x14, 0x0B, 0x0B, 0x07, 0x3A, 0x44, 0x50, 0x08, 0x13, 0x13, 0x2D, 0x32,
    0xE1, 14, 0xD0, 0x0D, 0x14, 0x0B, 0x0B, 0x07, 0x3A, 0x44, 0x50, 0x08, 0x13, 0x13, 0x2D, 0x32,
    0x36, 1, 0x00,                           /* MADCTL */
    0x3A, 1, 0x55,                           /* COLMOD: RGB565 */
    0xE7, 1, 0x00,                           /* SPI2EN: one data line */
    0x51, 1, 0xFF,                           /* WRDISBV: full */
    0x21, 0,                                 /* INVON */
    0x13, 0,                                 /* NORON */
};
#define SCR_SLOW 4u                        /* 12 MHz, M-VAVE's own */
static int scr_speed = -1;

static void scr_setup(void)
{
    uint32_t r, i;
    fm1_lcd_baud(SCR_SLOW);                /* (the setup itself always at the slow clock) */
    for (r = 0; r < sizeof SCR_SEQ; r += 2u + (SCR_SEQ[r] ? SCR_SEQ[r + 1u] : 0u)) {
        if (!SCR_SEQ[r]) {
            lcd_spin(SCR_SEQ[r + 1u] * 25000u);    /* (~1 ms a unit, as fm1-base's init waits) */
            continue;
        }
        lcd_cmd(SCR_SEQ[r]);
        for (i = 0; i < SCR_SEQ[r + 1u]; i++)
            lcd_small[16u + i] = SCR_SEQ[r + 2u + i];
        if (SCR_SEQ[r + 1u])
            lcd_data(lcd_small + 16, SCR_SEQ[r + 1u]);
    }
    lcd_sync();
}

static void scr_clock(void)                /* SETUP's SCREEN, whenever it changes */
{
    int s = h1_screen(), slow = s == 2;
    if (s == scr_speed)
        return;
    scr_speed = s;
    lcd_sync();
    fm1_lcd_baud(slow ? SCR_SLOW : LCD_BAUD);
}
#else
static void scr_setup(void) {}
static void scr_clock(void) {}
#endif

void app_splash(void)
{
    scr_setup();
    lcd_fill(0, 0, 240, 240, 0);
#ifndef PLAT_HOST
    lcd_cmd(0x29);                           /* DISPON (again: the DISPOFF above) */
#endif
}

void app_ui_init(void) { lcd_fill(0, 0, 240, 240, 0); }

static void ex_step(void);
static uint32_t dirty_ms;                   /* when the state was first waiting to be saved, with no note sounding */
static int dirty_wait;

void app_frame(void)
{
    uint32_t t0, t1, t2;
    scr_clock();
    input();
    usb_news();
    ex_step();
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
    /* the state's save erases flash, which stalls the sound for a moment: only when nothing is sounding. The
     * loop goes into its ring a page a frame, playing or not (ring_step). */
    /* (and not while an echo or a reverb still rings, either: the stall cuts its tail, #58. With no note
     * sounding for half a minute, a tail that never ends does not keep a change from being saved.) */
    if (!h1_state_dirty() || h1_voices_active())
        dirty_wait = 0;
    else if (!dirty_wait) {
        dirty_wait = 1;
        dirty_ms = plat_ms();
    }
    if (dirty_wait && (h1_silent() || h1_state_asked() || plat_ms() - dirty_ms > 30000u)) {
        uint32_t size;
        const void *p = h1_state(&size);
        dirty_wait = 0;
        if (plat_store_save(OBJ_STATE, p, size) == 0)
            h1_state_saved();
    } else
        ring_step();
}

void app_input_only(void) { input(); }

/* ---- the loop out to a computer, for a .mid (0.3.1) --------------------------------------------
 * A page on the computer (jakehandy.com/h1/midi) asks over USB MIDI with F0 7D 68 31 4C F7 ("h1" "L") and
 * gets the loop as it is recorded, every note with its time, velocity, instrument and layer, and makes the
 * file there. So the FM-1 needs no file format of its own and no storage: a few hundred bytes of code.
 * Every frame is F0 7D 68 31, a letter, then 7-bit bytes (each 7 bytes as an eighth of their top bits and
 * the seven low ones) and F7:
 *   'H'  h1_loop_export's meta (H1_META_N bytes), then the blob's length (4 bytes, little-endian)
 *   'D'  the chunk's number (two 7-bit bytes, low first), then up to EX_CHUNK bytes of the blob
 *   'E'  the CRC-32 of the blob as sent (4 bytes), the end
 *   'X'  given up: the loop changed while it went (a take), or the computer stopped reading
 * One frame a main-loop frame, and only into room the ring has: the sound and the screen go on. */
#define EX_CHUNK 196u                            /* 28 groups of 7: 224 bytes on the wire */
static struct {
    const uint8_t *p;
    uint32_t n, at, gen, crc;
    uint16_t chunk;
    uint8_t meta[H1_META_N];
    uint8_t state;                               /* 0 idle, 1 the header next, 2 data, 3 the end */
} ex;
static uint8_t ex_frame[8 + EX_CHUNK + EX_CHUNK / 7u + 2u];

static uint32_t ex_pack(uint8_t *d, const uint8_t *s, uint32_t n)   /* 7 bytes -> 8 */
{
    uint32_t o = 0, i, k;
    for (i = 0; i < n; i += 7u) {
        uint8_t hi = 0;
        uint32_t m = n - i < 7u ? n - i : 7u;
        for (k = 0; k < m; k++)
            hi |= (uint8_t)((s[i + k] >> 7) << k);
        d[o++] = hi;
        for (k = 0; k < m; k++)
            d[o++] = s[i + k] & 0x7Fu;
    }
    return o;
}

static void ex_send(uint8_t kind, const uint8_t *pre, uint32_t npre, const uint8_t *body, uint32_t nbody)
{
    uint32_t o = 0;
    ex_frame[o++] = 0xF0; ex_frame[o++] = 0x7D; ex_frame[o++] = 0x68; ex_frame[o++] = 0x31; ex_frame[o++] = kind;
    while (npre--)
        ex_frame[o++] = *pre++;
    o += ex_pack(ex_frame + o, body, nbody);
    ex_frame[o++] = 0xF7;
    if (plat_sysex_send(ex_frame, o) != 0 && kind != 'X') {
        ex.state = 0;                            /* (nobody reading: given up) */
        h1_toast("LOOP NOT SENT");
    }
}

int app_sysex(const uint8_t *p, uint32_t n)
{
    if (n < 4u || p[0] != 0x7Du || p[1] != 0x68u || p[2] != 0x31u || p[3] != 0x4Cu)
        return 0;
    ex.p = (const uint8_t *)h1_loop_export(&ex.n, ex.meta);
    ex.gen = h1_loop_gen();
    ex.at = 0;
    ex.chunk = 0;
    ex.crc = 0xFFFFFFFFu;
    ex.state = 1;
    h1_toast("SENDING LOOP");
    return 1;
}

static void ex_step(void)
{
    uint8_t b[H1_META_N + 4];
    uint32_t k;
    if (!ex.state || plat_sysex_room() < sizeof ex_frame)
        return;
    if (h1_loop_gen() != ex.gen) {
        ex.state = 0;
        ex_send('X', 0, 0, 0, 0);
        h1_toast("LOOP CHANGED");
        return;
    }
    if (ex.state == 1) {
        for (k = 0; k < H1_META_N; k++)
            b[k] = ex.meta[k];
        wr32(b + H1_META_N, ex.n);
        ex.state = 2;
        ex_send('H', 0, 0, b, sizeof b);
    } else if (ex.state == 2) {
        uint8_t no[2];
        k = ex.n - ex.at < EX_CHUNK ? ex.n - ex.at : EX_CHUNK;
        no[0] = (uint8_t)(ex.chunk & 0x7Fu);
        no[1] = (uint8_t)(ex.chunk >> 7 & 0x7Fu);
        ex.crc = crc32_run(ex.crc, ex.p + ex.at, k);
        ex.at += k;
        ex.chunk++;
        if (ex.at >= ex.n)
            ex.state = 3;
        ex_send('D', no, 2, ex.p + ex.at - k, k);
    } else {
        wr32(b, ~ex.crc);
        ex.state = 0;
        ex_send('E', 0, 0, b, 4);
        h1_toast("LOOP SENT");
    }
}

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
