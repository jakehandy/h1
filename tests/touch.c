/* Velocity and pressure, on the core alone, its panel worked as a port would (h1_key_vel, h1_key_pressure every
 * frame) and over MIDI. Run by tests/run_tests.sh.
 *
 *   velocity      a key pressed at a velocity sounds at it, and the picture has it; h1_key is 127, to the bit
 *   pressure      a lean reaches the note and the picture, eased, and goes when the key is let go
 *   not leant     a note of the loop (LAYERS on and off), the steps or the arpeggiator has no pressure
 *   the push      push_hold and push_move stay where they were pushed
 *   on a knob     a lean routed onto a home knob turns it, and changes the sound; not routed, it changes nothing
 *   MIDI in       poly aftertouch and channel pressure lean on what MIDI plays; a note-off and CC 123 let go
 *   let go        a key let go of is leant on no more, under HOLD (the note sounds on) and under channel pressure
 *                 (it is not the key's); a key pressed starts from none, whatever MIDI left on its note
 *   no jump       a knob a lean is routed onto eases down when the leant note goes at once (stolen, played again)
 *   layers        a closed layer is given no lean and no route
 *   HUSH          lets go of every lean, MIDI's channel pressure too
 *   MIDI out      a lean goes out as poly aftertouch, not too often, not with the arpeggiator, never MIDI's own;
 *                 back to 0 as the key is let go (HOLD too); one the port could not send is sent again
 *   saved         the route is saved; an older state, and one with nonsense where it goes, lean on nothing
 *
 * Each sound to compare is made in a process of its own (as in tests/v022.c). */
#define _POSIX_C_SOURCE 200809L
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include "../core/internal.h"
#include "../core/dsp.h"
#include "../core/h1.h"

static uint32_t ms;
static int fails, checks;
#define CHECK(c, what) do { checks++; if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, what); } } while (0)

typedef struct Heard { double energy, slope; uint32_t hash; f32 k0; } Heard;
static Heard heard;
static int32_t last_l;
static int lean_v[H1_NKEYS];                     /* each key's lean, handed to the core every frame */

static void frame(void)
{
    int k;
    for (k = 0; k < H1_NKEYS; k++)
        if (lean_v[k] >= 0)
            h1_key_pressure(k, lean_v[k]);
    h1_frame(ms += 17);
}
static void go(int msec)
{
    static int32_t out[512];
    int blocks = msec * 441 / 2560 + 1, i, j;
    for (i = 0; i < blocks; i++) {
        h1_render(out, 256);
        for (j = 0; j < 512; j += 2) {
            int32_t s = out[j];
            double d = (double)s - (double)last_l;
            heard.energy += (double)s * (double)s;
            heard.slope += d * d;
            heard.hash = heard.hash * 31u + (uint32_t)s;
            last_l = s;
        }
        if (i % 3 == 0)
            frame();
    }
}
static void start(void)
{
    int k;
    h1_init(44100);
    for (k = 0; k < H1_NKEYS; k++)
        lean_v[k] = -1;
    go(100);
}
static void listen(void) { memset(&heard, 0, sizeof heard); }
static void tap(int b) { h1_button(b, 1); go(40); h1_button(b, 0); go(40); }
static f32 vis_press(void) { Vis v; eng_snapshot(&v); return v.press; }
static void one_block(void) { static int32_t out[2 * CTRL]; h1_render(out, CTRL); }
static void up(int key) { h1_key(key, 0); lean_v[key] = -1; }

static Voice *sounding(int note)
{
    int i;
    for (i = 0; i < NVOICE; i++)
        if (voices[i].stage && voices[i].gate && voices[i].note == note)
            return &voices[i];
    return 0;
}
static f32 most_pressed(void)                    /* the hardest lean on any voice that sounds */
{
    f32 p = 0.0f;
    int i;
    for (i = 0; i < NVOICE; i++)
        if (voices[i].stage && voices[i].press > p)
            p = voices[i].press;
    return p;
}

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

