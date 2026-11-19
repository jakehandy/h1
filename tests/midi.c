/* The MIDI page (HOME held), on the core alone, its panel worked as a hand would. Run by tests/run_tests.sh.
 *
 *   the page        HOME held opens it, HOME goes back; OCT- and OCT+ held is HUSH now
 *   CLOCK           off on a new h1; ON: 24 a quarter note at GLOBAL's tempo, evenly spaced to within a control
 *                   block, with nothing playing, the same with SWING; +START: Start and its downbeat clock when a
 *                   take starts, Stop when PLAY stops it, Stop when CLOCK goes off while it runs
 *   CHAN            notes and the knobs' CCs on the channel set, an all-notes-off on the old one first
 *   SEND            LIVE: the keys and not the loop's playback; OFF: no notes, the clock still
 *   BT              h1_bluetooth follows it
 *   the state       the page is kept; a 0.2.2 state (the bytes zero) is h1's MIDI as it was; any bytes load
 *                   clamped */
#include <stdio.h>
#include <string.h>
#include "../core/internal.h"
#include "../core/h1.h"

static uint32_t ms, frames;
static int fails, checks;
#define CHECK(c, what) do { checks++; if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, what); } } while (0)

#define NMO 4096
static uint8_t mo[NMO][3];
static uint32_t mo_at[NMO];                      /* the frame it went out in (to within a control block) */
static int nmo;
static void mo_fn(uint8_t s, uint8_t a, uint8_t b)
{
    if (nmo < NMO) {
        mo[nmo][0] = s;
        mo[nmo][1] = a;
        mo[nmo][2] = b;
        mo_at[nmo] = frames;
        nmo++;
    }
}
static int mo_count(uint8_t s, int d1)           /* (d1 < 0: any) */
{
    int i, n = 0;
    for (i = 0; i < nmo; i++)
        if (mo[i][0] == s && (d1 < 0 || mo[i][1] == d1))
            n++;
    return n;
}
static int mo_find(uint8_t s, int from)
{
    int i;
    for (i = from; i < nmo; i++)
        if (mo[i][0] == s)
            return i;
    return -1;
}
static int notes_out(void)                       /* note-ons on any channel */
{
    int i, n = 0;
    for (i = 0; i < nmo; i++)
        if ((mo[i][0] & 0xF0u) == 0x90u)
            n++;
    return n;
}

/* the audio a control block at a time, so a clock's frame is known to within 32; a frame every 17 ms */
static void go(int msec)
{
    static int32_t out[2 * CTRL];
    uint32_t end = frames + (uint32_t)msec * 441u / 10u, next_frame = frames;
    while (frames < end) {
        h1_render(out, CTRL);
        frames += CTRL;
        if (frames >= next_frame) {
            h1_frame(ms += 17);
            next_frame = frames + 750u;
        }
    }
}
static void tap(int b) { h1_button(b, 1); go(40); h1_button(b, 0); go(40); }
static void hold(int b) { h1_button(b, 1); go(900); h1_button(b, 0); go(40); }
static void note(int key, int msec) { h1_key(key, 1); go(msec); h1_key(key, 0); go(60); }
static void midi_page(void)
{
    if (ui_page() != PG_MIDI)
        hold(H1_BTN_HOME);
}

/* the clocks between two indices of mo: how many, and how evenly (the widest gap from the tempo's, in frames) */
static int clocks(int from, int to, double bpm, double *worst)
{
    int i, n = 0, last = -1;
    double want = 44100.0 * 60.0 / bpm / 24.0;
    *worst = 0.0;
    for (i = from; i < to; i++)
        if (mo[i][0] == 0xF8u) {
            if (last >= 0) {
                double d = (double)(mo_at[i] - mo_at[last]) - want;
                if (d < 0) d = -d;
                if (d > *worst) *worst = d;
            }
            last = i;
            n++;
        }
    return n;
}

