/* h1's crash hunt: the core alone, on a computer, under the compiler's checkers.
 *
 *   sh tests/run_fuzz.sh [SEEDS]
 *
 * The FM-1's CPU stops on things a computer shrugs at: a word read from an odd address, a read
 * past the end of an array, a rectangle taller than the buffer it is converted in. Built with
 * -fsanitize=address,undefined, every one of those stops this program too, with the line.
 * It does four things, each from a seed so a failure can be run again:
 *
 *   1. state: saved states that pass the magic and version check but hold any bytes at all, handed
 *      over from an odd address, then played. Whatever a store gives back, h1 has to start.
 *   2. panel: every key, button (taps and holds), encoder and MIDI byte in any order and at any
 *      pace, on every instrument and page, with the sound rendered and every frame drawn.
 *   2b. long notes: every algorithm of every instrument with the knobs at their ends and a chord held
 *      nine seconds, then let go: what only goes wrong late in a note.
 *   3. two threads: the same, while a second thread renders the sound without waiting its turn,
 *      as the audio interrupt does on the device.
 *
 * A quarter of all of it runs with a second core (a thread) taking every other voice, as
 * h1_two_cores has it on an FM-1 built with one.
 *
 * It checks what the checkers cannot see: every rectangle sent to the panel is inside the screen
 * and no taller than a band (20 rows), after any state the values a picture and a sound index
 * with are in range, and a frame sent the way the device sends it (in pieces, while the next one is
 * drawn) leaves the panel showing exactly that frame. */
#include <pthread.h>
#ifdef __APPLE__   /* macOS has no unnamed POSIX semaphores (sem_init fails): Grand Central Dispatch's instead */
#include <dispatch/dispatch.h>
typedef dispatch_semaphore_t sem_t;
#define sem_init(s, shared, v) (*(s) = dispatch_semaphore_create(v), 0)
#define sem_wait(s) dispatch_semaphore_wait(*(s), DISPATCH_TIME_FOREVER)
#define sem_post(s) dispatch_semaphore_signal(*(s))
#else
#include <semaphore.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../core/h1.h"

/* how many algorithms an instrument has (eight, LAVA nine): the names it gives */
static int nalgos(int inst) { int a = 0; while (a < 16 && *h1_algo_name(inst, a)) a++; return a; }

static uint32_t rng_s = 1;
static uint32_t rnd(void)
{
    rng_s ^= rng_s << 13;
    rng_s ^= rng_s >> 17;
    rng_s ^= rng_s << 5;
    return rng_s;
}
static int below(int n) { return (int)(rnd() % (uint32_t)n); }

static unsigned seed_now;
static const char *part = "";
static void die(const char *what, int a, int b, int c, int d)
{
    fprintf(stderr, "FAIL (%s, seed %u): %s %d %d %d %d\n", part, seed_now, what, a, b, c, d);
    exit(1);
}

static void blit(int x, int y, int w, int h)
{
    if (x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > H1_W || y + h > H1_H || h > 20)
        die("a rectangle the device cannot take", x, y, w, h);
}

extern uint32_t h1_fuzz_nan;                 /* core/synth.c, in this build: samples that were not numbers */

/* MIDI out (h1_midi_out): every message a well-formed one on MIDI's CHAN (1 when it is ALL), notes and CC 20-23 and 123 only */
static volatile uint32_t midi_outs;
static void midi_out_check(uint8_t status, uint8_t d1, uint8_t d2)
{
    int ch = h1_midi_channel(), t = status & 0xF0;
    int ok = d1 < 128 && d2 < 128 && (status & 15) == (ch ? ch - 1 : 0) &&
             ((t == 0x90 && d2 >= 1) || (t == 0x80 && d2 == 0) ||
              (t == 0xB0 && ((d1 >= 20 && d1 <= 23) || (d1 == 123 && d2 == 0))));
    if (!ok)
        die("a MIDI message out that is not one", status, d1, d2, 0);
    midi_outs++;
}

