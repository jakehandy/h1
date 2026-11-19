/* What 0.2.2 adds, on the core alone, its panel worked as a hand would. Run by tests/run_tests.sh.
 *
 *   the limiter     a chord on every algorithm of every instrument never goes past the limiter's ceiling,
 *                   and a note at an ordinary level goes through untouched
 *   velocity        a soft note over MIDI is quieter and darker than a hard one; 127 over MIDI is the
 *                   panel's key to the bit
 *   layers          each take is a layer with its own knobs, ENV and LFO: changed after the take, the
 *                   instrument sounds different and the layer does not
 *   the loop saved  h1_loop / h1_loop_load bring a loop back, layers and velocities with it, and the loop
 *                   asks to be saved only when it is stopped
 *   MIDI out        keys, the arpeggiator and the loop go out, CC 20-23 with the knobs, nothing that came in
 *   version 6       a 0.2.1 state loads, with LAYERS on
 *
 * Each sound to compare is made in a process of its own: h1_init does not wind the engine's clocks and
 * random numbers back, so two takes in one process never sound the same. */
#define _POSIX_C_SOURCE 200809L
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include "../core/internal.h"
#include "../core/h1.h"

static uint32_t ms;
static int fails, checks;
#define CHECK(c, what) do { checks++; if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, what); } } while (0)

typedef struct Heard { double energy, slope; int32_t peak; uint32_t hash, n; } Heard;
static Heard heard;
static int32_t last_l;
static void go(int msec)
{
    static int32_t out[512];
    int blocks = msec * 441 / 2560 + 1, i, j;
    for (i = 0; i < blocks; i++) {
        h1_render(out, 256);
        for (j = 0; j < 512; j += 2) {
            int32_t s = out[j], a = s < 0 ? -s : s;
            double d = (double)s - (double)last_l;
            heard.energy += (double)s * (double)s;
            heard.slope += d * d;                /* the energy of the first difference: how bright */
            if (a > heard.peak) heard.peak = a;
            heard.hash = heard.hash * 31u + (uint32_t)s;
            last_l = s;
            heard.n++;
        }
        if (i % 3 == 0)
            h1_frame(ms += 17);
    }
}
static void listen(void) { memset(&heard, 0, sizeof heard); }
static void tap(int b) { h1_button(b, 1); go(40); h1_button(b, 0); go(40); }
static void pair(void)
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