int main(void)
{
    double worst;
    int a, b, n;
    uint32_t size;
    static uint8_t blob[4096];
    const uint8_t *p;

    h1_init(44100);
    h1_midi_out(mo_fn);
    go(200);

    /* ---- the page ---- */
    hold(H1_BTN_HOME);
    CHECK(ui_page() == PG_MIDI, "HOME held opens the MIDI page");
    tap(H1_BTN_HOME);
    CHECK(ui_page() == PG_HOME, "HOME goes back");
    tap(H1_BTN_GLO);
    hold(H1_BTN_HOME);
    CHECK(ui_page() == PG_MIDI, "HOME held opens it from another page too");
    tap(H1_BTN_HOME);
    nmo = 0;
    h1_button(H1_BTN_OCTDN, 1);
    h1_button(H1_BTN_OCTUP, 1);
    go(900);
    h1_button(H1_BTN_OCTUP, 0);
    h1_button(H1_BTN_OCTDN, 0);
    go(40);
    CHECK(mo_count(0xB0, 123) == 1, "OCT- and OCT+ held: HUSH, and CC 123 goes out");
    nmo = 0;
    tap(H1_BTN_OCTDN);
    h1_button(H1_BTN_OCTDN, 1);                  /* a tap of both: the middle octave, and no HUSH */
    h1_button(H1_BTN_OCTUP, 1);
    go(100);
    h1_button(H1_BTN_OCTUP, 0);
    h1_button(H1_BTN_OCTDN, 0);
    go(900);
    CHECK(mo_count(0xB0, 123) == 0, "a tap of OCT- and OCT+ is not HUSH");
    CHECK(st.octave == 0, "a tap of both is the middle octave, as before");

    /* ---- CLOCK ---- */
    nmo = 0;
    go(2000);
    CHECK(mo_count(0xF8, -1) == 0, "no clock on a new h1");
    midi_page();
    h1_control_set(H1_ENC_K3, MC_CLOCK);
    CHECK(st.midi[2] == MC_CLOCK, "CLOCK ON from the third knob");
    tap(H1_BTN_HOME);
    h1_control_set(H1_ENC_SELECT, 120);          /* home: SELECT is the tempo */
    CHECK(st.glo[0] == 120, "120 BPM");
    go(500);
    a = nmo;
    go(10000);
    n = clocks(a, nmo, 120.0, &worst);
    CHECK(n >= 479 && n <= 481, "120 BPM: 48 clocks a second, with nothing playing");
    CHECK(worst <= 33.0, "each clock where the tempo puts it, to within a control block");
    printf("  clock at 120 BPM: %d in 10 s, the widest gap off by %.0f frames (%.2f ms)\n", n, worst, worst / 44.1);
    st.glo[1] = 100;                             /* SWING all the way: the clock stays straight */
    h1_control_set(H1_ENC_SELECT, 90);
    go(500);
    a = nmo;
    go(10000);
    n = clocks(a, nmo, 90.0, &worst);
    CHECK(n >= 359 && n <= 361, "90 BPM with SWING: 36 clocks a second");
    CHECK(worst <= 33.0, "and straight");
    st.glo[1] = 0;
    CHECK(mo_count(0xFA, -1) == 0 && mo_count(0xFC, -1) == 0, "CLOCK ON sends no Start or Stop");

    /* ---- +START ---- */
    midi_page();
    h1_control_set(H1_ENC_K3, MC_START);
    tap(H1_BTN_HOME);
    nmo = 0;
    tap(H1_BTN_REC);                             /* a take */
    note(0, 200);
    go(6000);                                    /* it ends and plays */
    a = mo_find(0xFA, 0);
    CHECK(mo_count(0xFA, -1) == 1, "a take: one Start");
    CHECK(a >= 0 && a + 1 < nmo && mo[a + 1][0] == 0xF8u, "and its downbeat clock right after it");
    b = mo_find(0x90, 0);
    CHECK(a >= 0 && b >= 0 && mo_at[a] == mo_at[b], "the Start in the same control block as the take's first note");
    CHECK(mo_count(0xFC, -1) == 0, "no Stop while the loop goes round");
    tap(H1_BTN_PLAY);                            /* stop */
    go(500);
    CHECK(mo_count(0xFC, -1) == 1, "PLAY stops it: Stop");
    a = nmo;
    go(1000);
    CHECK(clocks(a, nmo, 90.0, &worst) >= 35, "the clock runs on after the Stop");
    tap(H1_BTN_PLAY);                            /* again */
    go(500);
    CHECK(mo_count(0xFA, -1) == 2, "PLAY: Start again");
    midi_page();
    h1_control_set(H1_ENC_K3, MC_OFF);           /* off while it runs */
    go(200);
    CHECK(mo_count(0xFC, -1) == 2, "CLOCK OFF while it runs: Stop");
    a = nmo;
    go(1000);
    CHECK(mo_count(0xF8, -1) == clocks(0, a, 90.0, &worst), "and no more clocks");
    tap(H1_BTN_HOME);
    tap(H1_BTN_PLAY);
    go(300);

    /* ---- CHAN ---- */
    nmo = 0;
    midi_page();
    h1_control_set(H1_ENC_K1, 9);                /* (the control's value is the stored 0..15; shown 1..16) */
    CHECK(st.midi[0] == 9, "CHAN 10");
    CHECK(mo_count(0xB0, 123) == 1, "an all-notes-off on channel 1 first");
    tap(H1_BTN_HOME);
    note(3, 100);
    CHECK(mo_count(0x99, -1) == 1 && mo_count(0x89, -1) == 1, "the keys on channel 10");
    CHECK(mo_count(0x90, -1) == 0, "and not on 1");
    h1_control_set(H1_ENC_K1, 30);
    CHECK(mo_count(0xB9, 20) == 1, "the first knob: CC 20 on channel 10");

    /* ---- SEND ---- */
    midi_page();
    h1_control_set(H1_ENC_K1, 0);
    h1_control_set(H1_ENC_K2, MS_LIVE);
    tap(H1_BTN_HOME);
    tap(H1_BTN_REC);
    note(0, 200);
    go(6000);                                    /* the take plays round */
    nmo = 0;
    go(4000);
    CHECK(notes_out() == 0, "SEND LIVE: the loop's playback does not go out");
    note(7, 100);
    CHECK(mo_count(0x90, -1) == 1, "the keys do");
    midi_page();
    h1_control_set(H1_ENC_K3, MC_CLOCK);
    h1_control_set(H1_ENC_K2, MS_OFF);
    tap(H1_BTN_HOME);
    nmo = 0;
    note(7, 100);
    h1_control_set(H1_ENC_K2, 60);
    go(1000);
    CHECK(notes_out() == 0, "SEND OFF: no notes");
    CHECK(mo_count(0xB0, 21) == 0, "and no knobs");
    CHECK(mo_count(0xF8, -1) > 30, "the clock still goes");
    tap(H1_BTN_PLAY);
    go(300);

    /* ---- BT ---- */
    CHECK(h1_bluetooth() == 1, "Bluetooth on by default");
    midi_page();
    h1_control_set(H1_ENC_K4, 1);
    CHECK(h1_bluetooth() == 0, "BT OFF");
    tap(H1_BTN_HOME);

    /* ---- the state ---- */
    p = (const uint8_t *)h1_state(&size);
    memcpy(blob, p, size);
    h1_init(44100);
    CHECK(st.midi[0] == 0 && st.midi[1] == 0 && st.midi[2] == 0 && st.midi[3] == 0,
          "a new h1: channel 1, SEND ALL, CLOCK OFF, BT ON");
    CHECK(h1_state_load(blob, size) == 0, "the state loads");
    CHECK(st.midi[0] == 0 && st.midi[1] == MS_OFF && st.midi[2] == MC_CLOCK && st.midi[3] == 1, "with the MIDI page");
    blob[offsetof(H1State, midi) + 0] = 0xFF;    /* any bytes there: clamped */
    blob[offsetof(H1State, midi) + 1] = 0xFF;
    blob[offsetof(H1State, midi) + 2] = 0x7F;
    blob[offsetof(H1State, midi) + 3] = 0x80;
    h1_init(44100);
    CHECK(h1_state_load(blob, size) == 0, "a state with odd bytes there loads");
    CHECK(st.midi[0] <= 15 && st.midi[1] <= MS_OFF && st.midi[2] <= MC_START && st.midi[3] <= 1, "clamped");
    memset(blob + offsetof(H1State, midi), 0, 4); /* as a 0.2.2 FM-1 saved it */
    h1_init(44100);
    CHECK(h1_state_load(blob, size) == 0, "a 0.2.2 state loads");
    CHECK(st.midi[0] == 0 && st.midi[1] == MS_ALL && st.midi[2] == MC_OFF && h1_bluetooth(),
          "as h1's MIDI was: channel 1, everything out, no clock, Bluetooth on");

    printf("midi: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