/* one note on LAVA BURN, from the panel: vel 0 is h1_key, else h1_key_vel at vel; lean on it, routed or not */
static Heard one_note(int vel, int lean_route)
{
    int lean = lean_route & 255, route = lean_route >> 8;
    Vis vis;
    start();
    h1_control_set(H1_ENC_PRESET, 9);
    go(300);
    h1_pressure_route(9, route ? 1 : 0, route ? 70 : 0);
    go(100);
    listen();
    if (vel)
        h1_key_vel(7, 1, vel);
    else
        h1_key(7, 1);
    lean_v[7] = lean;
    go(700);
    eng_snapshot(&vis);
    up(7);
    go(800);
    heard.k0 = vis.k[0];
    return heard;
}

/* ---- MIDI out ---- */
static uint8_t mo[1024][3];
static int nmo;
static int refuse;                               /* the port's queue is full: it sends nothing */
static void mo_fn(uint8_t s, uint8_t a, uint8_t b)
{
    if (refuse) {
        h1_midi_out_dropped(s, a, b);
        return;
    }
    if (nmo < 1024) {
        mo[nmo][0] = s;
        mo[nmo][1] = a;
        mo[nmo][2] = b;
        nmo++;
    }
}
static int count_out(uint8_t status, int note)
{
    int i, n = 0;
    for (i = 0; i < nmo; i++)
        n += mo[i][0] == status && (note < 0 || mo[i][1] == note);
    return n;
}
static int last_out(uint8_t status, int note)
{
    int i, v = -1;
    for (i = 0; i < nmo; i++)
        if (mo[i][0] == status && mo[i][1] == note)
            v = mo[i][2];
    return v;
}

