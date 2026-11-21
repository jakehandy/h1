/* The four home knobs belong to the algorithm: every algorithm of every instrument has its own, and
 * hold SAVE puts them back, a step further the longer it is held (the algorithm, the instrument,
 * everything). Run by tests/run_tests.sh: the core alone, its panel worked as a hand would.
 * Also here, since it came with the same saved state: a step holds eight notes.
 *
 * The second half listens: with LAYERS on, a loop recorded on one algorithm goes on sounding with
 * that algorithm's knobs while another algorithm of the same instrument is in hand. It plays the
 * same thing with different knobs on each side and compares what comes out. */
#define _POSIX_C_SOURCE 200809L
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include "../core/internal.h"
/* a part as states before version 9 kept it (internal.h, PART_V8) */
#define OLD_PART(inst, algo) ((uint8_t)(0x80u | ((uint32_t)(inst) << 3) | ((uint32_t)(algo) & 7u)))
#include "../core/h1.h"

static uint32_t ms;
static int fails, checks;
#define CHECK(c, what) do { checks++; if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, what); } } while (0)

static uint32_t heard;                           /* a hash of every sample since it was last cleared */
static void go(int msec)                         /* the sound and the frames, as the device runs them */
{
    static int32_t out[512];
    int blocks = msec * 441 / 2560 + 1, i, j;
    for (i = 0; i < blocks; i++) {
        h1_render(out, 256);
        for (j = 0; j < 512; j++)
            heard = (heard ^ (uint32_t)out[j]) * 16777619u;
        if (i % 3 == 0)
            h1_frame(ms += 17);
    }
}
static void tap(int b) { h1_button(b, 1); go(40); h1_button(b, 0); go(40); }
static void hold(int b, int msec) { h1_button(b, 1); go(msec); h1_button(b, 0); go(40); }
static void pair(void)                           /* SEQ and REC together: LAYERS */
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

static int knob(int k) { return h1_control(H1_ENC_K1 + k, 0, 0, 0, 0); }
static void set4(int a, int b, int c, int d)
{
    h1_control_set(H1_ENC_K1, a);
    h1_control_set(H1_ENC_K2, b);
    h1_control_set(H1_ENC_K3, c);
    h1_control_set(H1_ENC_K4, d);
}
static int are(int a, int b, int c, int d) { return knob(0) == a && knob(1) == b && knob(2) == c && knob(3) == d; }
static int as_new(void)                          /* the algorithm in hand has the knobs its instrument starts with */
{
    const uint8_t *d = insts[h1_instrument()]->def.knob0;
    return are(d[0], d[1], d[2], d[3]);
}
static void to(int inst, int algo)
{
    h1_control_set(H1_ENC_PRESET, inst);
    h1_control_set(H1_ENC_ALGO, algo);
}
static int all_as_new(int inst)                  /* every algorithm of the instrument */
{
    int a, ok = 1;
    for (a = 0; a < NALGOS(inst); a++) {
        to(inst, a);
        ok &= as_new();
    }
    return ok;
}

/* An instrument with LAYERS on: a loop recorded on algorithm 1 with the knobs at ka, then algorithm 5 in
 * hand with the knobs at kb, and nothing played on it. What the loop sounds like over four seconds.
 * (In a process of its own: h1_init does not wind the engine's clocks and random numbers back, so two
 * takes in one process would not sound the same whatever the knobs.) */
static uint32_t loop_take(int inst, const int *ka, const int *kb)
{
    h1_init(44100);
    ms = 0;
    go(100);
    to(inst, 1);
    set4(ka[0], ka[1], ka[2], ka[3]);
    if (!h1_layers())                        /* (on from the start since 0.2.2) */
        pair();
    tap(H1_BTN_REC);
    note(5, 200);
    go(300);
    note(12, 200);
    go(5000);                                    /* the take runs out its two bars and plays */
    h1_control_set(H1_ENC_ALGO, 5);
    set4(kb[0], kb[1], kb[2], kb[3]);
    go(700);                                     /* (what rang on from before the change is gone) */
    heard = 0;
    go(4000);
    return heard;
}
static uint32_t loop_heard(int inst, const int *ka, const int *kb)
{
    int fd[2];
    uint32_t h = 0;
    pid_t pid;
    fflush(stdout);
    if (pipe(fd) != 0 || (pid = fork()) < 0) {
        printf("FAIL: no process for a take\n");
        exit(1);
    }
    if (pid == 0) {
        h = loop_take(inst, ka, kb);
        _exit(write(fd[1], &h, sizeof h) == (ssize_t)sizeof h ? 0 : 1);
    }
    close(fd[1]);
    if (read(fd[0], &h, sizeof h) != (ssize_t)sizeof h) {
        printf("FAIL: a take did not come back\n");
        exit(1);
    }
    close(fd[0]);
    waitpid(pid, 0, 0);
    return h;
}

