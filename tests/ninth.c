/* LAVA's ninth algorithm, HOOVER, and what it took: an instrument may have nine algorithms, so a part
 * (an instrument and its algorithm, what a recorded note remembers) is counted, not packed in bits, the
 * state is version 9 and a loop saved is version 2. Run by tests/run_tests.sh: the core alone.
 *
 *   ALGORITHM     LAVA turns through nine, every other instrument through eight
 *   HOOVER        sounds, about as loud as LAVA's other algorithms
 *   version 8     a 0.2.2 state loads: its knobs, its steps' and layers' parts read the old way
 *   slots         a version 9 state saved with fewer instruments: the ones after come up as new
 *   loop v1       a 0.2.2 loop loads, its notes' parts read the old way
 *   round trip    HOOVER, its knobs and a loop played on it come back from a save */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../core/internal.h"
#include "../core/h1.h"

#define OLD_PART(inst, algo) ((uint8_t)(0x80u | ((uint32_t)(inst) << 3) | ((uint32_t)(algo) & 7u)))
#define LAVA 9

static uint32_t ms;
static int fails, checks;
#define CHECK(c, what) do { checks++; if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, what); } } while (0)

static double energy;
static int32_t peak;
static void go(int msec)
{
    static int32_t out[512];
    int blocks = msec * 441 / 2560 + 1, i, j;
    for (i = 0; i < blocks; i++) {
        h1_render(out, 256);
        for (j = 0; j < 512; j += 2) {
            int32_t s = out[j], a = s < 0 ? -s : s;
            energy += (double)s * (double)s;
            if (a > peak) peak = a;
        }
        if (i % 3 == 0)
            h1_frame(ms += 17);
    }
}
static void tap(int b) { h1_button(b, 1); go(40); h1_button(b, 0); go(40); }
static void to(int inst, int algo) { h1_control_set(H1_ENC_PRESET, inst); h1_control_set(H1_ENC_ALGO, algo); }

static double phrase(int algo)                   /* the same notes on one of LAVA's algorithms: how loud */
{
    int k;
    h1_init(44100);
    to(LAVA, algo);
    go(200);
    energy = 0.0;
    peak = 0;
    for (k = 0; k < 4; k++) {
        h1_key(5 + k * 3, 1);
        go(350);
        h1_key(5 + k * 3, 0);
        go(80);
    }
    return energy;
}

static uint8_t blob[4096], lblob[H1_LOOP_MAX];

