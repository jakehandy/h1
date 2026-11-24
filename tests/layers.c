/* LAYERS: with it on, what was recorded keeps the instrument and algorithm it was played on, in the
 * loop and in the steps, whatever is in hand afterwards; with it off, everything recorded sounds on
 * the instrument in hand. Run by tests/run_tests.sh: the core alone, its panel worked as a hand would.
 *
 * A voice knows whose sound it is (Voice.part) and whether the sequencer started it (Voice.seq), so
 * nothing here listens to the audio: it asks, while the sequencer plays, which parts are sounding. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../core/internal.h"
#include "../core/h1.h"

static uint32_t ms;
static int fails, checks;

static void dump(void);
#define CHECK(c, what) do { checks++; if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, what); dump(); } } while (0)

static void go(int msec)                         /* the sound and the frames, as the device runs them */
{
    static int32_t out[512];
    int blocks = msec * 441 / 2560 + 1, i;
    for (i = 0; i < blocks; i++) {
        h1_render(out, 256);
        if (i % 3 == 0)
            h1_frame(ms += 17);
    }
}

static void tap(int b) { h1_button(b, 1); go(40); h1_button(b, 0); go(40); }
static void hold(int b, int msec) { h1_button(b, 1); go(msec); h1_button(b, 0); go(40); }
static void pair(void)                           /* SEQ and REC together */
{
    h1_button(H1_BTN_SEQ, 1);
    go(30);
    h1_button(H1_BTN_REC, 1);
    go(30);
    h1_button(H1_BTN_REC, 0);
    h1_button(H1_BTN_SEQ, 0);
    go(40);
}
static void note(int key, int msec) { h1_key(key, 1); go(msec); h1_key(key, 0); go(60); }

/* Listen for a while. seen[] gets a mark for every part the sequencer sounded, played gets one for
 * every part a hand did; the count of sequencer notes heard comes back. */
static uint8_t seen[256], played[256];
static int listen(int msec)
{
    static int32_t out[512];
    int blocks = msec * 441 / 2560 + 1, i, v, n = 0;
    memset(seen, 0, sizeof seen);
    memset(played, 0, sizeof played);
    for (i = 0; i < blocks; i++) {
        h1_render(out, 256);
        if (i % 3 == 0)
            h1_frame(ms += 17);
        for (v = 0; v < 8; v++) {
            const Voice *vc = &voices[v];         /* a note held down now (its tail, once let go, is nobody's) */
            int part = PART_INST(vc->part) * 8 + PART_ALGO(vc->part);
            if (!vc->stage || !vc->gate)
                continue;
            if (vc->seq) { seen[part] = 1; n++; }
            else played[part] = 1;
        }
    }
    return n;
}
static void dump(void)                           /* what the last listen heard */
{
    int i;
    printf("  in hand %d.%d; sequencer:", h1_instrument(), h1_algorithm());
    for (i = 0; i < 256; i++) if (seen[i]) printf(" %d.%d", i / 8, i % 8);
    printf("; played:");
    for (i = 0; i < 256; i++) if (played[i]) printf(" %d.%d", i / 8, i % 8);
    printf("\n");
}
static int only(const uint8_t *m, int a, int b)   /* nothing marked but parts a and b (b < 0: only a) */
{
    int i;
    for (i = 0; i < 256; i++)
        if (m[i] && i != a && i != b)
            return 0;
    return 1;
}
static int part_now(void) { return h1_instrument() * 8 + h1_algorithm(); }