int main(void)
{
    static uint8_t blob[sizeof(H1State)];
    static const int K1[4] = {20, 70, 30, 60}, K2[4] = {90, 10, 80, 5}, K3[4] = {55, 45, 65, 35};
    uint32_t size = 0, h1, h2, h3;
    H1State *s = (H1State *)(void *)blob;
    int i, a, longest = 0;

    h1_init(44100);
    go(100);

    /* ---- every algorithm has its own ---- */
    for (i = 0; i < NINST; i++)
        CHECK(all_as_new(i), "a new h1: every algorithm starts on its instrument's knobs");
    to(2, 0);
    set4(11, 22, 33, 44);
    h1_encoder(H1_ENC_ALGO, 1);                  /* as a hand turns it */
    go(60);
    CHECK(as_new(), "the next algorithm does not take the last one's knobs");
    set4(91, 82, 73, 64);
    h1_encoder(H1_ENC_ALGO, -1);
    go(60);
    CHECK(are(11, 22, 33, 44), "back on the first algorithm its knobs are where they were left");
    h1_encoder(H1_ENC_ALGO, 1);
    CHECK(are(91, 82, 73, 64), "and the second's where they were left");
    h1_midi(0xB0, 21, 127);                      /* CC 21: the second knob, of the algorithm in hand */
    CHECK(are(91, 100, 73, 64), "a MIDI knob turns the algorithm in hand's");
    to(2, 0);
    CHECK(are(11, 22, 33, 44), "and no other's");
    for (a = 2; a < NALGOS(2); a++) {
        to(2, a);
        CHECK(as_new(), "an algorithm nobody touched is as new");
    }
    CHECK(all_as_new(3), "another instrument is as new");

    /* ---- saved and loaded ---- */
    to(2, 1);
    memcpy(blob, h1_state(&size), sizeof blob);
    CHECK(size == sizeof(H1State), "the state is one H1State");
    h1_init(44100);
    CHECK(h1_state_load(blob, size) == 0, "the saved state loads");
    CHECK(h1_instrument() == 2 && h1_algorithm() == 1 && are(91, 100, 73, 64), "an algorithm's knobs are saved");
    to(2, 0);
    CHECK(are(11, 22, 33, 44), "each algorithm's");

    /* the state before this (version 5: one set of knobs to an instrument, four notes to a step, 920 bytes):
     * it loads, every algorithm starts from where the instrument's knobs stood, and the steps keep their
     * notes and the part of each */
    {
        static uint8_t blob5[sizeof(H1State)];
        const size_t body = offsetof(H1Old, steps);
        int j, ok = 1;
        s->version = 5;
        for (i = 0; i < NINST; i++)
            for (a = 0; a < 4; a++)
                s->ip[i].knob0[a] = (uint8_t)(10 + i * 5 + a);
        memset(blob5, 0xFF, sizeof blob5);       /* (its padding, and what is past its end, may hold anything) */
        memcpy(blob5, blob, offsetof(H1Old, ip) + H1_OLD_NINST * sizeof(InstParams));   /* (fourteen instruments then) */
        memcpy(blob5 + offsetof(H1Old, ink), s->ink, H1_OLD_NINST * 3u);
        memset(blob5 + body, 0, NSTEP * 8u);
        for (j = 0; j < 4; j++) {                /* step 3: two notes; step 63: four */
            blob5[body + 63u * 4u + (size_t)j] = (uint8_t)(50 + j);
            blob5[body + NSTEP * 4u + 63u * 4u + (size_t)j] = OLD_PART(j, j + 1);
        }
        blob5[body + 3u * 4u] = 61;
        blob5[body + 3u * 4u + 1u] = 65;
        blob5[body + NSTEP * 4u + 3u * 4u] = OLD_PART(2, 1);
        blob5[body + NSTEP * 4u + 3u * 4u + 1u] = OLD_PART(4, 6);
        h1_init(44100);
        CHECK(H1_VERSION_5_SIZE == 920u, "a version 5 state is 920 bytes");
        CHECK(h1_state_load(blob5, (uint32_t)H1_VERSION_5_SIZE) == 0, "a version 5 state loads");
        for (i = 0; i < (int)H1_OLD_NINST; i += 5)
            for (a = 0; a < (int)H1_OLD_NALGO; a++) {
                to(i, a);
                CHECK(are(10 + i * 5, 11 + i * 5, 12 + i * 5, 13 + i * 5), "every algorithm starts from the instrument's old knobs");
            }
        CHECK(st.steps[3][0] == 61 && st.steps[3][1] == 65 && st.step_part[3][0] == PART(2, 1) && st.step_part[3][1] == PART(4, 6),
              "a step keeps its notes and their parts");
        for (j = 0; j < NSNOTE; j++) {
            ok &= st.steps[63][j] == (j < 4 ? 50 + j : 0) && st.step_part[63][j] == (j < 4 ? PART(j, j + 1) : 0);
            ok &= j < 2 || (st.steps[3][j] == 0 && st.step_part[3][j] == 0);
        }
        for (i = 0; i < NSTEP; i++)
            for (j = 0; j < NSNOTE; j++)
                ok &= i == 3 || i == 63 || (st.steps[i][j] == 0 && st.step_part[i][j] == 0);
        CHECK(ok, "and holds nothing it did not hold before");
        CHECK(h1_state_load(blob5, (uint32_t)H1_VERSION_5_SIZE - 1u) != 0, "a state of the wrong size is refused");
        CHECK(h1_state_load(blob5, (uint32_t)sizeof blob5) != 0, "a version 5 state of this version's size is refused");
        blob5[4] = 6;
        CHECK(h1_state_load(blob5, (uint32_t)H1_VERSION_5_SIZE) != 0, "and this version's of a version 5 state's size");
    }

    /* the state of fourteen instruments (version 6, 1880 bytes): everything in it comes back, and the two
     * instruments that came after it are as new */
    {
        static uint8_t blob6[sizeof(H1Old) + 8];
        int ok = 1, k;
        s->version = 6;
        for (i = 0; i < (int)H1_OLD_NINST; i++)
            for (a = 0; a < (int)H1_OLD_NALGO; a++)
                for (k = 0; k < 4; k++)
                    s->knob[i][a][k] = (uint8_t)((i * 7 + a * 3 + k) % 101);
        s->ip[13].algo = 5;
        s->ink[13][1] = 77;
        s->steps[9][7] = 64;
        s->step_part[9][7] = OLD_PART(13, 5);
        memset(blob6, 0xFF, sizeof blob6);
        memcpy(blob6 + 1, blob, offsetof(H1Old, ip) + H1_OLD_NINST * sizeof(InstParams));
        memcpy(blob6 + 1 + offsetof(H1Old, ink), s->ink, H1_OLD_NINST * 3u);
        memcpy(blob6 + 1 + offsetof(H1Old, rsv), s->rsv, sizeof s->rsv);
        memcpy(blob6 + 1 + offsetof(H1Old, steps), s->steps, sizeof s->steps);
        memcpy(blob6 + 1 + offsetof(H1Old, step_part), s->step_part, sizeof s->step_part);
        for (i = 0; i < (int)H1_OLD_NINST; i++)               /* (eight algorithms to an instrument then) */
            memcpy(blob6 + 1 + offsetof(H1Old, knob) + (size_t)i * H1_OLD_NALGO * 4u, s->knob[i], H1_OLD_NALGO * 4u);
        h1_init(44100);
        st.knob[14][2][1] = 3;                   /* (what is there now must not show through) */
        st.ip[15].algo = 6;
        CHECK(H1_VERSION_6_SIZE == 1880u, "a version 6 state is 1880 bytes");
        CHECK(h1_state_load(blob6 + 1, 1880u) == 0, "a version 6 state loads, from an odd address");
        for (i = 0; i < (int)H1_OLD_NINST; i++)
            for (a = 0; a < (int)H1_OLD_NALGO; a++)
                for (k = 0; k < 4; k++)
                    ok &= st.knob[i][a][k] == (uint8_t)((i * 7 + a * 3 + k) % 101);
        CHECK(ok, "every algorithm of the fourteen has its knobs");
        CHECK(st.ip[13].algo == 5 && st.ink[13][1] == 77 && st.steps[9][7] == 64 && st.step_part[9][7] == PART(13, 5),
              "the last of the fourteen, its ink and a step's eighth note are where they were");
        ok = st.version == H1_VERSION;
        for (i = (int)H1_OLD_NINST; i < NINST; i++) {
            ok &= st.ip[i].algo == 0 && st.ink[i][0] == insts[i]->ink[0];
            for (a = 0; a < NALGO; a++)
                for (k = 0; k < 4; k++)
                    ok &= st.knob[i][a][k] == insts[i]->def.knob0[k];
        }
        CHECK(ok, "TILT and PLUNK come up as new");
        CHECK(h1_state_load(blob6 + 1, 1879u) != 0, "a version 6 state of the wrong size is refused");

        /* and version 8, the same fourteen with each step note's velocity and layer and the layers after */
        blob6[1 + offsetof(H1Old, version)] = 8;
        memset(blob6 + 1 + offsetof(H1Old, step_vel), 0, sizeof(H1Old) - offsetof(H1Old, step_vel));
        blob6[1 + offsetof(H1Old, step_vel) + 9 * NSNOTE + 7] = 40;
        blob6[1 + offsetof(H1Old, step_lay) + 9 * NSNOTE + 7] = 3;
        blob6[1 + offsetof(H1Old, layer) + 2 * sizeof(Layer)] = OLD_PART(13, 5);
        blob6[1 + offsetof(H1Old, layer) + 2 * sizeof(Layer) + 2] = 61;
        h1_init(44100);
        CHECK(H1_VERSION_8_SIZE == 3160u, "a version 8 state is 3160 bytes");
        CHECK(h1_state_load(blob6 + 1, 3160u) == 0, "a version 8 state loads");
        CHECK(st.step_vel[9][7] == 40 && st.step_lay[9][7] == 3 && st.layer[2].part == PART(13, 5) && st.layer[2].knob[0] == 61,
              "a step note's velocity and layer and a layer's settings are where they were");
        CHECK(st.knob[13][7][3] == (uint8_t)((13 * 7 + 7 * 3 + 3) % 101) && st.ip[13].algo == 5 && st.steps[9][7] == 64,
              "with the fourteen's knobs and steps");
        ok = st.version == H1_VERSION;
        for (i = (int)H1_OLD_NINST; i < NINST; i++)
            ok &= st.ip[i].algo == 0 && st.knob[i][3][2] == insts[i]->def.knob0[2];
        CHECK(ok, "and TILT and PLUNK as new");
    }

    /* ---- hold SAVE: the algorithm, then the instrument, then everything ---- */
    h1_init(44100);
    go(100);
    to(4, 2);
    set4(1, 2, 3, 4);
    to(4, 6);
    set4(5, 6, 7, 8);
    to(7, 3);
    set4(9, 9, 9, 9);
    to(4, 6);
    tap(H1_BTN_FX);
    h1_control_set(H1_ENC_K1, 77);               /* ECHO, a page of the instrument */
    tap(H1_BTN_HOME);
    tap(H1_BTN_SAVE);
    CHECK(are(5, 6, 7, 8), "a tap of SAVE resets nothing");
    hold(H1_BTN_SAVE, 600);
    CHECK(are(5, 6, 7, 8), "nor does a short hold");
    hold(H1_BTN_SAVE, 1300);
    CHECK(as_new() && h1_algorithm() == 6, "hold SAVE: the algorithm in hand's knobs are as new");
    to(4, 2);
    CHECK(are(1, 2, 3, 4), "and the instrument's other algorithms are left alone");
    tap(H1_BTN_FX);
    CHECK(knob(0) == 77, "and its pages");
    tap(H1_BTN_HOME);
    to(4, 6);
    set4(5, 6, 7, 8);
    hold(H1_BTN_SAVE, 2800);
    CHECK(h1_instrument() == 4 && all_as_new(4), "held on: every algorithm of the instrument is as new");
    tap(H1_BTN_FX);
    CHECK(knob(0) == insts[4]->def.fx[0], "and its pages");
    tap(H1_BTN_HOME);
    to(7, 3);
    CHECK(are(9, 9, 9, 9), "and another instrument is left alone");
    to(4, 6);
    set4(5, 6, 7, 8);
    hold(H1_BTN_SAVE, 4600);
    for (i = 0; i < NINST; i++)
        CHECK(all_as_new(i), "held four seconds: every instrument is as new");

    /* (a reset says the algorithm's or the instrument's name and " RESET": it must fit the toast) */
    for (i = 0; i < NINST; i++) {
        if ((int)strlen(insts[i]->name) > longest) longest = (int)strlen(insts[i]->name);
        for (a = 0; a < NALGOS(i); a++)
            if ((int)strlen(insts[i]->algo[a]) > longest) longest = (int)strlen(insts[i]->algo[a]);
    }
    CHECK(longest + 6 < 24, "the longest name and RESET fit the toast");

    /* ---- a step holds eight notes ---- */
    {
        int j, n = 0, most = 0;
        h1_init(44100);
        go(100);
        h1_control_set(H1_ENC_PRESET, 8);        /* TWANG: plucked, every note its own voice */
        hold(H1_BTN_SEQ, 700);                   /* the sequencer to STEP */
        tap(H1_BTN_REC);                         /* step-write */
        for (j = 0; j < 9; j++) {                /* nine keys down together on the first step */
            h1_key(j * 2, 1);
            go(20);
        }
        for (j = 0; j < 9; j++)
            h1_key(j * 2, 0);
        go(100);
        for (j = 0; j < NSNOTE; j++)
            n += st.steps[0][j] != 0;
        CHECK(NSNOTE == 8 && n == 8, "nine keys on a step: it holds eight");
        for (i = 0; i < NSNOTE; i++)
            for (j = i + 1; j < NSNOTE; j++)
                CHECK(st.steps[0][i] != st.steps[0][j], "eight different notes");
        tap(H1_BTN_REC);
        tap(H1_BTN_PLAY);
        for (i = 0; i < 400; i++) {              /* the pattern comes round: all eight sound at once */
            int v, on = 0;
            go(10);
            for (v = 0; v < NVOICE; v++)
                on += voices[v].stage && voices[v].gate;
            if (on > most) most = on;
        }
        CHECK(most == 8, "the step plays all eight");
        memcpy(blob, h1_state(&size), sizeof blob);
        h1_init(44100);
        n = 0;
        if (h1_state_load(blob, size) == 0)
            for (j = 0; j < NSNOTE; j++)
                n += st.steps[0][j] != 0;
        CHECK(n == 8, "and they are saved");
    }

    /* ---- a recorded part keeps its own algorithm's knobs ---- */
    /* What the instrument keeps for all its voices goes with the algorithm in hand, as its effects do:
     * the post stage of COZY, KOI, PIP, BARD and BLAB (its one mouth), and ORBIT's spin. There a loop hears both. */
    for (i = 0; i < NINST; i++) {
        const char *nm = insts[i]->name;
        int shared = !strcmp(nm, "COZY") || !strcmp(nm, "KOI") || !strcmp(nm, "PIP") || !strcmp(nm, "BARD") || !strcmp(nm, "ORBIT") ||
                     !strcmp(nm, "BLAB");
        h1 = loop_heard(i, K1, K2);
        h2 = loop_heard(i, K1, K3);
        h3 = loop_heard(i, K3, K2);
        if ((h1 != h2 && !shared) || h1 == h3)
            printf("  %s: %08x %08x %08x\n", nm, (unsigned)h1, (unsigned)h2, (unsigned)h3);
        CHECK(h1 == h2 || shared, "a loop on one algorithm does not hear the knobs of the algorithm in hand");
        CHECK(h1 != h3, "it hears its own");
    }

    printf("knobs: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
