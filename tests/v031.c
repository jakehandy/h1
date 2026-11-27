/* What 0.3.1 adds and fixes, on the core alone, its panel worked as a hand would. Run by tests/run_tests.sh.
 *
 *   ARP          a key held while the ARP is switched on still lets its note go (#56); HOLD ARP latches only
 *                while the ARP runs
 *   layer view   PLAY held with LAYERS: a white key mutes its layer, held it solos it, with SELECT its
 *                volume, with REC it is cleared (#55 #63); a quick tap of PLAY still stops
 *   layers       a layer's note starts on the layer's own knobs (TILT's BOUNCE, #52)
 *   MIDI page    CHAN, BEND, WHEEL, PEDAL: off as before until set
 *   SETUP page   saved in the state's spare bytes: a 0.3.0 state reads as every setting off, a damaged one too
 *   CPU guard    reported over its limit, fewer voices; back under, all of them
 *   EDIT, LEDs   EDIT on a page goes home with the knob strip; the keys show the steps only on SEQ (#49)
 *   silence      h1_silent only once the sound has died away */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../core/internal.h"
#include "../core/h1.h"

static uint32_t ms;
static int fails, checks;
static int32_t peak;
#define CHECK(c, what) do { checks++; if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, what); } } while (0)

static void go(int msec)
{
    static int32_t out[512];
    int blocks = msec * 441 / 2560 + 1, i, j;
    for (i = 0; i < blocks; i++) {
        h1_render(out, 256);
        for (j = 0; j < 512; j++) {
            int32_t a = out[j] < 0 ? -out[j] : out[j];
            if (a > peak) peak = a;
        }
        if (i % 3 == 0)
            h1_frame(ms += 17);
    }
}
static void tap(int b) { h1_button(b, 1); go(40); h1_button(b, 0); go(40); }
static void hold(int b, int msec) { h1_button(b, 1); go(msec); h1_button(b, 0); go(40); }
static void note(int key, int msec) { h1_key(key, 1); go(msec); h1_key(key, 0); go(60); }
static void inst(int i) { h1_encoder(H1_ENC_PRESET, i - h1_instrument()); go(600); }
static int gated(void)
{
    int v, n = 0;
    for (v = 0; v < NVOICE; v++)
        n += voices[v].stage && voices[v].gate;
    return n;
}
static int sounding(void)
{
    int v, n = 0;
    for (v = 0; v < NVOICE; v++)
        n += voices[v].stage != 0;
    return n;
}
/* the layers the sequencer sounds over a while: bit l - 1 */
static uint32_t heard(int msec)
{
    static int32_t out[512];
    int blocks = msec * 441 / 2560 + 1, i, v;
    uint32_t m = 0;
    for (i = 0; i < blocks; i++) {
        h1_render(out, 256);
        if (i % 3 == 0)
            h1_frame(ms += 17);
        for (v = 0; v < NVOICE; v++)
            if (voices[v].stage && voices[v].gate && voices[v].seq && voices[v].lay)
                m |= 1u << (voices[v].lay - 1);
    }
    return m;
}
enum { COZY, ORBIT, KOI, HAUNT, TIDE, PIP, BONK, URCHIN, TWANG, LAVA, KNOT, INK, BARD, TURBO, TILT, PLUNK };
static const int WHITE[16] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};