int main(void)
{
    int A, B, C, B2, n;
    static uint8_t blob[sizeof(H1State)], blob4[sizeof(H1State)];
    uint32_t size = 0;
    const void *p;

    h1_init(44100);
    go(100);

    /* ---- the loop ---- */
    h1_encoder(H1_ENC_PRESET, 1);
    h1_encoder(H1_ENC_ALGO, 2);
    go(100);
    A = part_now();
    CHECK(h1_layers(), "LAYERS is on on a new h1 (since 0.2.2)");
    pair();
    CHECK(!h1_layers(), "SEQ and REC together turn it off");
    pair();
    CHECK(h1_layers(), "SEQ and REC together turn LAYERS on");
    CHECK(h1_page() == 0, "the pair opens no page");
    tap(H1_BTN_REC);                             /* armed: the first key starts the take */
    note(5, 200);
    go(300);
    note(9, 200);
    go(300);
    note(12, 200);
    go(5000);                                    /* the take runs out its two bars and plays */
    h1_encoder(H1_ENC_PRESET, 2);
    h1_encoder(H1_ENC_ALGO, 3);
    go(600);
    B = part_now();
    CHECK(B / 8 != A / 8, "another instrument is in hand");
    n = listen(5000);
    CHECK(n > 0, "the loop plays on with another instrument in hand");
    CHECK(seen[A] && only(seen, A, -1), "the loop's notes are still the part they were played on");
    h1_key(14, 1);
    listen(150);
    h1_key(14, 0);
    CHECK(played[B] && only(played, B, -1), "a key plays the instrument in hand");
    go(300);
    tap(H1_BTN_REC);                             /* onto the playing loop */
    note(16, 150);
    note(19, 150);
    tap(H1_BTN_REC);
    n = listen(5000);
    CHECK(seen[A] && seen[B] && only(seen, A, B), "two parts play, each as it was recorded");
    h1_encoder(H1_ENC_ALGO, 1);                  /* the algorithm in hand moves on: what was recorded does not */
    go(300);
    B2 = part_now();
    CHECK(B2 != B, "the algorithm changed");
    listen(5000);
    CHECK(seen[A] && seen[B] && only(seen, A, B), "a recorded part keeps its algorithm");

    /* stopped, another instrument, REC: the take joins the loop and nothing is erased */
    tap(H1_BTN_PLAY);
    go(400);
    h1_encoder(H1_ENC_PRESET, 3);
    go(600);
    C = part_now();
    tap(H1_BTN_REC);
    note(7, 150);
    note(10, 150);
    tap(H1_BTN_REC);
    listen(5000);
    CHECK(seen[A] && seen[B] && seen[C], "a take over a stopped loop adds a third part");

    /* LAYERS off: everything on the instrument in hand, as it always was; and on again, each its own */
    pair();
    CHECK(!h1_layers(), "the pair again turns LAYERS off");
    go(400);
    n = listen(5000);
    CHECK(n == 0 && played[C] && only(played, C, -1), "LAYERS off: the loop plays on the instrument in hand");
    pair();
    go(400);
    listen(5000);
    CHECK(seen[A] && seen[B] && seen[C], "LAYERS on again: each part is its own again");

    /* hold REC: the part in hand goes, the others stay; held three seconds, everything goes */
    hold(H1_BTN_REC, 900);
    go(400);
    listen(5000);
    CHECK(seen[A] && seen[B] && !seen[C] && only(seen, A, B), "hold REC clears only the instrument in hand");
    hold(H1_BTN_REC, 900);                       /* (nothing of this instrument left: nothing happens) */
    go(400);
    listen(5000);
    CHECK(seen[A] && seen[B], "hold REC with nothing of this instrument's leaves the rest");
    hold(H1_BTN_REC, 3200);
    go(400);
    n = listen(5000);
    CHECK(n > 0, "hold REC three seconds only asks (#49)");
    hold(H1_BTN_REC, 3200);
    go(4000);
    hold(H1_BTN_REC, 60);                        /* (asked too long ago: a tap of REC then is only a tap) */
    go(400);
    n = listen(5000);
    CHECK(n > 0, "and the question goes after three seconds");
    hold(H1_BTN_REC, 3200);
    go(100);
    hold(H1_BTN_REC, 60);                        /* REC again: yes */
    go(400);
    n = listen(5000);
    CHECK(n == 0 && only(played, -1, -1), "hold REC three seconds, then REC again, clears everything");

    /* ---- the steps ---- */
    hold(H1_BTN_SEQ, 700);                       /* to STEP */
    CHECK(h1_layers(), "LAYERS is still on in STEP");
    tap(H1_BTN_REC);                             /* writing */
    note(3, 120);
    note(5, 120);
    h1_encoder(H1_ENC_PRESET, 1);
    h1_encoder(H1_ENC_ALGO, 2);
    go(600);
    B = part_now();
    CHECK(B != C, "another part is in hand");
    note(8, 120);
    h1_encoder(H1_ENC_SELECT, -3);               /* back to the first step: a second instrument on the same step */
    go(100);
    note(15, 120);
    tap(H1_BTN_REC);
    tap(H1_BTN_PLAY);
    h1_encoder(H1_ENC_PRESET, 4);
    go(600);
    n = listen(6000);
    CHECK(n > 0, "the steps play with a third instrument in hand");
    CHECK(seen[C] && seen[B] && only(seen, C, B), "each step note is the part it was written on");

    /* ---- saved and loaded ---- */
    p = h1_state(&size);
    CHECK(size == sizeof(H1State), "the state is one H1State");
    memcpy(blob, p, sizeof blob);
    h1_init(44100);
    CHECK(h1_state_load(blob, size) == 0, "the saved state loads");
    CHECK(h1_layers(), "LAYERS is saved");
    go(200);
    tap(H1_BTN_PLAY);
    listen(6000);
    CHECK(seen[C] && seen[B] && only(seen, C, B), "the steps keep their parts over a restart");

    /* the state before LAYERS (version 4: the same, without the parts): it loads, LAYERS off, steps kept */
    memset(blob4, 0xFF, sizeof blob4);           /* (its padding, and what is past its end, may hold anything) */
    memcpy(blob4, blob, offsetof(H1Old, ip) + H1_OLD_NINST * sizeof(InstParams));   /* (fourteen instruments then) */
    blob4[4] = 4;
    {
        const H1State *s6 = (const H1State *)(const void *)blob;
        int i, j;
        for (i = 0; i < NSTEP; i++)              /* (a step held four notes then) */
            for (j = 0; j < 4; j++)
                blob4[offsetof(H1Old, steps) + (size_t)i * 4u + (size_t)j] = s6->steps[i][j];
    }
    h1_init(44100);
    CHECK(h1_state_load(blob4 + 0, (uint32_t)H1_VERSION_4_SIZE) == 0, "a version 4 state loads");
    CHECK(h1_layers(), "a version 4 state comes up with LAYERS on (0.2.2 turns it on)");
    CHECK(h1_state_load(blob4, (uint32_t)H1_VERSION_4_SIZE - 1u) != 0, "a state of the wrong size is refused");
    go(200);
    tap(H1_BTN_PLAY);
    listen(6000);
    CHECK(only(seen, part_now(), -1) && only(played, -1, -1), "with LAYERS on, steps that kept no part follow the hand");
    pair();                                      /* off: everything through the hand's instrument */
    go(300);
    n = listen(6000);
    CHECK(n == 0 && played[part_now()] && only(played, part_now(), -1), "with LAYERS off its steps play on the instrument in hand");

    printf("layers: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