/* a take in a process of its own */
static Heard taken(Heard (*fn)(int, int), int a, int b)
{
    int fd[2];
    Heard h;
    pid_t pid;
    fflush(stdout);
    if (pipe(fd) != 0 || (pid = fork()) < 0) {
        printf("FAIL: no process for a take\n");
        exit(1);
    }
    if (pid == 0) {
        h = fn(a, b);
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

/* ---- the limiter: six keys at once, on instrument i, algorithm a ---- */
static Heard chord(int i, int a)
{
    static const int K[6] = {0, 7, 12, 16, 19, 24};
    int k;
    h1_init(44100);
    h1_control_set(H1_ENC_PRESET, i);
    h1_control_set(H1_ENC_ALGO, a);
    go(300);
    listen();
    for (k = 0; k < 6; k++)
        h1_key(K[k], 1);
    go(1500);
    for (k = 0; k < 6; k++)
        h1_key(K[k], 0);
    go(1000);
    return heard;
}

/* ---- velocity: one note on COZY TINE, from the panel (vel 0) or over MIDI at vel ---- */
static Heard one_note(int vel, int unused)
{
    (void)unused;
    h1_init(44100);
    go(300);
    listen();
    if (vel)
        h1_midi(0x90, 60, (uint8_t)vel);
    else
        h1_key(7, 1);                            /* (key 7 is note 60) */
    go(700);
    if (vel)
        h1_midi(0x80, 60, 0);
    else
        h1_key(7, 0);
    go(800);
    return heard;
}

/* ---- MIDI out ---- */
static uint8_t mo[512][3];
static int nmo;
static void mo_fn(uint8_t s, uint8_t a, uint8_t b)
{
    if (nmo < 512) {
        mo[nmo][0] = s;
        mo[nmo][1] = a;
        mo[nmo][2] = b;
        nmo++;
    }
}
static int mo_count(uint8_t s, int note)          /* (note < 0: any) */
{
    int i, n = 0;
    for (i = 0; i < nmo; i++)
        if (mo[i][0] == s && (note < 0 || mo[i][1] == note))
            n++;
    return n;
}

static int seq_voices(void)
{
    int v, n = 0;
    for (v = 0; v < NVOICE; v++)
        n += voices[v].stage && voices[v].gate && voices[v].seq;
    return n;
}

int main(void)
{
    static uint8_t blob[sizeof(H1State)], lblob[H1_LOOP_MAX], v6[sizeof(H1State)];
    const uint8_t *p;
    uint32_t size, lsize;
    int i, a, la, lb = 0, n, most;

    /* ---- the limiter ---- */
    {
        int32_t ceiling = (int32_t)(0.7f * 7549747.0f) + 2, worst = 0;
        for (i = 0; i < NINST; i++)
            for (a = 0; a < NALGO; a++) {
                Heard h = taken(chord, i, a);
                if (h.peak > worst) worst = h.peak;
                if (h.peak > ceiling)
                    printf("  %s %s: peak %d over %d\n", insts[i]->name, insts[i]->algo[a], (int)h.peak, (int)ceiling);
                CHECK(h.peak <= ceiling, "six keys never pass the limiter's ceiling");
                CHECK(h.peak > 400000, "and they are heard");
            }
        printf("  limiter: the loudest chord peaks at %.3f of full scale\n", (double)worst / 8388607.0);
    }

    /* ---- velocity ---- */
    {
        Heard key = taken(one_note, 0, 0), hard = taken(one_note, 127, 0), soft = taken(one_note, 40, 0);
        double bright_hard = hard.slope / hard.energy, bright_soft = soft.slope / soft.energy;
        CHECK(key.hash == hard.hash && key.energy == hard.energy, "127 over MIDI is the panel's key, to the bit");
        CHECK(soft.energy < hard.energy * 0.25, "velocity 40 is much quieter");
        CHECK(soft.energy > 0.0, "and still heard");
        CHECK(bright_soft < bright_hard * 0.9, "and darker");
        printf("  velocity 40: %.1f dB, brightness %.2f of 127's\n", 10.0 * __builtin_log10(soft.energy / hard.energy),
               bright_soft / bright_hard);
    }

    /* ---- layers: their own settings ---- */
    h1_init(44100);
    nmo = 0;
    h1_midi_out(mo_fn);
    go(100);
    CHECK(h1_layers(), "LAYERS is on on a new h1");
    h1_control_set(H1_ENC_PRESET, 0);            /* COZY */
    h1_control_set(H1_ENC_K1, 10);
    tap(H1_BTN_ENV);
    h1_control_set(H1_ENC_K4, 5);                /* a short release: the bass */
    tap(H1_BTN_HOME);
    tap(H1_BTN_REC);
    note(0, 200);
    go(300);
    note(5, 200);
    go(5000);                                    /* two bars: the take ends and it plays */
    {
        SeqInfo si;
        eng_seq_info(&si);
        CHECK(si.loop_state == LOOP_PLAY, "the first take plays");
    }
    la = 0;
    for (i = 0; i < NLAYER; i++)
        if (st.layer[i].part) {
            la++;
            lb = i;
        }
    CHECK(la == 1 && !st.layer[lb].open, "one layer, closed when the take ended");
    CHECK(st.layer[lb].env[3] == 5 && st.layer[lb].knob[0] == 10, "it kept the release and the knob it was played with");
    /* the same instrument and algorithm, set up as a pad, and a second take over the first */
    tap(H1_BTN_ENV);
    h1_control_set(H1_ENC_K1, 90);
    h1_control_set(H1_ENC_K4, 95);
    tap(H1_BTN_HOME);
    h1_control_set(H1_ENC_K1, 80);
    CHECK(st.layer[lb].env[3] == 5 && st.layer[lb].knob[0] == 10, "turning the instrument does not turn the layer");
    tap(H1_BTN_REC);                             /* over the loop as it plays */
    note(12, 300);
    go(200);
    CHECK(eng_layer_rec() != 0 && eng_layer_rec() != lb + 1, "a take over the loop goes into a new layer");
    tap(H1_BTN_REC);                             /* the take ends */
    go(100);
    CHECK(eng_layer_rec() == 0, "and is closed when REC ends it");
    CHECK(eng_layer_count() == 2, "two layers");
    n = 0;
    for (i = 0; i < NLAYER; i++)
        if (st.layer[i].part && i != lb)
            n += st.layer[i].env[3] == 95 && st.layer[i].env[0] == 90;
    CHECK(n == 1, "the second layer has the pad's envelope");
    /* playing: the first layer's notes sound in its own group (part and layer), the second's too */
    most = 0;
    for (i = 0; i < 400; i++) {
        int v, seen_a = 0, seen_b = 0;
        go(12);
        for (v = 0; v < NVOICE; v++)
            if (voices[v].stage && voices[v].seq && voices[v].lay) {
                if (voices[v].lay == lb + 1) seen_a = 1;
                else seen_b = 1;
            }
        most |= seen_a | seen_b << 1;
    }
    CHECK(most == 3, "both layers sound, each as itself");
    CHECK(mo_count(0x90, -1) > 0 && mo_count(0x80, -1) > 0, "the loop's notes go out over MIDI");
    CHECK(mo_count(0xB0, 20) > 0, "the knob went out as CC 20");

    /* ---- the loop, saved: only once it is stopped ---- */
    CHECK(!h1_loop_dirty(), "a playing loop is not written (the flash would stall it)");
    tap(H1_BTN_PLAY);                            /* stop */
    go(200);
    CHECK(h1_loop_dirty(), "a stopped loop that changed asks to be saved");
    p = (const uint8_t *)h1_loop(&lsize);
    CHECK(lsize > 12u && lsize <= H1_LOOP_MAX, "the loop as a blob");
    memcpy(lblob, p, lsize);
    h1_loop_saved();
    CHECK(!h1_loop_dirty(), "and is clean once saved");
    p = (const uint8_t *)h1_state(&size);
    memcpy(blob, p, size);
    h1_init(44100);                              /* a power cycle */
    go(100);
    CHECK(h1_state_load(blob, size) == 0, "the state loads");
    CHECK(h1_loop_load(lblob + 1, lsize - 1) != 0, "a broken loop is refused");
    CHECK(h1_loop_load(lblob, lsize) == 0, "the loop loads");
    CHECK(!h1_loop_dirty(), "a loaded loop is not saved again");
    CHECK(eng_layer_count() == 2, "with its two layers");
    {
        SeqInfo si;
        eng_seq_info(&si);
        CHECK(si.loop_state == LOOP_IDLE, "stopped, ready to play");
    }
    tap(H1_BTN_PLAY);
    most = 0;
    for (i = 0; i < 400; i++) {
        go(12);
        if (seq_voices() > most) most = seq_voices();
    }
    CHECK(most > 0, "and it plays after the restart");
    {
        static uint8_t odd[H1_LOOP_MAX + 3];
        memcpy(odd + 1, lblob, lsize);
        CHECK(h1_loop_load(odd + 1, lsize) == 0, "from any address");
    }

    /* ---- MIDI out: keys, MIDI in, the arpeggiator ---- */
    h1_init(44100);
    nmo = 0;
    h1_midi_out(mo_fn);
    go(100);
    note(7, 100);
    CHECK(mo_count(0x90, 60) == 1 && mo_count(0x80, 60) == 1, "a key goes out, on and off");
    for (i = 0; i < nmo; i++)
        if (mo[i][0] == 0x90 && mo[i][1] == 60)
            CHECK(mo[i][2] == 127, "at velocity 127");
    nmo = 0;
    h1_midi(0x90, 64, 90);
    go(100);
    h1_midi(0x80, 64, 0);
    go(100);
    CHECK(mo_count(0x90, -1) == 0 && mo_count(0x80, -1) == 0, "what came in over MIDI is not sent back");
    tap(H1_BTN_ARP);
    h1_control_set(H1_ENC_K1, 1);                /* UP */
    tap(H1_BTN_HOME);
    nmo = 0;
    h1_midi(0x90, 64, 90);                       /* the arpeggiator plays what is held, wherever it came from */
    h1_midi(0x90, 67, 90);
    go(1500);
    h1_midi(0x80, 64, 0);
    h1_midi(0x80, 67, 0);
    go(500);
    CHECK(mo_count(0x90, 64) > 0 && mo_count(0x90, 67) > 0, "the arpeggiator's notes go out");
    CHECK(mo_count(0x90, -1) == mo_count(0x80, -1), "each with its note-off");
    nmo = 0;
    h1_midi(0xB0, 123, 0);                       /* all notes off from the computer: not sent back (a computer that */
    go(100);                                     /* passes MIDI through would return it, for ever) */
    CHECK(mo_count(0xB0, 123) == 0, "a CC 123 that came in is not sent back");
    nmo = 0;
    h1_button(H1_BTN_OCTDN, 1);                  /* OCT- and OCT+ held: HUSH, and the computer is told (HOME */
    h1_button(H1_BTN_OCTUP, 1);                  /* held is the MIDI page now) */
    go(1000);
    h1_button(H1_BTN_OCTUP, 0);
    h1_button(H1_BTN_OCTDN, 0);
    go(100);
    CHECK(mo_count(0xB0, 123) == 1, "HUSH sends CC 123");
    h1_midi_out(0);

    /* ---- a 0.2.1 state (version 6): it loads, LAYERS on, its steps at full and on no layer ---- */
    h1_init(44100);
    pair();                                      /* LAYERS off, as a 0.2.1 FM-1 has it */
    h1_control_set(H1_ENC_PRESET, 3);
    p = (const uint8_t *)h1_state(&size);
    memset(v6, 0xAA, sizeof v6);
    memcpy(v6, p, H1_VERSION_6_SIZE);
    v6[4] = 6;
    h1_init(44100);
    CHECK(h1_state_load(v6, (uint32_t)H1_VERSION_6_SIZE) == 0, "a version 6 state loads");
    CHECK(h1_instrument() == 3, "with what it held");
    CHECK(h1_layers(), "and LAYERS on");
    n = 0;
    for (i = 0; i < NLAYER; i++)
        n += st.layer[i].part != 0;
    CHECK(n == 0 && st.step_vel[0][0] == 0 && st.step_lay[0][0] == 0, "and nothing past it but zeros");

    printf("v022: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
