/* The loop recorder's room and length (user feedback for 0.3.0). "On the third or fourth layer it stops
 * recording new takes ... the portion of the layer is not recorded": the loop held 384 events (a note-on
 * and a note-off a note) across every layer, and once they were taken whatever came next was dropped
 * without a word. And "8 bars is a little short ... at least 16 bars (or ideal 128 bars)". Now: 2048
 * events, 1 to 128 bars, a full loop says LOOP FULL and never keeps a note-on without its note-off, and a
 * loop saved by 0.2.2 (events as played, not in order) still loads and plays every note.
 *
 * Run by tests/run_tests.sh: the core alone, its panel worked as a hand would, and what the loop plays
 * counted on MIDI out. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../core/internal.h"
#include "../core/h1.h"

static int fails, checks;
#define CHECK(c, what) do { checks++; if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, what); } } while (0)

static uint64_t smp;                             /* samples rendered */
static uint32_t ms;
static int ons, offs, counting;
static uint8_t heard[128];

static void midi(uint8_t status, uint8_t d1, uint8_t d2)
{
    if (!counting)
        return;
    if ((status & 0xF0) == 0x90 && d2) {
        ons++;
        heard[d1 & 127] = 1;
    } else if ((status & 0xF0) == 0x80 || (status & 0xF0) == 0x90)
        offs++;
}

static double now(void) { return (double)smp * 1000.0 / 44100.0; }
static void until(double at_ms)                  /* render up to a moment, to the block */
{
    static int32_t out[512];
    while (now() < at_ms) {
        h1_render(out, 256);
        smp += 256;
        if ((smp / 256) % 3 == 0)
            h1_frame(ms += 17);
    }
}
static void tap(int b) { h1_button(b, 1); until(now() + 30); h1_button(b, 0); until(now() + 30); }

static double bar_ms(void) { return 4.0 * 60000.0 / st.glo[0]; }   /* 4/4 */

/* n notes evenly through len ms from t0, each held a third of its slot; key0.. key0 + 11 */
static void take(double t0, double len, int n, int key0)
{
    int i;
    for (i = 0; i < n; i++) {
        double at = t0 + len * i / n;
        until(at);
        h1_key(key0 + i % 12, 1);
        until(at + len / n / 3.0);
        h1_key(key0 + i % 12, 0);
    }
}

static int count_pass(double len)                /* the note-ons one pass of the loop plays */
{
    double t = now();
    ons = offs = 0;
    memset(heard, 0, sizeof heard);
    counting = 1;
    until(t + len);
    counting = 0;
    return ons;
}

static int seq_gated(void)                       /* voices the loop holds down now */
{
    int v, n = 0;
    for (v = 0; v < NVOICE; v++)
        n += voices[v].stage && voices[v].gate && voices[v].seq;
    return n;
}

static void stop_and_clear(void)
{
    tap(H1_BTN_PLAY);
    h1_button(H1_BTN_REC, 1); until(now() + 3300); h1_button(H1_BTN_REC, 0); until(now() + 100);
    h1_button(H1_BTN_REC, 1); until(now() + 60); h1_button(H1_BTN_REC, 0); until(now() + 100);   /* (REC again: all of it, #49) */
}

/* takes of n notes each onto a loop of 2^lb bars, another instrument for each, then one pass counted */
static double len_now;
static int layered(int lb, int takes, int n)
{
    double len, t0;
    int k;
    st.seq[1] = (uint8_t)lb;
    len = len_now = bar_ms() * (1 << lb);
    tap(H1_BTN_REC);                             /* armed: the first key starts the take */
    t0 = now() + 50;
    take(t0, len, n, 0);
    until(t0 + len + 40);                        /* round once: the first take plays */
    for (k = 1; k < takes; k++) {
        double start = t0 + len * k;
        h1_encoder(H1_ENC_PRESET, 1);            /* another instrument: another layer */
        until(start - 120);
        tap(H1_BTN_REC);                         /* onto the playing loop */
        take(start, len, n, 12 * (k & 1));
        until(start + len - 60);
        tap(H1_BTN_REC);                         /* the take ends as the loop comes round */
        until(start + len + 40);
    }
    until(t0 + len * takes + 20);
    return count_pass(len);
}