static uint32_t now_ms;
static uint32_t keys_down, btns_down;

/* the stems (h1_stems): a ring of 10-channel frames as fm1-base's USB audio input keeps it, wrapping at an odd
 * place; now and then nobody records */
#define SRING 301u
static int16_t sring[SRING][10];
static uint32_t sring_w, sring_off;
static int16_t *stem_frame(uint32_t off, uint32_t *run)
{
    uint32_t s;
    if (sring_off) {
        *run = 0;
        return 0;
    }
    s = (sring_w + off) % SRING;
    *run = SRING - s;
    return &sring[s][2];
}

static void sound(uint32_t n)
{
    static int32_t lr[2 * 1024];
    uint32_t i;
    h1_render(lr, n);
    sring_w = (sring_w + n) % SRING;
    if ((rnd() & 63u) == 0)
        sring_off ^= 1u;
    if (h1_fuzz_nan)
        die("the sound is not a number", (int)h1_fuzz_nan, h1_instrument(), h1_algorithm(), 0);
    for (i = 0; i < 2 * n; i++)
        if (lr[i] > 8388607 || lr[i] < -8388608)
            die("a sample outside 24 bits", (int)i, (int)lr[i], 0, 0);
}

/* The device sends a frame while the next one is drawn (h1_present_begin, _next, h1_while_drawing).
 * Here the same, with a panel of our own: rectangles leave the queue at any moment of the next frame's
 * drawing, and when the queue is empty the panel has to show exactly the frame that was queued. */
static uint16_t shown[H1_W * H1_H], want[H1_W * H1_H], pal_then[256];
static int piped, have_want;

static void pipe_one(void)
{
    const uint8_t *px = h1_panel();
    int x, y, w, h, i, j;
    if (!h1_present_next(&x, &y, &w, &h))
        return;
    blit(x, y, w, h);
    for (j = y; j < y + h; j++)
        for (i = x; i < x + w; i++)
            shown[j * H1_W + i] = pal_then[px[j * H1_W + i]];
}

static void pipe_poll(void)
{
    if (piped && below(4) == 0)
        pipe_one();
}

static void frame(uint32_t ms, int with_sound)
{
    now_ms += ms;
    if (with_sound)
        sound(ms > 16 ? 256u * 3u : 256u * (uint32_t)(1 + below(3)));
    h1_frame(now_ms);
    if (piped) {
        const uint8_t *fb = h1_framebuffer();
        int i;
        while (h1_present_left())
            pipe_one();
        if (have_want && memcmp(shown, want, sizeof shown))
            die("the panel does not show the frame that was sent", 0, 0, 0, 0);
        memcpy(pal_then, h1_palette(), sizeof pal_then);
        h1_present_begin();
        if (h1_present_rects() > 512 || (h1_panel() == fb))
            die("the queue", h1_present_rects(), 0, 0, 0);
        for (i = 0; i < H1_W * H1_H; i++)
            want[i] = pal_then[fb[i]];
        have_want = 1;
    } else {
        h1_present(blit);
        have_want = 0;
    }
    if (h1_instrument() < 0 || h1_instrument() >= h1_instrument_count() || h1_algorithm() < 0 || h1_algorithm() >= nalgos(h1_instrument()) ||
        h1_page() < 0 || h1_page() > 9)
        die("out of range", h1_instrument(), h1_algorithm(), h1_page(), 0);
    (void)h1_key_leds();
    (void)h1_button_leds();
    {
        int k;
        for (k = 0; k < H1_NENC; k++) {
            int lo, hi;
            const char *name = 0, *text = 0;
            int v = h1_control(k, &lo, &hi, &name, &text);
            if (name && strlen(name) > 40) die("a name too long", k, v, 0, 0);
            if (text && strlen(text) > 40) die("a value's text too long", k, v, 0, 0);
        }
    }
}

