/* SONG (0.4.0): parts made of the layers, on the core alone, its panel worked as a hand would. Run by
 * tests/run_tests.sh.
 *
 *   with PLAY held (LAYERS on): OCT+ makes a part of the layers heard now, OCT- takes the last one off, EDIT
 *   turns the song on and off. On, the loop plays a part a pass, the last back to the first. The parts are
 *   saved with the loop, a 0.3.3 record (no parts) still loads, CLEAR ALL clears them. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../core/internal.h"
#include "../core/h1.h"

static uint32_t ms;
static int fails, checks;
#define CHECK(c, what) do { checks++; if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, what); } } while (0)

static double energy;
static void go(int msec)
{
    static int32_t out[512];
    int blocks = msec * 441 / 2560 + 1, i, k;
    for (i = 0; i < blocks; i++) {
        h1_render(out, 256);
        for (k = 0; k < 512; k++)
            energy += (double)out[k] * (double)out[k];
        if (i % 3 == 0)
            h1_frame(ms += 17);
    }
}
static void tap(int b) { h1_button(b, 1); go(40); h1_button(b, 0); go(40); }
static void note(int key, int msec) { h1_key(key, 1); go(msec); h1_key(key, 0); go(60); }
static void inst(int i) { h1_encoder(H1_ENC_PRESET, i - h1_instrument()); go(600); }
static void play_down(void) { h1_button(H1_BTN_PLAY, 1); go(400); }
static void play_up(void) { h1_button(H1_BTN_PLAY, 0); go(40); }
static void lv_tap(int key) { h1_key(key, 1); go(40); h1_key(key, 0); go(40); }   /* (a layer key: mute) */
static f32 pos(void) { SeqInfo si; eng_seq_info(&si); return si.loop_pos; }
static void to_top(void)                         /* on until the loop has just come round */
{
    int i;
    for (i = 0; i < 400 && pos() > 0.05f; i++)
        go(10);
}
enum { COZY, ORBIT, KOI, HAUNT, TIDE, PIP, BONK };

int main(void)
{
    static uint8_t blob[H1_LOOP_MAX + H1_SONG_MAX], old[H1_LOOP_MAX + H1_SONG_MAX];
    const uint8_t *p;
    uint32_t n, ns;
    double e1, e2;
    int i, part0;
    h1_init(44100);
    go(300);
    st.seq[6] = 1;                               /* LAYERS on */
    st.seq[3] = 0;                               /* SNAP FREE */
    st.seq[1] = 0;                               /* BARS 1 */

    /* ---- two layers, held notes through the bar ---- */
    inst(COZY);
    tap(H1_BTN_REC);
    note(0, 1500);
    go(2500);
    tap(H1_BTN_REC);                             /* (playing: overdub, a new layer) */
    inst(KOI);
    note(7, 1500);
    go(2500);
    tap(H1_BTN_REC);
    go(200);
    CHECK(eng_layer_count() == 2, "two layers");

    /* ---- parts: none heard, then both ---- */
    play_down();
    lv_tap(0);
    lv_tap(2);                                   /* layers 1 and 2 muted */
    tap(H1_BTN_OCTUP);
    CHECK(songp.n == 1 && songp.mask[0] == 0 && songp.known == 3, "part 1: nothing heard");
    lv_tap(0);
    lv_tap(2);
    tap(H1_BTN_OCTUP);
    CHECK(songp.n == 2 && songp.mask[1] == 3, "part 2: both layers");
    tap(H1_BTN_OCTUP);
    tap(H1_BTN_OCTDN);
    CHECK(songp.n == 2, "a third part, then taken off");
    CHECK(!songp.on, "the song not on yet");
    tap(H1_BTN_EDIT);
    CHECK(songp.on, "EDIT with PLAY held: the song on");
    play_up();
    CHECK(st.octave == 0, "OCT with PLAY held did not change the octave");

    /* ---- it plays a part a pass ---- */
    to_top();
    go(100);
    part0 = eng_song_part();
    energy = 0; go(1200); e1 = energy;
    to_top();
    go(100);
    CHECK(eng_song_part() == (part0 + 1) % 2, "the next part as the loop comes round");
    energy = 0; go(1200); e2 = energy;
    if (part0 == 1) { double t = e1; e1 = e2; e2 = t; }
    CHECK(e2 > 10.0 * (e1 + 1.0), "part 1 is silent and part 2 sounds");
    to_top();
    go(100);
    CHECK(eng_song_part() == part0, "and back to the first after the last");

    /* ---- saved with the loop, and back ---- */
    CHECK(h1_loop_dirty(), "the parts make the loop's record due");
    p = (const uint8_t *)h1_loop(&n);
    memcpy(blob, p, n);
    p = (const uint8_t *)h1_song(&ns);
    CHECK(ns == H1_SONG_MAX, "the song's size");
    memcpy(blob + n, p, ns);
    memcpy(old, blob, n + ns);
    tap(H1_BTN_PLAY);                            /* (stop) */
    go(200);
    songp.on = songp.n = 0;
    CHECK(h1_loop_load(blob, n + ns) == 0, "the record loads");
    CHECK(songp.on && songp.n == 2 && songp.mask[1] == 3 && songp.known == 3, "with its parts");
    CHECK(h1_loop_load(old, n + ns - (uint32_t)sizeof songp) == 0, "a 0.3.3 record loads");
    CHECK(!songp.on && songp.n == 0 && songp.known == 0, "with no parts");
    h1_loop_load(blob, n + ns);
    old[n + ns - sizeof songp + 1] = 200;        /* (n out of range) */
    h1_loop_load(old, n + ns);
    CHECK(songp.n == 0 && !songp.on, "a bad count of parts is none");
    h1_loop_load(blob, n + ns);

    /* ---- clearing a layer drops it from the parts; CLEAR ALL drops the song ---- */
    eng_post(EV_CLEAR_LAY, 2);
    go(200);
    CHECK(songp.known == 1 && songp.mask[1] == 1, "layer 2 cleared: out of every part");
    eng_post(EV_CLEAR, 0);
    go(200);
    CHECK(songp.n == 0 && !songp.on && songp.known == 0, "CLEAR ALL: no song");
    play_down();
    tap(H1_BTN_EDIT);
    CHECK(!songp.on, "no parts: EDIT does not turn it on");
    tap(H1_BTN_OCTUP);
    CHECK(songp.n == 0, "no layers: no part");
    play_up();
    for (i = 0; i < 3; i++)
        go(100);

    printf("song mode: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
