/* What is hidden in the pictures, and the noise each makes (the list is in core/internal.h). Run by
 * tests/run_tests.sh: the core alone, its keys worked as a hand would.
 *
 * A cue sounds and ends, and one that does not exist is nothing. Each hidden thing comes out for the way
 * of playing that is its own, once, and is ready again only after a pause. A loop that plays the same
 * notes back sets nothing off. */
#include <stdio.h>
#include <stdint.h>
#include "../core/internal.h"
#include "../core/h1.h"

static uint32_t ms;
static int fails, checks;
#define CHECK(c, what) do { checks++; if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, what); } } while (0)

static int32_t peak;
static void go(int msec)                         /* the sound and the frames, as the device runs them */
{
    static int32_t out[512];
    int blocks = msec * 441 / 2560 + 1, i, j;
    for (i = 0; i < blocks; i++) {
        h1_render(out, 256);
        for (j = 0; j < 512; j += 2) {
            int32_t a = out[j] < 0 ? -out[j] : out[j];
            if (a > peak) peak = a;
        }
        if (i % 3 == 0)
            h1_frame(ms += 17);
    }
}
static void tap(int b) { h1_button(b, 1); go(40); h1_button(b, 0); go(40); }
static void key(int k, int down_ms, int up_ms) { h1_key(k, 1); go(down_ms); h1_key(k, 0); go(up_ms); }
static void inst(int i) { h1_encoder(H1_ENC_PRESET, i - h1_instrument()); go(600); }
enum { COZY, ORBIT, KOI, HAUNT, TIDE, PIP, BONK, URCHIN, TWANG, LAVA, KNOT, INK, BARD, TURBO, TILT, PLUNK };

static void run_up(void)                         /* eight notes, each higher than the last */
{
    static const int K[8] = {0, 2, 4, 5, 7, 9, 11, 12};
    int i;
    for (i = 0; i < 8; i++)
        key(K[i], 90, 20);
}