/* one thing a hand (or a cable) could do */
static void poke(int with_sound)
{
    int r = below(100), k;
    if (r < 30) {                                            /* a key */
        k = below(H1_NKEYS);
        keys_down ^= 1u << k;
        h1_key(k, (int)(keys_down >> k & 1u));
    } else if (r < 50) {                                     /* a button down or up */
        k = below(H1_NBTN);
        btns_down ^= 1u << k;
        h1_button(k, (int)(btns_down >> k & 1u));
    } else if (r < 75) {                                     /* an encoder: a click, or a spin */
        static const int D[] = {1, -1, 1, -1, 2, -3, 7, -11, 40, -64, 300, -300};
        h1_encoder(below(H1_NENC), D[below(12)]);
    } else if (r < 80) {                                     /* MIDI, any bytes */
        h1_midi((uint8_t)(0x80 + below(128)), (uint8_t)below(256), (uint8_t)below(256));
    } else if (r < 85) {                                     /* a MIDI keyboard: notes at any velocity, CC 123 */
        int nt = 36 + below(60);
        if (below(10) == 0)
            h1_midi(0xB0, 123, 0);
        else
            h1_midi(below(3) ? 0x90 : 0x80, (uint8_t)nt, (uint8_t)below(128));
    } else if (r < 90) {                                     /* a slider in the simulator, any value */
        h1_control_set(below(H1_NENC), below(600) - 200);
    } else if (r < 93) {                                     /* everything let go */
        for (k = 0; k < H1_NKEYS; k++)
            if (keys_down >> k & 1u) h1_key(k, 0);
        for (k = 0; k < H1_NBTN; k++)
            if (btns_down >> k & 1u) h1_button(k, 0);
        keys_down = btns_down = 0;
    } else if (r < 94) {                                     /* the volume knob, in range and out */
        h1_master(below(1400) - 200);
        if (below(4) == 0) {                                 /* the port: how busy the sound is, any figure */
            int n = 1 + below(200), pct = below(5) ? below(160) : below(1000) - 200;
            while (n--)
                h1_load(pct);
        }
        if (below(8) == 0) {                                 /* a loop sent out: what comes back is whole */
            uint32_t size = 0;
            uint8_t meta[H1_META_N];
            const uint8_t *p = (const uint8_t *)h1_loop_export(&size, meta);
            volatile uint8_t sum = 0;
            uint32_t i;
            if (size > H1_LOOP_MAX)
                die("a loop sent out bigger than any", (int)size, 0, 0, 0);
            for (i = 0; p && i < size; i++)
                sum ^= p[i];
            (void)h1_silent();
            (void)h1_screen();
            (void)h1_state_asked();
        }
        /* (the MIDI extras in: bend, the wheel, the pedal, on any channel) */
        if (below(3) == 0) {
            static const uint8_t ST[] = {0xE0, 0xE5, 0xB0, 0xB3, 0xBF};
            uint8_t s0 = ST[below(5)];
            h1_midi(s0, (uint8_t)(s0 >= 0xE0 ? below(128) : below(2) ? 1 : 64), (uint8_t)below(128));
        }
    } else if (r < 96) {                                     /* nonsense from a port: ignored */
        h1_key(below(64) - 16, below(2));
        h1_button(below(64) - 16, below(2));
        h1_encoder(below(32) - 8, below(9) - 4);
        h1_control_set(below(32) - 8, below(600) - 200);
    } else {                                                 /* time passes: long enough for any hold */
        static const uint32_t T[] = {0, 1, 100, 500, 1000, 3200, 6000};
        frame(T[below(7)], with_sound);
    }
}