int main(void)
{
    const uint8_t *p;
    uint32_t size, lsize;
    int i, a;
    double e[NALGO], lo = 1e300, hi = 0.0;

    /* ---- ALGORITHM ---- */
    h1_init(44100);
    CHECK(NALGOS(LAVA) == 9 && !strcmp(insts[LAVA]->algo[8], "HOOVER"), "LAVA has nine algorithms, the ninth HOOVER");
    for (i = 0; i < NINST; i++)
        if (i != LAVA)
            CHECK(NALGOS(i) == 8, "every other instrument has eight");
    to(LAVA, 7);
    h1_encoder(H1_ENC_ALGO, 1);
    CHECK(h1_algorithm() == 8, "on LAVA ALGORITHM turns on from QUAKE to HOOVER");
    h1_encoder(H1_ENC_ALGO, 1);
    CHECK(h1_algorithm() == 0, "and from HOOVER round to MOLTEN");
    to(2, 7);
    h1_encoder(H1_ENC_ALGO, 1);
    CHECK(h1_algorithm() == 0, "on another instrument it goes round after the eighth");
    h1_control_set(H1_ENC_ALGO, 8);
    CHECK(h1_algorithm() == 7, "and cannot be set to a ninth");
    CHECK(1u + 15u * NALGO + 8u <= 255u && PART_INST(PART(13, 8)) == 13 && PART_ALGO(PART(13, 8)) == 8 && !PART_SOME(0),
          "a part fits sixteen instruments of nine in a byte, and 0 is none");
    CHECK(!part_ok(PART(2, 8)) && part_ok(PART(LAVA, 8)), "a ninth algorithm is a part only where there is one");

    /* ---- HOOVER sounds, as loud as the rest ---- */
    for (a = 0; a < NALGO; a++) {
        e[a] = phrase(a);
        if (a == 8)
            continue;
        if (e[a] < lo) lo = e[a];
        if (e[a] > hi) hi = e[a];
    }
    CHECK(peak > 400000, "HOOVER is heard");
    CHECK(e[8] > lo * 0.5 && e[8] < hi * 2.0, "and sits among LAVA's others, within 3 dB of their range");

    /* ---- a version 8 state (0.2.2): fourteen instruments of eight algorithms, its parts in bits ---- */
    h1_init(44100);
    to(LAVA, 3);
    h1_midi(0xB0, 20, 127);                      /* GROWL's first knob, as saved */
    p = (const uint8_t *)h1_state(&size);
    memset(blob, 0xAA, sizeof blob);
    {
        const H1State *c = (const H1State *)(const void *)p;
        H1Old *o = (H1Old *)(void *)blob;        /* (the host does not mind: the device never does this) */
        memcpy(blob, p, offsetof(H1Old, ip));
        memcpy(o->ip, c->ip, sizeof o->ip);
        memcpy(o->ink, c->ink, sizeof o->ink);
        memcpy(o->steps, c->steps, sizeof o->steps);
        memset(o->step_part, 0, sizeof o->step_part);
        for (i = 0; i < (int)H1_OLD_NINST; i++)
            memcpy(o->knob[i], c->knob[i], sizeof o->knob[i]);
        memcpy(o->step_vel, c->step_vel, sizeof o->step_vel);
        memcpy(o->step_lay, c->step_lay, sizeof o->step_lay);
        memcpy(o->layer, c->layer, sizeof o->layer);
        o->version = 8;
        o->step_part[5][0] = OLD_PART(4, 6);
        o->steps[5][0] = 61;
        o->layer[2].part = OLD_PART(LAVA, 3);
        o->knob[13][7][2] = 42;                  /* the last of the fourteen's last knob table entry */
    }
    h1_init(44100);
    st.knob[14][1][0] = 7;                       /* (a slot past the fourteen: what is there now must not show through) */
    CHECK(h1_state_load(blob, (uint32_t)H1_VERSION_8_SIZE) == 0, "a version 8 state loads");
    CHECK(h1_instrument() == LAVA && h1_algorithm() == 3, "with what it held");
    CHECK(KNOBS(LAVA, 3)[0] == 100, "its knobs kept");
    CHECK(st.step_part[5][0] == PART(4, 6), "a step's part read the old way");
    CHECK(st.layer[2].part == PART(LAVA, 3), "a layer's part read the old way");
    CHECK(!memcmp(st.knob[LAVA][8], insts[LAVA]->def.knob0, 4), "HOOVER's knobs as new");
    CHECK(st.knob[13][7][2] == 42, "the knob table read across, instrument by instrument");
    CHECK(st.knob[14][1][0] == (NINST > 14 ? insts[NINST > 14 ? 14 : 0]->def.knob0[0] : 0) && st.ninst == NINST,
          "the slot past the fourteen as new (or empty), and the count saved");
    for (i = 0; i < NLAYER; i++)
        if (i != 2)
            CHECK(st.layer[i].part == 0 || part_ok(st.layer[i].part), "every other layer empty or a real part");

    /* ---- a loop saved by 0.2.2 (version 1): its parts read the old way ---- */
    h1_init(44100);
    to(LAVA, 3);
    tap(H1_BTN_REC);
    h1_key(7, 1); go(150); h1_key(7, 0); go(150);
    tap(H1_BTN_REC);
    go(300);
    tap(H1_BTN_PLAY);
    go(100);
    p = (const uint8_t *)h1_loop(&lsize);
    CHECK(lsize > 12u && p[4] == 2, "a loop is saved as version 2");
    memcpy(lblob, p, lsize);
    for (i = 12; i + 8 <= (int)lsize; i += 8)
        if (lblob[i + 4] == PART(LAVA, 3))
            lblob[i + 4] = OLD_PART(LAVA, 3);
    lblob[4] = 1;
    h1_init(44100);
    CHECK(h1_loop_load(lblob, lsize) == 0, "a version 1 loop loads");
    p = (const uint8_t *)h1_loop(&lsize);
    {
        int n = 0, ok = 1;
        for (i = 12; i + 8 <= (int)lsize; i += 8, n++)
            ok &= p[i + 4] == PART(LAVA, 3) || p[i + 4] == 0;
        CHECK(n > 0 && ok, "its notes' parts read the old way");
    }

    /* ---- HOOVER saved and back ---- */
    h1_init(44100);
    to(LAVA, 8);
    h1_midi(0xB0, 21, 127);                      /* HEAT */
    tap(H1_BTN_REC);
    h1_key(9, 1); go(150); h1_key(9, 0); go(150);
    tap(H1_BTN_REC);
    go(300);
    tap(H1_BTN_PLAY);
    go(100);
    p = (const uint8_t *)h1_state(&size);
    CHECK(size == sizeof(H1State), "the state is one H1State");
    memcpy(blob, p, size);
    p = (const uint8_t *)h1_loop(&lsize);
    memcpy(lblob, p, lsize);
    h1_init(44100);
    CHECK(h1_state_load(blob + 0, size) == 0 && h1_loop_load(lblob, lsize) == 0, "both load again");
    CHECK(h1_instrument() == LAVA && h1_algorithm() == 8 && st.knob[LAVA][8][1] == 100, "on HOOVER, its knobs where they were");
    p = (const uint8_t *)h1_loop(&lsize);
    {
        int n = 0, ok = 1;
        for (i = 12; i + 8 <= (int)lsize; i += 8, n++)
            ok &= p[i + 4] == PART(LAVA, 8) || p[i + 4] == 0;
        CHECK(n > 0 && ok, "and the loop's notes still on HOOVER");
    }
    energy = 0.0;
    tap(H1_BTN_PLAY);
    go(1500);
    CHECK(energy > 0.0, "and it plays");

    /* ---- a state saved with fewer instruments than there are now: the ones after come up as new, the rest as
     * saved ---- */
    h1_init(44100);
    to(12, 2);
    h1_midi(0xB0, 21, 0);
    to(11, 4);
    h1_midi(0xB0, 22, 0);
    memcpy(blob, h1_state(&size), size);
    CHECK(size == 3596u, "a version 10 state is 3596 bytes");
    ((H1State *)(void *)blob)->ninst = 12;
    h1_init(44100);
    CHECK(h1_state_load(blob, size) == 0, "it loads");
    CHECK(st.knob[11][4][2] == 0 && h1_instrument() == 11 && h1_algorithm() == 4, "the twelve as saved");
    CHECK(st.ip[12].algo == 0 && !memcmp(st.knob[12][2], insts[12]->def.knob0, 4) && st.ink[12][0] == insts[12]->ink[0],
          "the thirteenth as new");
    CHECK(st.ninst == NINST, "and the count is now's");

    /* ---- 0.3.0's state, version 9: room for sixteen instruments. Everything comes back, BLAB as new ---- */
    {
        static H1V9 v9;
        const H1State *s10;
        int a;
        h1_init(44100);
        to(15, 3);
        h1_midi(0xB0, 23, 0);
        go(100);
        to(2, 5);
        h1_midi(0xB0, 21, 127);
        go(100);
        s10 = (const H1State *)h1_state(&size);
        memset(&v9, 0, sizeof v9);
        memcpy(&v9, s10, offsetof(H1V9, ip));
        memcpy(v9.ip, s10->ip, sizeof v9.ip);
        memcpy(v9.ink, s10->ink, sizeof v9.ink);
        v9.ninst = 16;
        memcpy(v9.steps, s10->steps, sizeof v9.steps);
        memcpy(v9.step_part, s10->step_part, sizeof v9.step_part);
        memcpy(v9.knob, s10->knob, sizeof v9.knob);
        memcpy(v9.step_vel, s10->step_vel, sizeof v9.step_vel);
        memcpy(v9.step_lay, s10->step_lay, sizeof v9.step_lay);
        memcpy(v9.layer, s10->layer, sizeof v9.layer);
        v9.version = 9;
        st.knob[16][0][0] = 99;                  /* (BLAB's, as a 0.3.0 unit never had it: it must come up as new) */
        h1_init(44100);
        CHECK(sizeof v9 == 3344u && h1_state_load(&v9, sizeof v9) == 0, "a version 9 state loads");
        CHECK(h1_instrument() == 2 && h1_algorithm() == 5 && st.knob[2][5][1] == 100 && st.knob[15][3][3] == 0,
              "its instruments as saved");
        for (a = 0; a < NALGO; a++)
            if (memcmp(st.knob[16][a], insts[16]->def.knob0, 4))
                break;
        CHECK(a == NALGO && st.ip[16].algo == 0 && st.ink[16][1] == insts[16]->ink[1], "BLAB as new");
        CHECK(st.version == H1_VERSION && st.ninst == NINST, "and it is version 10 now");
    }

    printf("ninth: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