int main(void)
{
    int got, lb, i;
    uint32_t size, full0;
    const uint8_t *p;
    static uint8_t blob[H1_LOOP_MAX], old[12 + 8 * 384];

    h1_init(44100);
    h1_midi_out(midi);
    until(100);
    CHECK(h1_layers(), "LAYERS is on");
    CHECK(H1_LOOP_MAX == 12u + 8u * NLEV && NLEV == 2048, "the loop holds 2048 events");

    /* ---- the report: eight bars, light layers of eighths. Three fit in 384 events; the fourth did not ---- */
    got = layered(3, 4, 64);
    printf("looper: 8 bars, four layers of 64 notes: %d notes play back\n", got);
    CHECK(got == 4 * 64, "eight bars, four layers of 64 notes: every note plays back");
    CHECK(offs >= ons, "every note the loop starts it lets go");
    got = layered(3, 1, 0);                      /* (nothing more: just listen again) */
    stop_and_clear();
    CHECK(h1_loop_room() == NLEV / 2, "hold REC empties the loop");

    /* ---- longer loops: 16, 32, 64 and 128 bars, at a quick tempo to keep the test short ---- */
    st.glo[0] = 240;
    for (lb = 4; lb <= 7; lb++) {
        int n = 16 << (lb - 4);                  /* a note a bar */
        got = layered(lb, 2, n);
        printf("looper: %d bars, two layers of %d notes: %d notes play back in one pass\n", 1 << lb, n, got);
        CHECK(got == 2 * n, "a long loop plays every note of its takes, once a pass");
        stop_and_clear();
    }
    st.glo[0] = 112;

    /* ---- a full loop: says so, and keeps nothing half (a note-on without its note-off) ---- */
    st.glo[0] = 240;
    st.seq[3] = 0;                               /* SNAP FREE: no two notes on one tick */
    full0 = eng_loop_full();
    got = layered(5, 3, 400);                    /* 32 bars, 1200 notes: 1024 fit */
    printf("looper: 1200 notes into a loop of 1024: %d play back, room %d, toast \"%s\"\n", got, h1_loop_room(), h1_toast_now());
    CHECK(h1_loop_room() == 0, "the loop is full");
    CHECK(got == NLEV / 2, "every note that went in plays back");
    CHECK(eng_loop_full() != full0, "the notes turned away are counted");
    CHECK(offs >= ons, "every note the full loop starts it lets go");
    tap(H1_BTN_REC);                             /* one more onto the full loop: it says why it is not kept */
    h1_key(3, 1);
    until(now() + 80);
    CHECK(strcmp(h1_toast_now(), "LOOP FULL") == 0, "a note turned away by a full loop says LOOP FULL");
    h1_key(3, 0);
    until(now() + 80);
    tap(H1_BTN_REC);
    CHECK(h1_loop_room() == 0, "and the loop is as it was");

    /* ---- saved and loaded whole: 2048 events through h1_loop ---- */
    p = (const uint8_t *)h1_loop(&size);
    CHECK(size == H1_LOOP_MAX, "a full loop saves as H1_LOOP_MAX bytes");
    memcpy(blob, p, size);
    for (i = 0; i + 1 < NLEV; i++) {             /* in order of tick, as it lies in RAM */
        uint32_t a = blob[12 + 8 * i] | blob[13 + 8 * i] << 8, b = blob[12 + 8 * (i + 1)] | blob[13 + 8 * (i + 1)] << 8;
        if (a > b) break;
    }
    CHECK(i + 1 == NLEV, "the saved events are in order of tick");
    tap(H1_BTN_PLAY);
    stop_and_clear();
    CHECK(h1_loop_load(blob, size) == 0, "the 2048-event loop loads");
    CHECK(h1_loop_room() == 0, "and is whole");
    tap(H1_BTN_PLAY);
    got = count_pass(len_now);
    CHECK(got == NLEV / 2, "the loaded loop plays every note");
    tap(H1_BTN_PLAY);
    until(now() + 300);
    CHECK(seq_gated() == 0, "stopped, the loop holds no note down");
    st.glo[0] = 112;
    st.seq[3] = 1;

    /* ---- a loop as 0.2.2 saved it: version 1, events as they were played (a later take's after an earlier
     * one's, whatever their ticks), two bars. It loads in order and plays every note ---- */
    memset(old, 0, sizeof old);
    old[0] = 0x48; old[1] = 0x31; old[2] = 0x4C; old[3] = 0x50; old[4] = 1;
    old[6] = 32; old[8] = 768 & 255; old[9] = 768 >> 8;
    for (i = 0; i < 32; i++) {                   /* take one: 8 notes on 0.., take two: 8 on 12.. between them */
        int t = i < 16, k = (i % 16) / 2, on = !(i & 1);
        uint32_t tick = (uint32_t)(k * 96 + (t ? 0 : 48) + (on ? 0 : 30));
        uint8_t *e = old + 12 + 8 * i;
        e[0] = (uint8_t)tick; e[1] = (uint8_t)(tick >> 8);
        e[2] = (uint8_t)(60 + (t ? 0 : 12) + k);
        e[3] = (uint8_t)on;
        e[4] = (uint8_t)(0x80 | 1 << 3 | 0);     /* 0.2.2's part: ORBIT, its first algorithm */
        e[6] = on ? 100 : 0;
    }
    stop_and_clear();
    CHECK(h1_loop_load(old, sizeof old - 8 * (384 - 32)) == 0, "a 0.2.2 loop loads");
    CHECK(h1_loop_room() == NLEV / 2 - 16, "with all 32 of its events");
    tap(H1_BTN_PLAY);
    got = count_pass(4.0 * 60000.0 / 112 * 2);
    printf("looper: a 0.2.2 loop of 16 notes: %d play back\n", got);
    CHECK(got == 16, "a 0.2.2 loop plays every note, in order");
    tap(H1_BTN_PLAY);

    /* ---- the length's bounds: a loop longer than 128 bars of 5/4 is refused, the BARS byte is kept in range ---- */
    old[6] = 0; old[7] = 0;
    old[8] = (uint8_t)61440; old[9] = (uint8_t)(61440 >> 8);
    CHECK(h1_loop_load(old, 12) == 0, "128 bars of 5/4 load");
    old[8] = (uint8_t)61441; old[9] = (uint8_t)(61441 >> 8);
    CHECK(h1_loop_load(old, 12) != 0, "and nothing longer");
    {
        static uint8_t sb[sizeof(H1State)];
        const void *sp;
        uint32_t ssz;
        st.seq[1] = 7;
        sp = h1_state(&ssz);
        memcpy(sb, sp, ssz);
        st.seq[1] = 1;
        CHECK(h1_state_load(sb, ssz) == 0 && st.seq[1] == 7, "BARS 128 is saved with the state");
    }

    printf("looper: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
