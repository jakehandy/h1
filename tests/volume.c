/* Every instrument has a volume of its own beside MASTER: EDIT held, and SELECT. It is saved with the
 * instrument and goes back to full when the instrument is reset. Either volume shows as a ring in the
 * middle of the screen while it is turned, GLOBAL or INSTRUMENT. Run by tests/run_tests.sh: the core
 * alone, its panel worked as a hand would.
 *
 * The second half listens: an instrument turned down is the same sound, quieter; at nothing it is
 * silent; a turn of the knob does not click; and with LAYERS on, what was recorded on an instrument
 * follows that instrument's volume while another is in hand, which is what the volume is for. */
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

/* what came out since it was last cleared: how loud, the largest sample, and the largest step from one
 * sample to the next */
typedef struct Heard { double energy; int32_t peak, jump; uint32_t n; } Heard;
static Heard heard;
static int32_t last_l;
static void go(int msec)                         /* the sound and the frames, as the device runs them */
{
    static int32_t out[512];
    int blocks = msec * 441 / 2560 + 1, i, j;
    for (i = 0; i < blocks; i++) {
        h1_render(out, 256);
        for (j = 0; j < 512; j += 2) {
            int32_t s = out[j], a = s < 0 ? -s : s, d = s > last_l ? s - last_l : last_l - s;
            heard.energy += (double)s * (double)s;
            if (a > heard.peak) heard.peak = a;
            if (d > heard.jump) heard.jump = d;
            last_l = s;
            heard.n++;
        }
        if (i % 3 == 0)
            h1_frame(ms += 17);
    }
}
static void listen(void)
{
    heard.energy = 0.0;
    heard.peak = heard.jump = 0;
    heard.n = 0;
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
static void inst(int i) { h1_control_set(H1_ENC_PRESET, i); go(60); }
static int vol(void) { return h1_instrument_volume(h1_instrument()); }
static int tempo(void) { return st.glo[0]; }

/* EDIT held, and SELECT a click at a time, slowly: one unit a click */
static void turn(int clicks)
{
    int i, d = clicks < 0 ? -1 : 1, n = clicks < 0 ? -clicks : clicks;
    h1_button(H1_BTN_EDIT, 1);
    go(30);
    for (i = 0; i < n; i++) {
        h1_encoder(H1_ENC_SELECT, d);
        go(110);
    }
    h1_button(H1_BTN_EDIT, 0);
    go(40);
}
static void set_vol(int v)                       /* the way the simulator's slider does it */
{
    h1_button(H1_BTN_EDIT, 1);
    go(20);
    h1_control_set(H1_ENC_SELECT, v);
    go(20);
    h1_button(H1_BTN_EDIT, 0);
    go(40);
}

/* ---- takes, each in a process of its own, and before this one has made a sound (h1_init does not
 * wind the engine's clocks and random numbers back or empty the echo, so two takes in one process
 * would not be the same notes, and the second would have the first's tail in it). The instrument and
 * its volume are set before anything is rendered: COZY and ORBIT sound with no key down, and what one
 * of them puts in the echo is not the next instrument's to turn down. ---- */
enum { T_CHORD, T_TURNED, T_LAYER, T_LAYER_LIVE };
static void set_vol_now(int v)                   /* (no sound in between) */
{
    h1_button(H1_BTN_EDIT, 1);
    h1_control_set(H1_ENC_SELECT, v);
    h1_button(H1_BTN_EDIT, 0);
}
static Heard take(int kind, int i, int v)
{
    h1_init(44100);
    ms = 0;
    last_l = 0;
    h1_control_set(H1_ENC_PRESET, i);
    if (kind != T_TURNED)
        set_vol_now(v);
    go(1300);
    if (kind == T_CHORD) {                       /* three keys held at volume v */
        listen();
        h1_key(5, 1); h1_key(9, 1); h1_key(12, 1);
        go(1500);
        h1_key(5, 0); h1_key(9, 0); h1_key(12, 0);
        go(1500);
    } else if (kind == T_TURNED) {               /* the same keys, and the volume taken from full to v at a stroke */
        h1_key(5, 1); h1_key(9, 1); h1_key(12, 1);
        go(700);
        listen();
        if (v < 100)
            set_vol_now(v);
        go(600);
        h1_key(5, 0); h1_key(9, 0); h1_key(12, 0);
    } else {                                     /* LAYERS: a loop on instrument i at volume v, then another in hand */
        int other = (i + 5) % NINST < 2 ? 4 : (i + 5) % NINST;   /* (one that is silent with no key down) */
        if (!h1_layers())                    /* (on from the start since 0.2.2) */
            pair();
        tap(H1_BTN_REC);
        note(5, 200);
        go(300);
        note(12, 200);
        go(5000);                                /* the take runs out its two bars and plays */
        inst(other);
        go(1500);
        listen();
        if (kind == T_LAYER_LIVE) {
            h1_key(7, 1);
            go(1500);
            h1_key(7, 0);
            go(2500);
        } else
            go(4000);
    }
    return heard;
}
static Heard taken(int kind, int i, int v)
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
        h = take(kind, i, v);
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
    H1State *s = (H1State *)(void *)blob;
    uint32_t size = 0;
    const char *name = 0;
    SeqInfo si;
    int i, lo = -1, hi = -1, t0, hud0, quiet = 0;

    /* ---- listening (first: see take) ---- */
    for (i = 0; i < NINST; i++) {
        Heard full = taken(T_CHORD, i, 100), half = taken(T_CHORD, i, 50), none = taken(T_CHORD, i, 0);
        double ratio = full.energy > 0.0 ? half.energy / full.energy : 0.0;
        CHECK(full.peak > 20000, "an instrument at full is heard");
        /* 50 is a quarter of the amplitude (the gain is a square, as MASTER's is): a sixteenth of the
         * energy, give or take what the echo, the space and the output's soft clip do to a quieter sound */
        CHECK(ratio > 0.0625 * 0.5 && ratio < 0.0625 * 1.6, "at 50 it is a quarter as loud");
        CHECK(none.peak == 0, "at nothing it is silent");
        if (!(ratio > 0.0625 * 0.5 && ratio < 0.0625 * 1.6) || none.peak != 0)
            printf("  %s: full %.3g, half %.3g (ratio %.4f), peak at nothing %d\n", insts[i]->name, full.energy, half.energy,
                   ratio, (int)none.peak);
    }
    for (i = 0; i < NINST; i++) {
        /* full to nothing at a stroke under held keys: the sound goes down in about a hundredth of a
         * second, and no sample steps further from the last than the held sound's own did */
        Heard steady = taken(T_TURNED, i, 100), cut = taken(T_TURNED, i, 0);
        CHECK(cut.jump <= steady.jump + steady.jump / 4 + 2000, "full to nothing at a stroke does not click");
        if (cut.jump > steady.jump + steady.jump / 4 + 2000)
            printf("  %s: largest step %d held, %d turned down\n", insts[i]->name, (int)steady.jump, (int)cut.jump);
        quiet += cut.energy < steady.energy;
    }
    CHECK(quiet == NINST, "and turned down under the hand, every instrument is quieter");
    for (i = 0; i < NINST; i += 3) {
        /* LAYERS: the loop was played on one instrument and another is in hand. The loop follows its own
         * instrument's volume, and what the hand plays on the other is as loud as ever. */
        Heard full = taken(T_LAYER, i, 100), half = taken(T_LAYER, i, 50), none = taken(T_LAYER, i, 0);
        Heard live = taken(T_LAYER_LIVE, i, 0);
        CHECK(full.peak > 20000, "LAYERS: the loop sounds under another instrument");
        CHECK(half.energy < full.energy * 0.2 && half.energy > 0.0, "LAYERS: its instrument at 50, the loop is quieter");
        CHECK(none.peak == 0, "LAYERS: its instrument at nothing, the loop is silent");
        CHECK(live.peak > 20000, "LAYERS: and the instrument in hand is heard over it");
    }

    h1_init(44100);
    go(100);

    /* ---- as it comes ---- */
    for (i = 0; i < NINST; i++)
        CHECK(h1_instrument_volume(i) == 100, "a new h1: every instrument at full");
    CHECK(h1_volume_ring() == 0, "a new h1: no ring");

    /* ---- MASTER: the ring, GLOBAL ---- */
    h1_master(500);
    go(60);
    CHECK(h1_volume_ring() == 0, "MASTER's first reading shows nothing");
    h1_master(700);
    go(60);
    CHECK(h1_volume_ring() == 1, "MASTER turned: the ring, GLOBAL");
    go(1500);
    CHECK(h1_volume_ring() == 0, "the ring goes when MASTER is left alone");
    h1_master(704);
    go(60);
    CHECK(h1_volume_ring() == 0, "MASTER's own unrest does not bring the ring up");

    /* ---- EDIT: a tap is still a tap, on the way up ---- */
    hud0 = st.hud;
    h1_button(H1_BTN_EDIT, 1);
    go(60);
    CHECK(st.hud == hud0, "EDIT down: nothing yet");
    h1_button(H1_BTN_EDIT, 0);
    go(40);
    CHECK(st.hud == !hud0, "EDIT tapped: the knob strip is pinned");
    CHECK(h1_volume_ring() == 0, "EDIT tapped: no ring");
    tap(H1_BTN_EDIT);
    CHECK(st.hud == hud0, "EDIT tapped again: unpinned");
    CHECK(h1_global_volume() == -1, "EDIT let go without MASTER turned: everything's volume follows the knob");

    /* ---- EDIT held: the ring, INSTRUMENT, for as long as it is held, and no tap ---- */
    h1_button(H1_BTN_EDIT, 1);
    go(500);
    CHECK(h1_volume_ring() == 2, "EDIT held: the ring, INSTRUMENT");
    go(3000);
    CHECK(h1_volume_ring() == 2, "EDIT still held: the ring stays");
    CHECK(h1_global_volume() == 704, "EDIT held, MASTER not turned: everything's volume where the knob stands");
    i = vol();
    h1_master(708);                              /* (#38: a turn too small to count yet) */
    go(60);
    CHECK(h1_global_volume() == 704 && vol() == i, "a nudge under EDIT moves neither everything's volume nor the instrument's");
    h1_master(704);
    h1_master(300);                              /* MASTER turned meanwhile: it is the instrument's volume now */
    go(60);
    CHECK(h1_volume_ring() == 2, "EDIT held and MASTER turned: the ring is INSTRUMENT");
    CHECK(vol() == 29, "EDIT + MASTER: the instrument is where the knob stands");
    CHECK(h1_global_volume() == 704, "EDIT + MASTER: everything's volume stays where the knob was");
    h1_master(0);
    go(60);
    CHECK(vol() == 0, "EDIT + MASTER: the bottom of the knob is nothing");
    h1_master(8);
    go(60);
    CHECK(vol() == 0, "EDIT + MASTER: and so is a knob that does not quite reach the bottom");
    h1_master(990);
    go(60);
    CHECK(vol() == 100, "EDIT + MASTER: a knob that does not quite reach the top is full");
    h1_master(1000);
    go(60);
    CHECK(vol() == 100 && h1_global_volume() == 704, "EDIT + MASTER: the top is full, and everything's is still held");
    go(2000);
    CHECK(h1_volume_ring() == 2, "EDIT still held: the ring is still INSTRUMENT");
    h1_encoder(H1_ENC_PRESET, 1);                /* another instrument: its name first, then its ring */
    go(100);
    CHECK(h1_volume_ring() == 0, "EDIT held and PRESETS turned: the name has the middle of the screen");
    go(1500);
    CHECK(h1_volume_ring() == 2, "EDIT still held: the new instrument's ring");
    h1_encoder(H1_ENC_PRESET, -1);
    go(1500);
    h1_button(H1_BTN_EDIT, 0);
    go(40);
    CHECK(st.hud == hud0, "EDIT held is not a tap");
    go(1500);
    CHECK(h1_volume_ring() == 0, "EDIT let go: the ring goes");

    /* ---- EDIT held, and SELECT ---- */
    t0 = tempo();
    turn(-10);
    CHECK(vol() == 90, "EDIT + SELECT: ten clicks down is 90");
    CHECK(tempo() == t0, "EDIT + SELECT leaves the tempo alone");
    CHECK(st.hud == hud0, "EDIT + SELECT is not a tap of EDIT");
    CHECK(h1_state_dirty() || (go(1600), h1_state_dirty()), "a volume changed: something to save");
    turn(4);
    CHECK(vol() == 94, "and four up is 94");
    h1_button(H1_BTN_EDIT, 1);                   /* at once, before the hold has shown anything */
    h1_encoder(H1_ENC_SELECT, -1);
    CHECK(h1_volume_ring() == 2, "EDIT + SELECT at once: the ring is up already");
    h1_button(H1_BTN_EDIT, 0);
    go(40);
    CHECK(vol() == 93 && st.hud == hud0, "EDIT + SELECT at once: the volume, and no tap");
    h1_encoder(H1_ENC_SELECT, 1);
    go(40);
    CHECK(vol() == 93 && tempo() != t0, "SELECT alone is still the tempo");
    h1_button(H1_BTN_EDIT, 1);
    for (i = 0; i < 60; i++) {
        h1_encoder(H1_ENC_SELECT, -3);
        go(10);
    }
    CHECK(vol() == 0, "it stops at nothing");
    for (i = 0; i < 60; i++) {
        h1_encoder(H1_ENC_SELECT, 3);
        go(10);
    }
    CHECK(vol() == 100, "and at full");
    h1_control(H1_ENC_SELECT, &lo, &hi, &name, 0);
    CHECK(lo == 0 && hi == 100 && name && !strcmp(name, "VOLUME"), "EDIT held: SELECT is VOLUME, 0 to 100, for a slider");
    h1_control_set(H1_ENC_SELECT, 37);
    CHECK(vol() == 37 && h1_control(H1_ENC_SELECT, 0, 0, 0, 0) == 37, "and a slider sets it");
    h1_button(H1_BTN_EDIT, 0);
    go(40);
    h1_control(H1_ENC_SELECT, &lo, &hi, &name, 0);
    CHECK(name && !strcmp(name, "TEMPO"), "EDIT let go: SELECT is the tempo again");

    /* ---- each instrument its own ---- */
    for (i = 0; i < NINST; i++)
        CHECK(h1_instrument_volume(i) == (i == h1_instrument() ? 37 : 100), "one instrument turned down: only that one");
    inst(3);
    CHECK(vol() == 100, "another instrument: at full");
    set_vol(60);
    inst(0);
    CHECK(vol() == 37, "and back: where it was left");

    /* ---- on a page too ---- */
    tap(H1_BTN_FX);
    i = h1_page();
    CHECK(i != 0, "the FX page is open");
    turn(-7);
    CHECK(vol() == 30 && h1_page() == i, "on a page: EDIT + SELECT is the volume, and the page stays");
    tap(H1_BTN_HOME);

    /* ---- writing steps: EDIT tapped is a rest, EDIT + SELECT is still the volume ---- */
    hold(H1_BTN_SEQ, 700);
    tap(H1_BTN_REC);
    go(100);
    eng_seq_info(&si);
    CHECK(si.mode == SEQ_STEP && si.write && si.cursor == 0, "step-write, at the first step");
    hud0 = st.hud;
    tap(H1_BTN_EDIT);
    go(100);
    eng_seq_info(&si);
    CHECK(si.cursor == 1 && st.hud == hud0, "EDIT tapped while writing: a rest, and the strip is left alone");
    turn(-5);
    go(100);
    eng_seq_info(&si);
    CHECK(vol() == 25 && si.cursor == 1, "EDIT + SELECT while writing: the volume, no rest, the cursor stays");
    tap(H1_BTN_REC);
    hold(H1_BTN_SEQ, 700);
    go(100);

    /* ---- saved, and loaded ---- */
    inst(0);
    memcpy(blob, h1_state(&size), sizeof blob);
    CHECK(size == sizeof blob, "the state's size");
    h1_init(44100);
    CHECK(h1_instrument_volume(0) == 100, "a new h1 again");
    CHECK(h1_state_load(blob, size) == 0, "the saved state loads");
    CHECK(h1_instrument_volume(0) == 25 && h1_instrument_volume(3) == 60 && h1_instrument_volume(1) == 100,
          "the volumes come back as they were saved");
    for (i = 0; i < NINST; i++)                  /* what v0.1.0 saved: nothing where the volume now is */
        s->ip[i].mix[IP_CUT] = 0;
    h1_init(44100);
    CHECK(h1_state_load(blob, size) == 0, "a state from before the volume loads");
    for (i = 0; i < NINST; i++)
        CHECK(h1_instrument_volume(i) == 100, "a state from before the volume: every instrument at full");
    s->ip[2].mix[IP_CUT] = 100;
    s->ip[4].mix[IP_CUT] = 101;
    s->ip[6].mix[IP_CUT] = 255;
    h1_init(44100);
    CHECK(h1_state_load(blob, size) == 0, "a state with nonsense where the volume is still loads");
    CHECK(h1_instrument_volume(2) == 0 && h1_instrument_volume(4) == 100 && h1_instrument_volume(6) == 100,
          "nonsense where the volume is: full, not an instrument gone quiet");

    /* ---- the resets ---- */
    h1_init(44100);
    go(100);
    inst(2);
    set_vol(30);
    inst(5);
    set_vol(60);
    inst(2);
    hold(H1_BTN_SAVE, 1300);
    CHECK(vol() == 30, "SAVE held to the algorithm's reset: the volume is the instrument's, and stays");
    hold(H1_BTN_SAVE, 2800);
    CHECK(vol() == 100 && h1_instrument_volume(5) == 60, "SAVE held to the instrument's reset: it is at full, the others as they were");
    set_vol(45);
    hold(H1_BTN_SAVE, 4600);
    for (i = 0; i < NINST; i++)
        CHECK(h1_instrument_volume(i) == 100, "SAVE held to the end: every instrument at full");

    /* ---- MASTER after it has been the instrument's: everything's volume waits for the knob ---- */
    h1_init(44100);
    go(100);
    h1_master(600);
    go(60);
    h1_master(600);
    hud0 = st.hud;
    h1_button(H1_BTN_EDIT, 1);                   /* at once, before the hold has shown anything */
    h1_master(900);
    CHECK(h1_volume_ring() == 2 && vol() == 91, "EDIT + MASTER at once: the instrument's volume, its ring up already");
    CHECK(h1_global_volume() == 600, "EDIT + MASTER at once: everything's volume is held");
    h1_button(H1_BTN_EDIT, 0);
    go(40);
    CHECK(st.hud == hud0, "EDIT + MASTER is not a tap of EDIT");
    go(1500);
    h1_master(904);
    go(60);
    CHECK(h1_global_volume() == 600 && h1_volume_ring() == 0, "the knob's own unrest picks nothing up and shows nothing");
    h1_master(950);                              /* away from where everything's volume was left */
    go(60);
    CHECK(h1_global_volume() == 600 && vol() == 91, "MASTER turned away from it: everything's volume stays, and the instrument's");
    CHECK(h1_volume_ring() == 1, "and the ring is GLOBAL: where it is held, and where the knob is");
    h1_master(700);                              /* towards it, not there yet */
    go(60);
    CHECK(h1_global_volume() == 600, "MASTER turned towards it: not yet");
    h1_master(560);                              /* through it */
    go(60);
    CHECK(h1_global_volume() == -1, "MASTER turned through it: everything's volume follows the knob again");
    CHECK(vol() == 91, "and the instrument's volume is its own");
    h1_master(300);
    go(60);
    CHECK(h1_global_volume() == -1 && h1_volume_ring() == 1, "and goes on following");
    h1_button(H1_BTN_EDIT, 1);                   /* again, and this time the knob ends up close to it */
    h1_master(500);
    h1_master(310);
    h1_button(H1_BTN_EDIT, 0);
    go(1500);
    CHECK(h1_global_volume() == 300 && vol() == 30, "EDIT + MASTER again: held where it was");
    h1_master(325);
    go(60);
    CHECK(h1_global_volume() == 300, "MASTER nudged away from it: still held");
    go(1500);
    h1_master(340);
    go(60);
    CHECK(h1_global_volume() == 300, "MASTER moved away from it from beside it: still held");
    h1_master(305);
    go(60);
    CHECK(h1_global_volume() == -1, "MASTER brought close to it: picked up");
    h1_button(H1_BTN_EDIT, 1);
    h1_master(800);
    h1_button(H1_BTN_EDIT, 0);
    go(100);
    inst(3);
    CHECK(h1_global_volume() == 305, "another instrument in hand: everything's volume is still held");
    h1_button(H1_BTN_EDIT, 1);
    h1_master(400);
    h1_button(H1_BTN_EDIT, 0);
    go(100);
    CHECK(vol() == 40 && h1_instrument_volume(2) == 100 && h1_global_volume() == 305, "EDIT + MASTER while it is held: the instrument's, and the hold is not moved");
    h1_init(44100);
    CHECK(h1_global_volume() == -1, "a new h1 follows the knob");

    printf("volume: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
