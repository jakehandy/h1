/* What 0.4.0 adds, on the core alone, its panel worked as a hand would. Run by tests/run_tests.sh.
 *
 *   take length  with LAYERS on and a loop there, BARS is the next take's length (#69): longer, and the loop
 *                is repeated out to it; shorter, and the take closes itself once that long and is repeated
 *                out over the loop
 *   PLAY page    FX ALL, MAX VOL, KEY VEL, TRANSP, saved in the state's spare bytes; LEDS SCALE */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../core/internal.h"
#include "../core/h1.h"

static uint32_t ms;
static int fails, checks;
#define CHECK(c, what) do { checks++; if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, what); } } while (0)

static void go(int msec)
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
static void note(int key, int msec) { h1_key(key, 1); go(msec); h1_key(key, 0); go(60); }
static void inst(int i) { h1_encoder(H1_ENC_PRESET, i - h1_instrument()); go(600); }

/* the loop's length in ticks, and its note-ons on layer lay (0: all), as h1_loop gives them */
static uint32_t loop_ticks(void)
{
    uint32_t n;
    const uint8_t *p = (const uint8_t *)h1_loop(&n);
    return (uint32_t)p[8] | (uint32_t)p[9] << 8;
}
static int ons(int lay, uint32_t *first, uint32_t *last)
{
    uint32_t n, i, ne, c = 0;
    const uint8_t *p = (const uint8_t *)h1_loop(&n);
    ne = (uint32_t)p[6] | (uint32_t)p[7] << 8;
    if (first) *first = 0xFFFFu;
    if (last) *last = 0;
    for (i = 0; i < ne; i++) {
        const uint8_t *e = p + 12 + i * 8;
        uint32_t t = (uint32_t)e[0] | (uint32_t)e[1] << 8;
        if ((e[3] & 1) && (!lay || e[5] == lay)) {
            c++;
            if (first && t < *first) *first = t;
            if (last && t > *last) *last = t;
        }
    }
    return (int)c;
}
enum { COZY, ORBIT, KOI, HAUNT, TIDE, PIP, BONK };