int main(void)
{
    uint32_t n;
    int i, c;
    h1_init(44100);
    go(200);

    /* the cues themselves, on an instrument that is silent when nothing is played */
    inst(PIP);
    go(3000);
    peak = 0;
    go(500);
    CHECK(peak == 0, "PIP is silent with nothing played");
    for (c = 0; c < NCUE; c++) {
        peak = 0;
        eng_cue(c);
        go(150);
        CHECK(peak > 100000, "a cue sounds");
        CHECK(peak < 5000000, "and is not louder than a note");
        go(4000);
        peak = 0;
        go(300);
        CHECK(peak == 0, "and it ends");
    }
    n = eng_cues();
    CHECK(n == NCUE, "each of them was counted");
    eng_post(EV_CUE, NCUE);
    eng_post(EV_CUE, 255);
    peak = 0;
    go(300);
    CHECK(peak == 0 && eng_cues() == n, "a cue that does not exist is nothing");
    eng_cue(CUE_HAUNT);
    go(100);
    eng_post(EV_PANIC, 0);
    go(60);
    peak = 0;
    go(300);
    CHECK(peak == 0, "silencing everything silences a cue");

    /* PIP: a run of eight rising notes */
    n = eng_cues();
    for (i = 0; i < 7; i++)
        key(2 * i, 90, 20);
    go(600);
    CHECK(eng_cues() == n, "seven rising notes are not enough");
    go(3200);
    run_up();
    go(200);
    CHECK(eng_cues() == n + 1, "PIP: eight rising notes, one up");
    go(500);
    run_up();
    go(200);
    CHECK(eng_cues() == n + 1, "and not again straight away");
    go(3200);
    run_up();
    go(200);
    CHECK(eng_cues() == n + 2, "but again after a pause");
    go(3200);
    for (i = 0; i < 8; i++)
        key(12 - i, 90, 20);
    go(200);
    CHECK(eng_cues() == n + 2, "a run down is not a run up");

    /* a loop that plays the run back does not set it off */
    go(3200);
    n = eng_cues();
    tap(H1_BTN_REC);
    run_up();
    go(400);
    tap(H1_BTN_REC);                             /* the take closes and goes round */
    CHECK(eng_cues() == n + 1, "played into a take, it comes out");
    {
        SeqInfo si;
        go(3000);
        eng_seq_info(&si);
        CHECK(si.loop_state == LOOP_PLAY, "(the loop is going round)");
        peak = 0;
        go(6000);
        CHECK(peak > 100000, "(and is heard)");
    }
    CHECK(eng_cues() == n + 1, "the loop playing it back does not set it off");
    tap(H1_BTN_PLAY);
    h1_button(H1_BTN_REC, 1); go(3300); h1_button(H1_BTN_REC, 0); go(200);       /* (and the loop is cleared) */
    tap(H1_BTN_REC);                             /* (REC again: all of it) */
    go(200);

    /* TURBO: fast, for long enough */
    inst(TURBO);
    n = eng_cues();
    for (i = 0; i < 16; i++)
        key(i % 12, 150, 100);                   /* four notes a second is not fast */
    CHECK(eng_cues() == n, "TURBO: ordinary playing is not noticed");
    for (i = 0; i < 60 && eng_cues() == n; i++)
        key(i % 12, 40, 25);                     /* fifteen a second is */
    CHECK(eng_cues() == n + 1, "TURBO: fast enough for long enough is");
    for (i = 0; i < 60; i++)
        key(i % 12, 40, 25);
    CHECK(eng_cues() == n + 1, "and once");

    /* ORBIT: one note, alone, held */
    inst(ORBIT);
    n = eng_cues();
    h1_key(5, 1); h1_key(9, 1);
    go(6000);
    CHECK(eng_cues() == n, "ORBIT: two notes held are not one");
    h1_key(9, 0);
    h1_key(5, 0);
    go(300);
    h1_key(5, 1);
    go(19000);
    CHECK(eng_cues() == n, "ORBIT: nineteen seconds is not long enough (#62)");
    go(1500);
    CHECK(eng_cues() == n + 1, "ORBIT: one note held twenty seconds is answered");
    go(8000);
    CHECK(eng_cues() == n + 1, "and once, however long it is held");
    h1_key(5, 0);
    go(300);
    h1_key(5, 1);
    go(21000);
    h1_key(5, 0);
    go(300);
    CHECK(eng_cues() == n + 1, "ORBIT: not again within ten minutes (#62)");
    /* SETUP EGGS OFF: none of them, however they are asked for */
    opt[OPT_EGGS] = 1;
    inst(TURBO);
    n = eng_cues();
    for (i = 0; i < 80; i++)
        key(i % 12, 40, 25);
    CHECK(eng_cues() == n, "EGGS OFF: TURBO's siren stays away");
    inst(ORBIT);
    h1_key(5, 1);
    go(21000);
    h1_key(5, 0);
    go(300);
    CHECK(eng_cues() == n, "EGGS OFF: nothing comes to ORBIT");
    opt[OPT_EGGS] = 0;

    /* COZY: the same note, over and over */
    inst(COZY);
    n = eng_cues();
    for (i = 0; i < 20; i++)
        key(i & 1 ? 7 : 9, 80, 60);
    go(1000);
    CHECK(eng_cues() == n, "COZY: two notes in turn move nothing");
    for (i = 0; i < 14; i++)
        key(7, 80, 60);
    go(1000);
    CHECK(eng_cues() == n + 1, "COZY: the same note enough times, and the pot goes");

    /* KOI: five notes down at once */
    inst(KOI);
    n = eng_cues();
    for (i = 0; i < 4; i++) h1_key(3 * i, 1);
    go(600);
    CHECK(eng_cues() == n, "KOI: four notes are not five");
    h1_key(13, 1);
    go(300);
    CHECK(eng_cues() == n + 1, "KOI: five notes down at once");
    for (i = 0; i < 4; i++) h1_key(3 * i, 0);
    h1_key(13, 0);
    go(300);

    /* HAUNT: a tritone, bare */
    inst(HAUNT);
    n = eng_cues();
    h1_key(0, 1); h1_key(7, 1);
    go(2000);
    CHECK(eng_cues() == n, "HAUNT: a fifth is only a fifth");
    h1_key(7, 0); h1_key(6, 1);
    go(1500);
    CHECK(eng_cues() == n + 1, "HAUNT: a tritone held");
    h1_key(0, 0); h1_key(6, 0);
    go(300);

    /* TILT: hammered */
    inst(TILT);
    n = eng_cues();
    for (i = 0; i < 16; i++)
        key(7 + (i & 3), 60, 190);               /* a beat */
    CHECK(eng_cues() == n, "TILT: a beat does not tilt the table");
    for (i = 0; i < 40 && eng_cues() == n; i++) {
        h1_key(7, 1); h1_key(9, 1); h1_key(11, 1); h1_key(0, 1); go(30);
        h1_key(7, 0); h1_key(9, 0); h1_key(11, 0); h1_key(0, 0); go(20);
    }
    CHECK(eng_cues() == n + 1, "TILT: hammering does, and the buzzer goes");

    printf("eggs: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