/* a second core, as a thread: it sleeps until it is handed a job, and the sound waits for it */
static sem_t job_go, job_done;
static void (*job_fn)(uint32_t);
static uint32_t job_arg;
static void *core_two(void *x)
{
    (void)x;
    for (;;) {
        sem_wait(&job_go);
        job_fn(job_arg);
        sem_post(&job_done);
    }
    return 0;
}
static int run_two(void (*fn)(uint32_t), uint32_t arg)
{
    job_fn = fn;
    job_arg = arg;
    sem_post(&job_go);
    return 1;
}
static int wait_two(void)
{
    sem_wait(&job_done);
    return 0;
}
static int no_two(void (*fn)(uint32_t), uint32_t arg) { (void)fn; (void)arg; return 0; }   /* (it takes nothing) */

static void fresh(uint32_t t0)
{
    static int started;
    static pthread_t th;
    if (!started) {
        started = 1;
        sem_init(&job_go, 0, 0);
        sem_init(&job_done, 0, 0);
        if (pthread_create(&th, 0, core_two, 0))
            die("no thread for the second core", 0, 0, 0, 0);
    }
    h1_init(44100);
    switch (rnd() & 3u) {                        /* one core mostly; two; or a second core that takes no job */
    case 0: h1_two_cores(run_two, wait_two); break;
    case 1: h1_two_cores(no_two, wait_two); break;
    default: h1_two_cores(0, 0); break;
    }
    h1_stems(rnd() & 1u ? stem_frame : 0, 10);
    h1_while_drawing(pipe_poll);
    h1_midi_out(midi_out_check);
    have_want = 0;
    piped = (int)(rnd() & 1u);                   /* half the time as the device sends, half all at once */
    now_ms = t0;
    keys_down = btns_down = 0;
}