int main(void)
{
    uint32_t bar, a, b;
    int n1;
    h1_init(44100);
    go(300);
    st.seq[6] = 1;                               /* LAYERS on */
    st.seq[3] = 0;                               /* SNAP FREE (the notes where they were played) */

    /* ---- a 2-bar take, then an 8-bar one: the 2 bars are repeated out to 8 ---- */
    st.seq[1] = 1;                               /* BARS 2 */
    inst(BONK);
    tap(H1_BTN_REC);
    note(0, 100);
    go(300);
    note(4, 100);
    go(5000);                                    /* (past the 2 bars: the take is played back) */
    tap(H1_BTN_REC);
    go(200);
    bar = loop_ticks() / 2u;
    n1 = ons(1, 0, 0);
    CHECK(n1 == 2, "a 2-bar loop, two notes on layer 1");
    tap(H1_BTN_PLAY);                            /* stop */
    go(200);
    st.seq[1] = 3;                               /* BARS 8 */
    inst(COZY);
    tap(H1_BTN_REC);
    note(7, 200);
    go(400);
    CHECK(loop_ticks() == 8u * bar, "BARS 8 over a 2-bar loop: the loop is 8 bars");
    CHECK(ons(1, &a, &b) == 4 * n1 && b >= 6u * bar, "layer 1 repeated out, four times");
    tap(H1_BTN_REC);
    go(300);
    CHECK(ons(2, 0, 0) == 1, "and the new take on layer 2, once");
    tap(H1_BTN_PLAY);
    go(200);

    /* ---- BARS 2 over the 8-bar loop: the take closes itself after 2 bars and repeats ---- */
    st.seq[1] = 1;
    inst(KOI);
    tap(H1_BTN_REC);
    note(12, 150);
    go(500);
    note(14, 150);
    go(2 * 2000 + 1500);                         /* (2 bars at the tempo, and more: closed by then) */
    CHECK(ons(3, &a, &b) == 2 * 4, "the 2-bar take on layer 3, repeated four times over 8 bars");
    CHECK(b >= 6u * bar, "out to the loop's last 2 bars");
    CHECK(loop_ticks() == 8u * bar, "the loop stays 8 bars");
    CHECK(!h1_layers() || !eng_loop_taking(), "the take closed on its own");
    note(16, 150);
    go(300);
    CHECK(ons(3, 0, 0) == 8, "a note after it is not recorded");

    /* ---- the PLAY page: FX ALL, MAX VOL, KEY VEL, TRANSP; LEDS SCALE ---- */
    tap(H1_BTN_PLAY);
    go(200);
    tap(H1_BTN_HOME);
    tap(H1_BTN_SEQ);
    h1_encoder(H1_ENC_SELECT, 1);
    h1_encoder(H1_ENC_SELECT, 1);
    h1_encoder(H1_ENC_SELECT, 1);
    CHECK(h1_page() == PG_PLAY, "SELECT from SEQ, three on: PLAY");
    CHECK(!opt[OPT_FX] && !opt[OPT_MAXVOL] && !opt[OPT_KEYVEL] && opt[OPT_TRANS] == TRANS_MID,
          "every PLAY setting as before until set");
    st.ip[h1_instrument()].fx[0] = 37;
    h1_encoder(H1_ENC_K1, 1);
    CHECK(opt[OPT_FX] == 1, "FX ALL");
    CHECK(st.ip[COZY].fx[0] == 37 && st.ip[BONK].fx[0] == 37, "every instrument takes the FX in hand");
    { int k; for (k = 0; k < 5; k++) { h1_encoder(H1_ENC_K2, 1); go(150); } }
    CHECK(h1_volume_cap() == 500, "MAX VOL 50%");
    {
        int k;
        for (k = 0; k < 4; k++) { h1_encoder(H1_ENC_K3, 1); go(150); }
        for (k = 0; k < 3; k++) { h1_encoder(H1_ENC_K4, 1); go(150); }
    }
    CHECK(opt[OPT_TRANS] == TRANS_MID + 3, "TRANSP +3");
    {
        uint8_t keep[4];
        memcpy(keep, st.rsv, 4);
        opt[OPT_FX] = opt[OPT_MAXVOL] = opt[OPT_KEYVEL] = 0;
        opt[OPT_TRANS] = TRANS_MID;
        opt_unpack();
        CHECK(opt[OPT_FX] == 1 && opt[OPT_MAXVOL] == 5 && opt[OPT_KEYVEL] == 4 && opt[OPT_TRANS] == TRANS_MID + 3,
              "all four saved in the state's spare bytes");
        st.rsv[2] = st.rsv[3] = 0;               /* a 0.3.x state */
        opt_unpack();
        CHECK(!opt[OPT_MAXVOL] && !opt[OPT_KEYVEL] && opt[OPT_TRANS] == TRANS_MID, "an older state: as before");
        memcpy(st.rsv, keep, 4);
        opt_unpack();
    }
    tap(H1_BTN_HOME);
    go(200);
    tap(H1_BTN_FX);
    h1_encoder(H1_ENC_K3, 3);
    CHECK(st.ip[COZY].fx[2] == st.ip[KOI].fx[2] && st.ip[KOI].fx[2] == st.ip[h1_instrument()].fx[2],
          "FX ALL: a change on the FX page goes to every instrument");
    tap(H1_BTN_HOME);
    go(200);
    {
        int v, found = 0;
        st.octave = 0;
        st.keys[1] = 0;
        h1_key(0, 1);
        go(100);
        for (v = 0; v < NVOICE; v++)
            if (voices[v].stage && voices[v].gate && voices[v].note == 53 + 3) {
                found = 1;
                CHECK(voices[v].vel == 64, "KEY VEL 64: the panel's key plays at 64");
            }
        CHECK(found, "TRANSP +3: the first key plays F + 3");
        CHECK(h1_key_leds() & 1u, "its light is still the key played");
        h1_key(0, 0);
        go(300);
    }
    opt[OPT_LEDS] = 2;
    st.keys[0] = 0;
    st.keys[1] = 1;                              /* C MAJOR */
    {
        uint32_t m = h1_key_leds();
        CHECK(!(m & 1u << 1) && (m & 1u << 0) && (m & 1u << 2), "LEDS SCALE: F lit, F# dark, G lit");
    }
    opt[OPT_LEDS] = 0;
    st.keys[1] = 0;

    printf("v040: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