int main(void)
{
    Voice *v;
    Vis vis;
    Heard a, b, c, d;
    const uint8_t *p;
    static uint8_t blob[4096];
    uint32_t size;
    int i, n, depth;
    f32 x, y;

    /* ---- velocity, from the panel ---- */
    start();
    h1_key_vel(7, 1, 40);                        /* (key 7 is note 60) */
    go(30);
    v = sounding(60);
    CHECK(v && v->vel == 40, "a key pressed at 40 sounds at 40");
    eng_snapshot(&vis);
    CHECK(v && vis.v[v - voices].vel == 40, "and the picture has it");
    h1_key(9, 1);
    go(30);
    v = sounding(62);
    CHECK(v && v->vel == 127, "h1_key is full velocity");
    h1_key_vel(9, 1, 1);                         /* (held already: nothing) */
    go(30);
    CHECK(v && v->vel == 127, "a second press of a held key changes nothing");
    up(7);
    up(9);
    go(300);
    h1_key_vel(7, 1, 0);
    go(30);
    v = sounding(60);
    CHECK(v && v->vel == 1, "a velocity below 1 is 1");
    up(7);
    a = taken(one_note, 0, 0);
    b = taken(one_note, 127, 0);
    c = taken(one_note, 30, 0);
    CHECK(a.hash == b.hash, "h1_key_vel at 127 is h1_key to the bit");
    CHECK(c.energy < a.energy * 0.5, "a soft key is quieter");

    /* ---- pressure reaches the note and the picture ---- */
    start();
    h1_key_vel(7, 1, 100);
    lean_v[7] = 127;
    go(20);
    v = sounding(60);
    CHECK(v && v->press > 0.0f && v->press < 0.9f, "a lean comes in eased, not at once");
    go(200);
    CHECK(v && v->press > 0.98f, "a full lean is a pressure of 1");
    eng_snapshot(&vis);
    CHECK(v && vis.v[v - voices].press == v->press, "the picture has the note's pressure");
    CHECK(vis.press > 0.98f, "and the instrument's");
    lean_v[7] = 64;
    go(200);
    CHECK(v && v->press > 0.48f && v->press < 0.53f, "half a lean is half");
    h1_key_vel(4, 1, 100);                       /* another key, leant on less */
    lean_v[4] = 20;
    go(200);
    eng_snapshot(&vis);
    CHECK(vis.press > 0.48f && vis.press < 0.53f, "the instrument's is the hardest of its notes'");
    up(7);
    go(10);
    CHECK(v && v->press < 0.5f, "a key let go: its note's lean goes");
    go(300);
    CHECK(!v->stage || v->press == 0.0f, "all the way");
    up(4);
    go(300);
    CHECK(most_pressed() == 0.0f, "nothing held, nothing leant on");

    /* ---- not leant: the loop, LAYERS on and off, the steps, the arpeggiator ---- */
    for (n = 0; n < 2; n++) {
        start();
        if (n == 1) {                            /* LAYERS off (SEQ and REC together): the loop plays through route */
            h1_button(H1_BTN_SEQ, 1);
            go(30);
            h1_button(H1_BTN_REC, 1);
            go(30);
            h1_button(H1_BTN_REC, 0);
            h1_button(H1_BTN_SEQ, 0);
            go(40);
        }
        CHECK(h1_layers() == !n, "LAYERS as asked");
        tap(H1_BTN_REC);
        h1_key_vel(7, 1, 100);
        lean_v[7] = 127;
        go(400);
        CHECK(most_pressed() > 0.9f, "leant on while it is recorded");
        up(7);
        go(3000);                                /* (round to where the note plays again) */
        tap(H1_BTN_REC);
        go(4000);
        for (i = 0; i < 40; i++) {
            h1_midi(0xA0, 60, 127);              /* the note, leant on over MIDI, while only the loop plays it */
            h1_midi(0xD0, 127, 0);
            go(100);
            if (sounding(60))
                break;
        }
        CHECK(sounding(60) != 0, "the loop plays the note");
        go(50);
        CHECK(sounding(60) && sounding(60)->press == 0.0f, "and nothing leans on it");
        eng_snapshot(&vis);
        CHECK(vis.press == 0.0f, "nor on the instrument");
    }
    start();                                     /* the steps */
    h1_button(H1_BTN_SEQ, 1);
    go(700);
    h1_button(H1_BTN_SEQ, 0);
    go(40);
    tap(H1_BTN_REC);                             /* (step write) */
    h1_key(7, 1);
    go(40);
    up(7);
    go(40);
    tap(H1_BTN_REC);
    tap(H1_BTN_PLAY);
    h1_midi(0xA0, 60, 127);
    h1_midi(0xD0, 127, 0);
    for (i = 0; i < 40 && !sounding(60); i++)
        go(20);
    go(20);
    CHECK(sounding(60) && sounding(60)->press == 0.0f, "a step's note is not leant on");
    start();                                     /* the arpeggiator */
    st.ip[st.inst].arp[0] = 1;
    h1_key_vel(7, 1, 100);
    lean_v[7] = 127;
    for (i = 0, n = 0; i < 20; i++) {
        go(50);
        n += sounding(60) != 0;
        CHECK(most_pressed() == 0.0f, "the arpeggiator's notes are not leant on");
    }
    CHECK(n > 0, "and it played");
    up(7);

    /* ---- the push ---- */
    x = 0.0f;
    push_hold(&x, 0.2f);
    push_hold(&x, 0.6f);
    CHECK(push_hold(&x, 0.3f) == 0.6f, "push_hold keeps the hardest lean");
    CHECK(push_hold(&x, 2.0f) == 1.0f, "and stops at 1");
    y = 0.25f;
    CHECK(push_move(&y, 0.1f, 0.2f, 1.0f, 0.5f) == 0.25f, "push_move: a lean under the dead zone moves nothing");
    CHECK(push_move(&y, 1.0f, 0.2f, 1.0f, 0.5f) == 0.75f, "a full lean moves it speed a second");
    CHECK(push_move(&y, 0.0f, 0.2f, 1.0f, 0.5f) == 0.75f, "eased, it stays");
    CHECK(push_move(&y, 0.6f, 0.2f, 1.0f, 0.5f) == 1.0f, "and it stops at 1");

    /* ---- on a knob: LAVA's FOLD ---- */
    a = taken(one_note, 100, 0);
    b = taken(one_note, 100, 127);
    c = taken(one_note, 100, 1 << 8);
    d = taken(one_note, 100, 1 << 8 | 127);
    CHECK(a.hash == b.hash, "not routed, a lean changes nothing");
    CHECK(a.hash == c.hash, "routed, no lean changes nothing");
    CHECK(d.hash != a.hash && d.slope / d.energy > a.slope / a.energy * 1.2, "routed, a lean folds the sound brighter");
    CHECK(d.k0 > a.k0 + 0.6f, "and the picture's FOLD turns with it");
    start();
    h1_control_set(H1_ENC_PRESET, 9);
    go(100);
    KNOBS_NOW(9)[0] = 80;
    h1_pressure_route(9, 1, -50);
    CHECK(h1_pressure_routed(9, &depth) == 1 && depth == -50, "a route reads back");
    h1_key_vel(7, 1, 100);
    lean_v[7] = 127;
    go(600);
    eng_snapshot(&vis);
    CHECK(vis.k[0] > 0.27f && vis.k[0] < 0.33f, "a route the other way turns the knob down");
    h1_pressure_route(9, 7, 300);
    CHECK(h1_pressure_routed(9, &depth) == 0 && depth == 100, "a route to no knob is none, a depth past 100 is 100");
    up(7);

    /* ---- MIDI in ---- */
    start();
    h1_midi(0x90, 60, 100);
    h1_midi(0xA0, 60, 127);
    go(200);
    v = sounding(60);
    CHECK(v && v->press > 0.98f, "poly aftertouch leans on its note");
    h1_midi(0x90, 64, 100);
    go(200);
    CHECK(sounding(64) && sounding(64)->press == 0.0f, "and only on its note");
    h1_midi(0xA0, 60, 0);
    go(300);
    CHECK(v && v->press < 0.01f, "aftertouch 0 lets go");
    h1_midi(0xD0, 64, 0);
    go(300);
    CHECK(v && v->press > 0.48f && sounding(64)->press > 0.48f, "channel pressure leans on every note");
    h1_midi(0xD0, 0, 0);
    h1_midi(0xA0, 64, 127);
    h1_midi(0x80, 64, 0);
    h1_midi(0x90, 64, 90);
    go(100);
    CHECK(sounding(64) && sounding(64)->press == 0.0f, "a note let go and played again starts with no lean");
    h1_midi(0xD0, 100, 0);
    h1_midi(0xB0, 123, 0);
    h1_midi(0x90, 60, 90);
    go(100);
    CHECK(sounding(60) && sounding(60)->press == 0.0f, "CC 123 lets go of all of it, the channel's too");

    /* ---- MIDI out ---- */
    start();
    h1_midi_out(mo_fn);
    nmo = 0;
    h1_key_vel(7, 1, 100);
    go(100);
    for (i = 0; i <= 127; i += 8) {              /* a lean swept in over about 200 ms */
        lean_v[7] = i;
        go(17);
    }
    lean_v[7] = 127;
    go(300);
    n = count_out(0xA0, 60);
    CHECK(n >= 3, "a lean goes out as poly aftertouch on its note");
    CHECK(n <= 12, "not every frame");
    CHECK(last_out(0xA0, 60) == 127, "and arrives where the lean did");
    lean_v[7] = 70;
    go(300);
    lean_v[7] = 77;                              /* (out at once) */
    go(40);
    lean_v[7] = 76;                              /* (and then a reading's noise) */
    go(60);
    CHECK(last_out(0xA0, 60) == 77, "a move of one waits");
    go(300);
    CHECK(last_out(0xA0, 60) == 76, "and goes once it has stood");
    up(7);
    go(100);
    nmo = 0;
    h1_midi(0x90, 62, 100);
    h1_midi(0xA0, 62, 100);
    h1_midi(0xD0, 100, 0);
    go(200);
    CHECK(count_out(0xA0, -1) == 0 && count_out(0xD0, -1) == 0, "what came in over MIDI does not go out");
    h1_midi(0x80, 62, 0);
    st.ip[st.inst].arp[0] = 1;
    h1_key_vel(7, 1, 100);
    lean_v[7] = 100;
    go(500);
    CHECK(count_out(0xA0, -1) == 0, "with the arpeggiator, no aftertouch goes out");
    up(7);
    nmo = 0;                                     /* HOLD: the key let go, the note sounds on, its lean goes to 0 */
    st.ip[st.inst].arp[0] = 0;
    st.keys[3] = 1;
    h1_key_vel(9, 1, 100);
    lean_v[9] = 127;
    go(300);
    CHECK(last_out(0xA0, 62) == 127, "HOLD: leant on, out");
    up(9);
    go(100);
    CHECK(last_out(0xA0, 62) == 0 && count_out(0x80, 62) == 0, "let go, its aftertouch goes back to 0, the note held");
    st.keys[3] = 0;
    h1_midi(0xB0, 123, 0);
    go(200);
    nmo = 0;                                     /* the port's queue full: what did not go is sent again */
    h1_key_vel(7, 1, 100);
    lean_v[7] = 127;
    refuse = 1;
    go(200);
    CHECK(count_out(0xA0, 60) == 0, "nothing goes while the queue is full");
    refuse = 0;
    go(60);
    CHECK(last_out(0xA0, 60) == 127, "and the lean goes once there is room");
    refuse = 1;
    up(7);
    go(100);
    refuse = 0;
    go(60);
    CHECK(last_out(0xA0, 60) == 0, "a 0 at the release that did not go goes later");
    h1_midi_out(0);

    /* ---- let go: under HOLD, and under channel pressure ---- */
    start();
    st.keys[3] = 1;                              /* HOLD: the note goes on sounding, held, after the key */
    h1_key_vel(7, 1, 100);
    lean_v[7] = 127;
    go(300);
    v = sounding(60);
    CHECK(v && v->press > 0.98f, "HOLD: leant on");
    up(7);
    go(300);
    CHECK(sounding(60) && sounding(60)->press < 0.01f, "let go, the held note is leant on no more");
    st.keys[3] = 0;
    h1_midi(0xB0, 123, 0);
    start();
    st.ip[st.inst].env[3] = 100;                 /* (a long release: the note still sounds after the key) */
    h1_midi(0xD0, 100, 0);                       /* channel pressure, from a MIDI keyboard */
    h1_key_vel(7, 1, 100);
    go(300);
    for (i = 0, v = 0; i < NVOICE; i++)
        if (voices[i].stage && voices[i].note == 60)
            v = &voices[i];
    CHECK(v && v->press > 0.75f, "channel pressure leans on a key's note");
    up(7);
    go(300);
    CHECK(v && v->stage && !v->gate && v->press < 0.01f, "let go, the note dying away is leant on no more");
    h1_midi(0xD0, 0, 0);
    start();
    h1_midi(0xA0, 60, 127);                      /* MIDI left a lean on the note, with no note of its own */
    h1_key_vel(7, 1, 100);
    go(300);
    CHECK(sounding(60) && sounding(60)->press == 0.0f, "a key pressed starts from no lean");
    up(7);

    /* ---- no jump: the leant note goes at once ---- */
    for (n = 0; n < 2; n++) {
        f32 was, worst = 0.0f;
        start();
        h1_control_set(H1_ENC_PRESET, 9);
        go(100);
        h1_pressure_route(9, 1, 70);
        h1_midi(0x90, 60, 100);
        h1_midi(0xA0, 60, 127);
        go(300);
        was = vis_press();
        CHECK(was > 0.98f, "leant on over MIDI");
        if (n == 0)
            h1_midi(0x90, 60, 100);              /* played again, with no note-off: it starts from none */
        else
            for (i = 0; i < NVOICE; i++)         /* eight more notes: the leant one is taken */
                h1_midi(0x90, (uint8_t)(64 + i), 100);
        for (i = 0; i < 60; i++) {
            f32 p;
            one_block();
            p = vis_press();
            if (was - p > worst)
                worst = was - p;
            was = p;
        }
        CHECK(worst < 0.06f, n ? "a leant note taken for another: the lean eases away" : "a leant note played again: the lean eases away");
        CHECK(was < 0.2f, "but it does go");
        h1_midi(0xB0, 123, 0);
    }

    /* ---- layers: a closed layer has no lean and no route ---- */
    start();
    h1_control_set(H1_ENC_PRESET, 9);            /* LAVA: a take of a note held, which comes round and closes */
    go(300);
    tap(H1_BTN_REC);
    h1_key(7, 1);
    go(3800);
    up(7);
    go(1500);
    for (i = 0, n = 0; i < NLAYER; i++)
        if (st.layer[i].part == PART(9, 0) && !st.layer[i].open)
            n = i + 1;
    CHECK(n > 0, "a closed layer on LAVA");
    h1_pressure_route(9, 1, 70);
    h1_key_vel(12, 1, 100);                      /* and a key on LAVA, leant on, while the layer's note plays */
    lean_v[12] = 127;
    for (i = 0, depth = 0; i < 60; i++) {
        go(100);
        if (n && sounding(60) && sounding(60)->lay == n && vis_press() > 0.98f) {
            depth = 1;
            break;
        }
    }
    CHECK(depth, "the layer's note sounds while the key is leant on");
    CHECK(n && eng_lay_blk(n)->press == 0.0f, "the layer's block() is given no lean");
    CHECK(n && eng_lay_blk(n)->k[0] < (f32)st.layer[n - 1].knob[0] * 0.01f + 0.01f, "and its FOLD no route");
    up(12);

    /* ---- HUSH: every lean let go ---- */
    start();
    h1_midi(0xD0, 100, 0);
    h1_midi(0xA0, 64, 100);
    h1_button(H1_BTN_HOME, 1);                   /* (held: HUSH) */
    go(900);
    h1_button(H1_BTN_HOME, 0);
    go(100);
    h1_key_vel(7, 1, 100);
    h1_midi(0x90, 64, 100);
    go(200);
    CHECK(sounding(60) && sounding(60)->press == 0.0f && sounding(64) && sounding(64)->press == 0.0f,
          "after HUSH nothing is leant on, MIDI's channel pressure included");
    up(7);

    /* ---- saved ---- */
    start();
    h1_pressure_route(4, 3, -40);
    p = (const uint8_t *)h1_state(&size);
    memcpy(blob + 1, p, size);
    h1_init(44100);
    CHECK(h1_pressure_routed(4, 0) == 0, "a new start leans on nothing");
    go(2000);
    h1_state_saved();
    go(2000);
    CHECK(!h1_state_dirty(), "(saved, nothing to save)");
    h1_pressure_route(4, 3, -40);
    go(1600);
    CHECK(h1_state_dirty(), "a route set is saved");
    CHECK(h1_state_load(blob + 1, size) == 0 && h1_pressure_routed(4, &depth) == 3 && depth == -40,
          "a route is saved");
    blob[1 + offsetof(H1State, ip) + 5 * sizeof(InstParams) + offsetof(InstParams, mix) + IP_PTO] = 9;
    blob[1 + offsetof(H1State, ip) + 6 * sizeof(InstParams) + offsetof(InstParams, mix) + IP_PTO] = 2;
    blob[1 + offsetof(H1State, ip) + 6 * sizeof(InstParams) + offsetof(InstParams, mix) + IP_PDEPTH] = 0x90;
    CHECK(h1_state_load(blob + 1, size) == 0, "a state with nonsense where the route goes loads");
    CHECK(h1_pressure_routed(5, 0) == 0 && h1_pressure_routed(6, 0) == 0 && h1_pressure_routed(4, 0) == 3,
          "and leans on nothing there");
    h1_init(44100);
    p = (const uint8_t *)h1_state(&size);
    memcpy(blob, p, H1_VERSION_8_SIZE);
    blob[4] = 8;
    blob[offsetof(H1Old, ip) + 2 * sizeof(InstParams) + offsetof(InstParams, mix) + IP_PTO] = 0xAA;
    blob[offsetof(H1Old, ip) + 2 * sizeof(InstParams) + offsetof(InstParams, mix) + IP_PDEPTH] = 0xAA;
    h1_init(44100);
    CHECK(h1_state_load(blob, (uint32_t)H1_VERSION_8_SIZE) == 0, "a version 8 state (0.2.2) loads");
    for (i = 0, n = 0; i < NINST; i++)
        n += h1_pressure_routed(i, 0) != 0;
    CHECK(n == 0, "and leans on nothing");
    memcpy(blob, p, H1_VERSION_6_SIZE);
    blob[4] = 6;
    h1_init(44100);
    CHECK(h1_state_load(blob, (uint32_t)H1_VERSION_6_SIZE) == 0, "a version 6 state (0.2.1) loads");
    for (i = 0, n = 0; i < NINST; i++)
        n += h1_pressure_routed(i, 0) != 0;
    CHECK(n == 0, "and leans on nothing");

    printf("touch: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