/* ---- 1. any saved state, from an odd address ----------------------------------------------- */
static int loops_taken;
static void part_state(int rounds)
{
    static uint8_t odd[4096 + 8];
    uint32_t size, i;
    int n, k;
    part = "state";
    fresh(0);
    (void)h1_state(&size);
    if (size + 3 > sizeof odd)
        die("the state outgrew this test", (int)size, 0, 0, 0);
    /* a good state comes back, wherever the buffer sits */
    for (k = 1; k < 4; k++) {
        fresh(0);
        h1_encoder(H1_ENC_PRESET, 3 + k);
        memcpy(odd + k, h1_state(&size), size);
        fresh(0);
        if (h1_state_load(odd + k, size) != 0 || h1_instrument() != 3 + k)
            die("a good state was refused at an odd address", k, h1_instrument(), 0, 0);
    }
    /* what is not this version's is refused and changes nothing */
    fresh(0);
    memcpy(odd + 1, h1_state(&size), size);
    odd[1] ^= 1u;
    if (h1_state_load(odd + 1, size) == 0 || h1_state_load(odd + 1, size - 1) == 0 || h1_state_load(0, size) == 0)
        die("a bad state was taken", 0, 0, 0, 0);
    for (n = 0; n < rounds; n++) {
        fresh(n & 1 ? 0xFFFFF000u : 0u);                     /* (the clock's wrap too) */
        memcpy(odd + 1, h1_state(&size), size);
        if (n % 3 == 0)
            for (i = 5; i < size; i++) odd[1 + i] = (uint8_t)rnd();          /* all of it */
        else
            for (k = 0; k < 40; k++) odd[1 + 5 + below((int)size - 5)] = (uint8_t)rnd();   /* a few bytes */
        if (n % 5 == 4) {                                    /* the state before LAYERS: version 4, 664 bytes */
            odd[1 + 4] = 4;
            if (h1_state_load(odd + 1, 664u) != 0 || !h1_layers())
                die("a version 4 state was refused, or came back with LAYERS off", n, h1_layers(), 0, 0);
        } else if (n % 5 == 2) {                             /* the one before a step's eight notes and each algorithm's
                                                              * knobs: version 5, 920 bytes */
            odd[1 + 4] = 5;
            if (h1_state_load(odd + 1, 920u) != 0)
                die("a version 5 state was refused", n, 0, 0, 0);
        } else if (n % 5 == 1) {                             /* the one of fourteen instruments: version 6, 1880 bytes */
            odd[1 + 4] = 6;
            if (h1_state_load(odd + 1, 1880u) != 0 || !h1_layers())
                die("a version 6 state was refused, or came back with LAYERS off", n, 0, 0, 0);
        } else if (n % 5 == 3) {                             /* and that with velocities and layers: version 8, 3160 bytes */
            odd[1 + 4] = 8;
            if (h1_state_load(odd + 1, 3160u) != 0)
                die("a version 8 state was refused", n, 0, 0, 0);
        } else if (h1_state_load(odd + 1, size) != 0)
            die("a state with the right magic and version was refused", n, 0, 0, 0);
        if (n % 2 == 0) {                                    /* and a saved loop: any bytes after its head, from an
                                                              * odd address (the state's layers are what it names) */
            static uint8_t lp[H1_LOOP_MAX + 8];
            uint32_t ln = 12u + 8u * (uint32_t)below(400), j;
            if (ln > H1_LOOP_MAX + 4u)
                ln = H1_LOOP_MAX + 4u;
            for (j = 0; j < ln; j++)
                lp[3 + j] = (uint8_t)rnd();
            lp[3] = 'H'; lp[4] = '1'; lp[5] = 'L'; lp[6] = 'P'; lp[7] = 1;
            if (below(4)) {                                  /* a count that fits, a length in range (mostly) */
                uint32_t ne = (ln - 12u) / 8u, len = 24u + (uint32_t)below(3840 - 24 + 1);
                lp[9] = (uint8_t)ne; lp[10] = (uint8_t)(ne >> 8);
                lp[11] = (uint8_t)len; lp[12] = (uint8_t)(len >> 8);
            }
            if (below(8) == 0)
                lp[3 + (uint32_t)below((int)ln)] ^= (uint8_t)(1u << below(8));
            if (h1_loop_load(lp + 3, below(4) ? ln : ln - (uint32_t)below(9)) == 0)
                loops_taken++;
        }
        for (k = 0; k < 6; k++)
            frame(16, 1);
        h1_button(H1_BTN_PLAY, 1);                           /* whatever the pattern holds, play it */
        h1_button(H1_BTN_PLAY, 0);
        for (k = 0; k < 4; k++)
            h1_key(below(H1_NKEYS), 1);
        for (k = 0; k < 40; k++)
            frame(16 + (uint32_t)(k % 3 == 0), 1);
        for (k = 0; k < 60; k++)
            poke(1);
    }
    if (rounds >= 20 && !loops_taken)
        die("no saved loop was ever taken: the loop part tested nothing", rounds, 0, 0, 0);
}

/* ---- 2. the panel, any way at all ------------------------------------------------------------ */
static void part_panel(int steps)
{
    int n, inst;
    part = "panel";
    for (inst = 0; inst < h1_instrument_count(); inst++) {
        fresh(inst & 1 ? 0xFFFF0000u : 0u);
        h1_encoder(H1_ENC_PRESET, inst);
        if (inst & 2) {                                      /* half of them with LAYERS off from the start */
            h1_button(H1_BTN_SEQ, 1);
            h1_button(H1_BTN_REC, 1);
            h1_button(H1_BTN_REC, 0);
            h1_button(H1_BTN_SEQ, 0);
            if (h1_layers())
                die("SEQ and REC together did not turn LAYERS off", inst, 0, 0, 0);
        }
        for (n = 0; n < steps; n++) {
            poke(1);
            if (n % 4 == 0)
                frame(16 + (uint32_t)(n % 3 == 0), 1);
        }
        {                                                    /* the loop as it would be saved, loaded again: the same */
            static uint8_t a[H1_LOOP_MAX + 4], b[H1_LOOP_MAX + 4];
            uint32_t na, nb;
            const void *p = h1_loop(&na);
            if (na > H1_LOOP_MAX)
                die("a saved loop longer than its room", (int)na, 0, 0, 0);
            memcpy(a + 1, p, na);
            for (n = 12; n + 8 <= (int)na; n += 8)          /* (bit 7 of on: played this pass, not saved) */
                a[1 + n + 3] &= 1;
            if (na) {
                if (h1_loop_load(a + 1, na) != 0)
                    die("a loop it saved itself was refused", (int)na, 0, 0, 0);
                p = h1_loop(&nb);
                memcpy(b, p, nb);
                if (nb != na || memcmp(a + 1, b, na)) {
                    uint32_t q;
                    for (q = 0; q < na; q++)
                        if (a[1 + q] != b[q])
                            fprintf(stderr, "byte %u (event %d field %d): %u -> %u\n", q, (int)(q - 12) / 8,
                                    (int)(q - 12) % 8, a[1 + q], b[q]);
                }
                if (nb != na || memcmp(a + 1, b, na))
                    die("a loop saved, loaded and saved again changed", (int)na, (int)nb, 0, 0);
                h1_button(H1_BTN_PLAY, 1);
                h1_button(H1_BTN_PLAY, 0);
                for (n = 0; n < 30; n++)
                    frame(16, 1);
            }
        }
    }
}