int main(void)
{
    int i;
    h1_init(44100);
    go(300);

    /* ---- #56: the ARP switched on while a key is down ---- */
    inst(COZY);
    h1_key(5, 1);
    go(200);
    CHECK(gated() == 1, "a key down sounds (ARP off)");
    st.ip[COZY].arp[0] = 1;                      /* (the ARP page's MODE turned to UP, the key still down) */
    go(200);
    h1_key(5, 0);
    go(400);
    CHECK(gated() == 0, "let go, its note ends though the ARP is on now (#56)");
    st.ip[COZY].arp[0] = 0;
    go(300);

    /* ---- KEYS HOLD ARP: holds only while the ARP runs ---- */
    st.keys[3] = 2;
    note(5, 150);
    go(200);
    CHECK(gated() == 0, "HOLD ARP with the ARP off: a note ends when let go");
    st.ip[COZY].arp[0] = 1;
    note(5, 150);
    go(400);
    CHECK(gated() > 0 || h1_voices_active(), "HOLD ARP with the ARP on: it goes on");
    st.ip[COZY].arp[0] = 0;
    go(600);
    CHECK(gated() == 0, "the ARP off: what it held goes");
    st.keys[3] = 0;
    go(300);

    /* ---- the MIDI page ---- */
    h1_midi(0x92, 60, 100);
    go(100);
    CHECK(gated() == 1, "CHAN ALL: a note on channel 3 plays");
    h1_midi(0x82, 60, 0);
    go(200);
    opt[OPT_CHAN] = 1;
    h1_midi(0x92, 60, 100);
    go(100);
    CHECK(gated() == 0, "CHAN 1: channel 3 is not heard");
    h1_midi(0x90, 60, 100);
    go(100);
    CHECK(gated() == 1, "CHAN 1: channel 1 is");
    h1_midi(0x80, 60, 0);
    go(200);
    opt[OPT_CHAN] = 0;
    h1_midi(0xE0, 0, 127);
    CHECK(eng_bend == 0.0f, "BEND OFF: the pitch bend does nothing");
    opt[OPT_BEND] = 1;
    h1_midi(0xE0, 127, 127);
    CHECK(eng_bend > 1.99f && eng_bend <= 2.0f, "BEND 2: up two semitones at the top");
    opt[OPT_BEND] = 2;
    h1_midi(0xE0, 0, 0);
    CHECK(eng_bend == -12.0f, "BEND 12: down an octave at the bottom");
    h1_midi(0xE0, 0, 64);
    CHECK(eng_bend == 0.0f, "and none in the middle");
    opt[OPT_BEND] = 0;
    h1_midi(0xB0, 1, 127);
    CHECK(eng_wheel == 0.0f, "WHEEL OFF: the mod wheel does nothing");
    opt[OPT_WHEEL] = 1;
    h1_midi(0xB0, 1, 127);
    CHECK(eng_wheel == 1.0f, "WHEEL ON: it does");
    h1_midi(0xB0, 1, 0);
    opt[OPT_WHEEL] = 0;
    h1_midi(0xB0, 64, 127);                      /* PEDAL OFF: CC 64 is nothing */
    h1_midi(0x90, 62, 100);
    go(100);
    h1_midi(0x80, 62, 0);
    go(300);
    CHECK(gated() == 0, "PEDAL OFF: a note ends when let go, pedal or not");
    opt[OPT_PEDAL] = 1;
    h1_midi(0xB0, 64, 127);
    h1_midi(0x90, 62, 100);
    go(100);
    h1_midi(0x80, 62, 0);
    go(300);
    CHECK(gated() == 1, "PEDAL ON, the pedal down: it goes on after it is let go");
    h1_key(0, 1);
    go(60);
    h1_key(0, 0);
    go(200);
    CHECK(gated() == 2, "the panel's keys too");
    h1_midi(0xB0, 64, 0);
    go(300);
    CHECK(gated() == 0, "the pedal up: all of them go");
    opt[OPT_PEDAL] = 0;

    /* ---- SETUP's settings are saved, and a 0.3.0 state has them off ---- */
    opt[OPT_ANIM] = 1;
    opt[OPT_SCREEN] = 2;
    opt[OPT_CHAN] = 16;
    opt_pack();
    {
        uint32_t size;
        static uint8_t copy[sizeof(H1State)];
        const void *now_st = h1_state(&size);
        memcpy(copy, now_st, size);
        memset(opt, 0, sizeof opt);
        CHECK(h1_state_load(copy, size) == 0, "the state loads");
        CHECK(opt[OPT_ANIM] == 1 && opt[OPT_SCREEN] == 2 && opt[OPT_CHAN] == 16 && opt[OPT_PEDAL] == 0, "with SETUP's and MIDI's settings");
        CHECK(h1_screen() == 2 && h1_midi_channel() == 16, "which the port reads");
        copy[offsetof(H1State, rsv)] = 0;            /* 0.3.0's: zeros */
        copy[offsetof(H1State, rsv) + 1] = 0;
        h1_state_load(copy, size);
        CHECK(opt[OPT_ANIM] == 0 && opt[OPT_SCREEN] == 0 && opt[OPT_CHAN] == 0, "a 0.3.0 state: every one off");
        copy[offsetof(H1State, rsv)] = 0xFF;         /* damaged */
        copy[offsetof(H1State, rsv) + 1] = 0xFF;
        h1_state_load(copy, size);
        for (i = 0; i < NOPT; i++)
            CHECK(opt[i] <= (i == OPT_CHAN || i >= OPT_MAXVOL ? 24 : 2), "a damaged one: every one in its range");
        CHECK(opt[OPT_CHAN] == 0 && opt[OPT_BEND] == 0, "(out of range: off)");
        memset(opt, 0, sizeof opt);
        opt_pack();
    }
    go(300);

    /* ---- EDIT on a page: home, with the knob strip (#49) ---- */
    st.hud = 0;
    tap(H1_BTN_FX);
    CHECK(h1_page() == PG_FX, "FX opens its page");
    tap(H1_BTN_EDIT);
    CHECK(h1_page() == PG_HOME && st.hud == 1, "EDIT there: home, the knob strip up");
    tap(H1_BTN_EDIT);
    CHECK(st.hud == 0, "EDIT at home: as before, the strip down");

    /* ---- SELECT goes on from SEQ to MIDI and SETUP ---- */
    tap(H1_BTN_SEQ);
    h1_encoder(H1_ENC_SELECT, 1);
    CHECK(h1_page() == PG_MIDI, "SELECT from SEQ: MIDI");
    h1_encoder(H1_ENC_SELECT, 1);
    CHECK(h1_page() == PG_SETUP, "then SETUP");
    h1_encoder(H1_ENC_K1, 1);
    CHECK(opt[OPT_ANIM] == 1 && (st.rsv[1] >> 2 & 1u), "its first knob: ANIM OFF, saved");
    go(300);
    tap(H1_BTN_HOME);
    go(300);
    h1_encoder(H1_ENC_K1, -1);                   /* (home: a knob of the instrument's) */
    tap(H1_BTN_SEQ);
    h1_encoder(H1_ENC_SELECT, 1);
    h1_encoder(H1_ENC_SELECT, 1);
    h1_encoder(H1_ENC_K1, -1);
    CHECK(opt[OPT_ANIM] == 0, "and back");
    h1_encoder(H1_ENC_SELECT, 1);
    CHECK(h1_page() == PG_PLAY, "past SETUP: PLAY (0.4.0)");
    h1_encoder(H1_ENC_SELECT, 1);
    CHECK(h1_page() == PG_FX, "past PLAY: round to FX");
    tap(H1_BTN_HOME);
    go(300);

    /* ---- the layer view ---- */
    CHECK(h1_layers(), "LAYERS is on");
    inst(COZY);
    tap(H1_BTN_REC);
    note(0, 200);
    note(4, 200);
    go(2200);
    tap(H1_BTN_REC);                             /* layer 1: COZY */
    inst(ORBIT);
    tap(H1_BTN_REC);
    note(7, 300);
    go(1500);
    tap(H1_BTN_REC);                             /* layer 2: ORBIT, over it */
    go(5000);
    CHECK(st.layer[0].part && st.layer[1].part, "two layers");
    CHECK(heard(5000) == 3u, "both are heard");
    h1_button(H1_BTN_PLAY, 1);
    go(400);
    CHECK(h1_key_leds() == (1u << WHITE[0] | 1u << WHITE[1]), "PLAY held: the white keys show the layers there");
    h1_key(WHITE[0], 1); go(100); h1_key(WHITE[0], 0); go(40);
    h1_button(H1_BTN_PLAY, 0);
    go(100);
    CHECK(st.layer[0].rsv[1] & LAY_MUTE, "a white key's tap mutes its layer");
    CHECK(heard(5000) == 2u, "and it is not heard; the loop plays on");
    h1_button(H1_BTN_PLAY, 1);
    go(100);
    h1_key(WHITE[0], 1); go(100); h1_key(WHITE[0], 0); go(40);
    h1_key(WHITE[1], 1); go(1000); h1_key(WHITE[1], 0); go(40);   /* layer 2 held: solo */
    h1_button(H1_BTN_PLAY, 0);
    go(100);
    CHECK(!(st.layer[0].rsv[1] & LAY_MUTE) && (st.layer[1].rsv[1] & LAY_SOLO), "a tap unmutes, a hold solos");
    CHECK(heard(5000) == 2u, "solo: only that one heard");
    h1_button(H1_BTN_PLAY, 1);
    go(100);
    h1_key(WHITE[1], 1); go(1000); h1_key(WHITE[1], 0); go(40);
    h1_key(WHITE[0], 1);
    go(100);
    for (i = 0; i < 10; i++) {
        h1_encoder(H1_ENC_SELECT, -1);           /* (a click at a time: no acceleration) */
        go(120);
    }
    h1_key(WHITE[0], 0);
    go(40);
    h1_button(H1_BTN_PLAY, 0);
    go(100);
    CHECK(!(st.layer[1].rsv[1] & LAY_SOLO), "held again: solo off");
    CHECK(st.layer[0].rsv[0] == 10 && !(st.layer[0].rsv[1] & LAY_MUTE), "a key held with SELECT: its volume, and no mute");
    CHECK(heard(5000) == 3u, "both heard again");
    {
        SeqInfo si;
        eng_seq_info(&si);
        CHECK(si.loop_state == LOOP_PLAY, "none of that stopped the loop");
    }
    h1_button(H1_BTN_PLAY, 1);
    go(100);
    h1_key(WHITE[0], 1);
    go(100);
    tap(H1_BTN_REC);
    h1_key(WHITE[0], 0);
    go(40);
    h1_button(H1_BTN_PLAY, 0);
    go(300);
    CHECK(!st.layer[0].part && st.layer[1].part, "a key held with REC: that layer cleared, the other kept");
    CHECK(heard(5000) == 2u, "and only the other heard");
    tap(H1_BTN_PLAY);
    go(200);
    {
        SeqInfo si;
        eng_seq_info(&si);
        CHECK(si.loop_state == LOOP_IDLE, "a quick tap of PLAY still stops it");
    }
    tap(H1_BTN_PLAY);
    go(200);
    {
        SeqInfo si;
        eng_seq_info(&si);
        CHECK(si.loop_state == LOOP_PLAY, "and starts it at once");
    }

    /* ---- the keys show what sounds, the steps only on SEQ (#49) ---- */
    go(3000);
    {
        uint32_t lit = 0, again = 0;
        for (i = 0; i < 100; i++) {
            go(30);
            lit |= h1_key_leds();
        }
        tap(H1_BTN_SEQ);
        for (i = 0; i < 100; i++) {
            go(30);
            again |= h1_key_leds();
        }
        tap(H1_BTN_HOME);
        CHECK(again != lit, "the SEQ page shows the loop on the keys, home shows what sounds");
    }

    /* ---- a layer's note starts on the layer's knobs: TILT's BOUNCE (#52) ---- */
    hold(H1_BTN_REC, 3200);
    tap(H1_BTN_REC);
    go(300);
    inst(TILT);
    KNOBS_NOW(TILT)[2] = 0;                      /* BOUNCE all the way down */
    go(200);
    tap(H1_BTN_REC);
    note(0, 100);
    note(2, 100);
    go(2600);
    tap(H1_BTN_REC);
    go(200);
    KNOBS_NOW(TILT)[2] = 100;                    /* and up, after the take */
    {
        static int32_t out[512];
        f32 most = 0.0f;
        int k, v, seqd = 0;
        for (k = 0; k < 2000; k++) {
            h1_render(out, 256);
            if (k % 3 == 0)
                h1_frame(ms += 17);
            for (v = 0; v < NVOICE; v++)
                if (voices[v].stage && voices[v].seq && voices[v].lay) {
                    seqd = 1;
                    if (voices[v].s[6] > most) most = voices[v].s[6];
                }
        }
        CHECK(seqd, "the layer plays");
        CHECK(most < 0.01f, "BOUNCE turned up after the take leaves the layer's drums as they were (#52)");
    }
    h1_key(0, 1);
    go(10);
    {
        int v;
        f32 most = 0.0f;
        for (v = 0; v < NVOICE; v++)
            if (voices[v].stage && !voices[v].seq && voices[v].s[6] > most) most = voices[v].s[6];
        CHECK(most > 0.5f, "and a drum played now bounces");
    }
    h1_key(0, 0);

    /* ---- the CPU guard ---- */
    hold(H1_BTN_REC, 3200);
    tap(H1_BTN_REC);                             /* (everything cleared) */
    go(300);
    inst(HAUNT);
    for (i = 0; i < 400; i++)
        h1_load(150);
    CHECK(eng_vcap() == 3, "over its time for long enough: three voices");
    for (i = 0; i < 8; i++)
        h1_key(i, 1);
    go(300);

    CHECK(sounding() <= 3 + 1, "eight keys down: no more than that sound");
    for (i = 0; i < 8; i++)
        h1_key(i, 0);
    for (i = 0; i < 400; i++)
        h1_load(30);
    CHECK(eng_vcap() == NVOICE, "back under: all of them");
    go(300);
    for (i = 0; i < 8; i++)
        h1_key(i, 1);
    go(300);
    CHECK(gated() == 8, "eight keys, eight voices");
    for (i = 0; i < 8; i++)
        h1_key(i, 0);

    /* ---- silence ---- */
    go(15000);                                   /* (HAUNT's tails let go) */
    inst(ORBIT);
    note(5, 200);
    go(300);
    CHECK(!h1_silent(), "a note dying away is not silence");
    go(10000);
    CHECK(h1_silent(), "gone: silence");
    st.ip[ORBIT].fx[0] = 80;                     /* a long echo */
    note(5, 200);
    go(1500);
    CHECK(!h1_silent(), "an echo still ringing is not silence");
    go(14000);
    CHECK(h1_silent(), "six seconds after the last note: silence, however faint the echo goes on");
    st.ip[ORBIT].fx[0] = 0;
    inst(COZY);                                  /* COZY crackles for ever */
    note(5, 200);
    go(14000);
    CHECK(h1_silent(), "COZY: six seconds after the last note");

    printf("v031: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
