/* What 0.3.3 fixes in the loop's saving, on the core alone. Run by tests/run_tests.sh.
 *
 *   clear      CLEAR ALL lets go of the layers too: the layer view shows none, and a solo left on one hushes
 *              nothing recorded after (#55, #71)
 *   song       the steps and the layers go with the loop's record (h1_song), so they keep through a power-off
 *              while it plays: the state, saved only once nothing sounds, may not have them (#39, #71)
 *   arp        with SNAP on, the arpeggiator's notes go in on its own RATE's grid, not on 1/16 (#49)
 *   old record a loop saved by 0.3.2 (no song after it) over a state that lost its layers: the layers come back */
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
static void hold(int b, int msec) { h1_button(b, 1); go(msec); h1_button(b, 0); go(40); }
static void note(int key, int msec) { h1_key(key, 1); go(msec); h1_key(key, 0); go(60); }
static void inst(int i) { h1_encoder(H1_ENC_PRESET, i - h1_instrument()); go(600); }
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
static int nlayers(void)
{
    int i, n = 0;
    for (i = 0; i < NLAYER; i++)
        n += st.layer[i].part != 0;
    return n;
}
static void take(int key)
{
    tap(H1_BTN_REC);
    note(key, 200);
    go(2200);
    tap(H1_BTN_REC);
    go(300);
}
static void clear_all(void) { hold(H1_BTN_REC, 3200); tap(H1_BTN_REC); go(300); }
enum { COZY, ORBIT, KOI };

static uint8_t sblob[8192], lblob[H1_LOOP_MAX + H1_SONG_MAX];

int main(void)
{
    uint32_t size, ls, ss;
    const uint8_t *p;
    h1_init(44100);
    go(300);
    st.seq[6] = 1;                               /* LAYERS on */

    /* ---- CLEAR ALL takes the layers with it ---- */
    inst(COZY);
    take(0);
    inst(ORBIT);
    take(7);
    CHECK(nlayers() == 2, "two layers");
    st.layer[1].rsv[1] = LAY_SOLO;               /* the second soloed */
    st.layer[0].rsv[0] = 60;
    clear_all();
    CHECK(nlayers() == 0, "CLEAR ALL: no layer left for the layer view to show");
    inst(KOI);
    take(4);
    CHECK(nlayers() == 1 && st.layer[0].rsv[0] == 0 && !st.layer[0].rsv[1], "a new take: one layer, at full, heard");
    CHECK(heard(4000) == 1u, "and it plays (no solo left over hushes it)");
    clear_all();

    /* ---- the steps and the layers go with the loop's record ---- */
    inst(COZY);
    take(0);
    p = (const uint8_t *)h1_state(&size);         /* the state as saved now: one layer, no steps */
    memcpy(sblob, p, size);
    h1_state_saved();
    inst(ORBIT);
    take(7);                                     /* a second layer while it plays */
    st.steps[3][0] = 61;                         /* and a step written */
    st.step_part[3][0] = st.layer[1].part;
    st.step_lay[3][0] = 2;
    st.layer[1].rsv[1] = LAY_MUTE;
    CHECK(h1_loop_dirty(), "a change to the steps or the layers asks for a save");
    p = (const uint8_t *)h1_loop(&ls);
    memcpy(lblob, p, ls);
    p = (const uint8_t *)h1_song(&ss);
    CHECK(ss == H1_SONG_MAX, "the song's size");
    memcpy(lblob + ls, p, ss);
    h1_loop_saved();
    CHECK(!h1_loop_dirty(), "clean once saved");
    st.steps[3][1] = 62;
    CHECK(h1_loop_dirty(), "a step more: dirty again");
    st.steps[3][1] = 0;
    h1_init(44100);                              /* a power-off: the state from before, the record from after */
    go(100);
    CHECK(h1_state_load(sblob, size) == 0, "the old state loads");
    CHECK(nlayers() == 1 && !st.steps[3][0], "(it has one layer and no step)");
    CHECK(h1_loop_load(lblob, ls + ss) == 0, "the record loads");
    CHECK(nlayers() == 2 && st.layer[1].rsv[1] == LAY_MUTE, "both layers back, the mute with them");
    CHECK(st.steps[3][0] == 61 && st.step_lay[3][0] == 2, "the step back");
    CHECK(!h1_loop_dirty(), "and nothing to save after a load");
    st.seq[6] = 1;
    tap(H1_BTN_PLAY);
    CHECK(heard(5000) == 1u, "layer 1 plays, layer 2 muted");
    tap(H1_BTN_PLAY);
    go(300);

    /* ---- a 0.3.2 record (no song) over a state that lost the layers ---- */
    h1_init(44100);
    go(100);
    CHECK(h1_state_load(sblob, size) == 0, "the old state");
    memset(st.layer, 0, sizeof st.layer);        /* (as a state saved before the takes) */
    CHECK(h1_loop_load(lblob, ls) == 0, "the loop alone loads");
    CHECK(nlayers() == 2, "its two layers come back, from the notes");
    st.seq[6] = 1;
    tap(H1_BTN_PLAY);
    CHECK(heard(5000) == 3u, "both play");

    /* ---- the arpeggiator's notes recorded on its own RATE (#49) ---- */
    tap(H1_BTN_PLAY);
    clear_all();
    inst(COZY);
    st.ip[COZY].arp[0] = 1;                      /* UP */
    st.ip[COZY].arp[1] = 2;                      /* 1/8T: 32 ticks a step */
    tap(H1_BTN_REC);
    h1_key(0, 1);
    go(1500);
    h1_key(0, 0);
    go(1200);
    tap(H1_BTN_REC);
    go(200);
    {
        uint32_t i, ne, on = 0, off = 0;
        p = (const uint8_t *)h1_loop(&ls);
        ne = (uint32_t)p[6] | (uint32_t)p[7] << 8;
        for (i = 0; i < ne; i++) {
            const uint8_t *e = p + 12 + i * 8;
            if (e[3] & 1) {
                on++;
                off += (((uint32_t)e[0] | (uint32_t)e[1] << 8) % 32u) != 0;
            }
        }
        CHECK(on >= 4, "the ARP's notes went in");
        CHECK(off == 0, "each on the 1/8T grid, not SNAP's 1/16");
    }
    st.ip[COZY].arp[0] = 0;

    printf("song: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