/* ---- 2b. long notes: every algorithm of every instrument, a chord held nine seconds ----------- */
static void part_long(void)
{
    static const uint8_t EDGE[] = {0, 0, 50, 100, 100, 127};
    int inst, algo, k, n;
    part = "long notes";
    fresh(0);
    for (inst = 0; inst < h1_instrument_count(); inst++) {
        for (algo = 0; algo < nalgos(inst); algo++) {
            for (k = 0; k < 4; k++)                          /* the knobs at their ends, mostly */
                h1_midi(0xB0, (uint8_t)(20 + k), below(4) ? EDGE[below(6)] : (uint8_t)below(128));
            for (k = 0; k < 3; k++)
                h1_key(below(H1_NKEYS), 1);
            for (n = 0; n < 390; n++) {                      /* 390 * 1024 frames: nine seconds */
                sound(1024);
                if (n % 16 == 0)
                    frame(370, 0);
            }
            for (k = 0; k < H1_NKEYS; k++)
                h1_key(k, 0);
            for (n = 0; n < 40; n++)                         /* and the tails */
                sound(1024);
            frame(16, 1);
            h1_encoder(H1_ENC_ALGO, 1);
        }
        h1_encoder(H1_ENC_PRESET, 1);
        frame(16, 1);
    }
}

/* ---- 3. the sound on its own thread ---------------------------------------------------------- */
static volatile int audio_run;
static void *audio_thread(void *arg)
{
    static int32_t lr[2 * 256];
    (void)arg;
    while (audio_run)
        h1_render(lr, 256);
    return 0;
}

static void part_threads(int steps)
{
    pthread_t th;
    int n;
    part = "two threads";
    fresh(0);
    audio_run = 1;
    if (pthread_create(&th, 0, audio_thread, 0))
        die("no thread", 0, 0, 0, 0);
    for (n = 0; n < steps; n++) {
        poke(0);
        if (n % 4 == 0)
            frame(16, 0);
    }
    audio_run = 0;
    pthread_join(th, 0);
    if (h1_fuzz_nan)
        die("the sound is not a number", (int)h1_fuzz_nan, h1_instrument(), h1_algorithm(), 0);
}

int main(int argc, char **argv)
{
    unsigned seeds = argc > 1 ? (unsigned)atoi(argv[1]) : 8u, first = argc > 2 ? (unsigned)atoi(argv[2]) : 1u, s;
    for (s = first; s < first + seeds; s++) {
        seed_now = s;
        rng_s = s * 2654435761u + 1u;
        part_state(40);
        part_panel(1500);
        part_long();
        part_threads(6000);
    }
    printf("fuzz: %u seeds from %u, nothing found\n", seeds, first);
    return 0;
}
