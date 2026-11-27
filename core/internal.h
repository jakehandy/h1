/* h1 - what the core's files share. Not for ports. */
#pragma once
#include <stddef.h>
#include "h1.h"
#include "mathx.h"
#include "gfx.h"

#define NVOICE 8
#define NSTEP 64                 /* the step sequencer's pattern, at most */
#define NSNOTE 8                 /* notes a step holds, at most */
#define NINST 16
#define NALGO 9                  /* algorithms an instrument may have: eight each, and LAVA a ninth (HOOVER) */
#define NLAYER 16                /* recorded layers, each with its own settings */
#define NLEV 2048                /* the loop's events, a note-on and a note-off a note, across every layer */
#define CTRL 32                  /* frames per control block: envelopes, LFO, coefficients */
#define KS_LEN 1024              /* a string's delay line: down to 43 Hz at 44.1 kHz */
#define SCOPE_N 512

/* big zeroed buffers: the device puts them in its .pool RAM (fm1-base PLAT_POOL). The host simulator
 * build has H1_DEVICE too, and on macOS a section name needs a segment ("__DATA,...") */
#if defined(H1_DEVICE) && defined(__APPLE__)
#define H1_POOL __attribute__((section("__DATA,__pool")))
#elif defined(H1_DEVICE)
#define H1_POOL __attribute__((section(".pool")))
#else
#define H1_POOL
#endif

/* ---- voices --------------------------------------------------------------------------- */
typedef struct SmpR {            /* where a voice is in a recording (smp.h) */
    uint32_t frac;               /* how far between two stored samples, in 1/65536 */
    uint32_t idx;                /* the next stored sample to take up */
    f32 w[4];                    /* the last four taken up */
} SmpR;

typedef struct Voice {
    uint8_t stage;               /* 0 off, 1 attack, 2 decay, 3 sustain, 4 release */
    uint8_t gate;
    uint8_t note;
    uint8_t idx;
    uint8_t part;                /* the instrument and algorithm it sounds with (PART), 0 = never used */
    uint8_t seq;                 /* 1: the sequencer started it (LAYERS), and the sequencer lets it go */
    uint8_t lay;                 /* the recorded layer it sounds as (1..NLAYER, with its own settings), 0 = none */
    uint8_t vel;                 /* the note's velocity, 1..127 (the panel's keys: 127) */
    uint8_t declick;             /* 1: it was taken over while it still sounded (voice_on_part) */
    uint32_t serial;             /* counts note-ons: the picture sees a new note when it changes */
    uint32_t stamp;              /* allocation order */
    f32 pitch;                   /* MIDI note, gliding, before bends */
    f32 target;
    f32 env, env_prev;
    f32 age;                     /* seconds since the note began */
    f32 pan;                     /* -1..1 */
    f32 r1, r2;                  /* two random numbers per note */
    f32 ph[6];                   /* oscillator phases, in turns */
    f32 s[14];                   /* the instrument's own state */
    uint32_t u[3];
    uint32_t q[4];
    SmpR r[2];
    f32 vamp, vk, vlp[2];        /* velocity: the gain, the low-pass's coefficient (1: open) and its two poles */
    f32 last, tail;              /* the last sample it put in the mix (gain in, before the pan), and what is
                                  * left of a jump when it was taken over, dying away */
} Voice;

/* ---- saved parameters ----------------------------------------------------------------- */
typedef struct InstParams {
    uint8_t knob0[4];            /* 0..100. In an InstDef: where the four home knobs start, on every algorithm.
                                  * In a saved state they are what the knobs were before version 6, when the
                                  * algorithms of an instrument shared them; H1State.knob holds them now. */
    uint8_t algo;
    uint8_t env[4];              /* attack, decay, sustain, release: 0..100 */
    uint8_t lfo[4];              /* shape, rate, depth, target */
    uint8_t fx[4];               /* echo, time, space, tone */
    uint8_t arp[4];              /* mode, rate, range, gate */
    uint8_t mix[3];              /* [IP_CUT] 0..100: how far the instrument is turned down. Its volume is 100 less
                                  * that, kept this way round so a state saved before there was one (a zero here)
                                  * is at full. The other two: spare, zero. */
} InstParams;
#define IP_CUT 0

#define H1_MAGIC 0x31483148u     /* "H1H1" */
#define H1_VERSION 9u
/* Version 9 has room for NSLOT instruments of NALGO algorithms, and says in ninst how many it was saved
 * with: the slots from there on are taken as new when it is loaded, so instruments can be added without a
 * new version. Versions 4, 5, 6 and 8 are of fourteen instruments of eight algorithms (H1Old, below: only
 * its offsets are used), their parts packed in bits (PART_V8); 7 was never released. Version 8 is H1Old
 * and no more. Version 6 is it up to the steps' velocities: those, the steps' layers and the layers came
 * with version 8. Versions 4 and 5 are the same up to steps: there a step held four notes, not eight
 * (steps[NSTEP][4], then in version 5 step_part[NSTEP][4]), and there was no knob table: an instrument's
 * algorithms shared InstParams.knob0. h1_state_load reads all of them. */
#define NSLOT 16                 /* instruments the saved state has room for */
#define H1_OLD_NINST 14u
#define H1_OLD_NALGO 8u
#define H1_OLD_SNOTE 4u
#define H1_VERSION_4_SIZE ((offsetof(H1Old, steps) + NSTEP * H1_OLD_SNOTE + 3u) & ~3u)
#define H1_VERSION_5_SIZE ((offsetof(H1Old, steps) + NSTEP * H1_OLD_SNOTE * 2u + 3u) & ~3u)
#define H1_VERSION_6_SIZE ((offsetof(H1Old, step_vel) + 3u) & ~3u)
#define H1_VERSION_8_SIZE sizeof(H1Old)

/* A recorded layer: what was played in one take on one part (an instrument and its algorithm), and that
 * part's settings as they stood when the take ended: its four knobs, ENV and LFO. While the take is
 * open it follows the instrument as it is played; once it is closed it sounds as it was left, whatever
 * is done to the instrument after. part 0: the slot is free. */
typedef struct Layer {
    uint8_t part;
    uint8_t open;                /* 1 while its take goes on (never 1 in a saved state that is loaded) */
    uint8_t knob[4];
    uint8_t env[4];
    uint8_t lfo[4];
    uint8_t rsv[2];              /* [0] how far it is turned down, 0..100 (0: full); [1] LAY_MUTE, LAY_SOLO (0.3.1) */
} Layer;
/* An FM index by the note's velocity: the full index at 127 (every key of the panel), 0.3 of it at the softest */
#define VEL_FM(v) (0.3f + 0.7f * (f32)(v)->vel * (1.0f / 127.0f))
#define LAY_MUTE 1u
#define LAY_SOLO 2u

typedef struct H1Old {           /* what versions 4 to 8 saved: only its offsets are used, nothing is read through it */
    uint32_t magic;
    uint8_t version, inst;
    int8_t octave;
    uint8_t hud, keys[4], glo[4], seq[8];
    InstParams ip[H1_OLD_NINST];
    uint8_t ink[H1_OLD_NINST][3];
    uint8_t rsv[5];
    uint8_t steps[NSTEP][NSNOTE];
    uint8_t step_part[NSTEP][NSNOTE];
    uint8_t knob[H1_OLD_NINST][H1_OLD_NALGO][4];
    uint8_t step_vel[NSTEP][NSNOTE];
    uint8_t step_lay[NSTEP][NSNOTE];
    Layer layer[NLAYER];
} H1Old;

typedef struct H1State {
    uint32_t magic;
    uint8_t version;
    uint8_t inst;
    int8_t octave;               /* -2..2 */
    uint8_t hud;                 /* the knob strip pinned on the home screen */
    uint8_t keys[4];             /* root, scale, glide, hold */
    uint8_t glo[4];              /* tempo, swing, tune, bright */
    uint8_t seq[8];              /* mode (loop, step), bars, meter, loop snap, step grid, click, layers */
    InstParams ip[NSLOT];        /* (NSLOT: room for instruments to come, see H1_VERSION) */
    uint8_t ink[NSLOT][3];
    uint8_t ninst;               /* the instruments it was saved with */
    uint8_t rsv[4];
    uint8_t steps[NSTEP][NSNOTE];       /* the step pattern: up to eight notes a step, note + 1, 0 = none */
    uint8_t step_part[NSTEP][NSNOTE];   /* the part each of those notes was played on (PART), 0 = none kept */
    uint8_t knob[NSLOT][NALGO][4];      /* 0..100: the four live modulations of the home screen, each algorithm its own */
    uint8_t step_vel[NSTEP][NSNOTE];    /* each step note's velocity, 0 = 127 (version 8) */
    uint8_t step_lay[NSTEP][NSNOTE];    /* each step note's layer, 0 = none (it follows its part as it is now) */
    Layer layer[NLAYER];
} H1State;

/* the four home knobs of an instrument's algorithm, and of the algorithm it is on */
#define KNOBS(inst, algo) (st.knob[inst][(uint32_t)(algo) < NALGO ? (uint32_t)(algo) : 0u])
#define KNOBS_NOW(inst) KNOBS(inst, st.ip[inst].algo)
void knobs_default(int inst, int algo);          /* h1.c: one algorithm's knobs as new; algo < 0: all of the instrument's */

/* an instrument's own volume, 0..100 (EDIT held, and SELECT): what the mix takes of it, beside MASTER */
#define INST_VOL(inst) (100 - (st.ip[inst].mix[IP_CUT] <= 100 ? st.ip[inst].mix[IP_CUT] : 0))

/* A part: an instrument and one of its algorithms, in a byte. What a recorded note remembers, so that
 * with LAYERS on it goes on sounding as it was played when another instrument is in hand. Counted from
 * 1 (0 is none): 1 + inst * NALGO + algo, so sixteen instruments of nine fit. Until version 9 (and in a
 * loop saved as version 1) it was 0x80 | inst << 3 | algo: PART_V8 reads that. */
#define PART(inst, algo) ((uint8_t)(1u + (uint32_t)(inst) * NALGO + ((uint32_t)(algo) < NALGO ? (uint32_t)(algo) : 0u)))
#define PART_SOME(p) ((uint32_t)(p) - 1u < (uint32_t)(NINST * NALGO))
#define PART_INST(p) (PART_SOME(p) ? (int)(((uint32_t)(p) - 1u) / NALGO) : 0)
#define PART_ALGO(p) (PART_SOME(p) ? (int)(((uint32_t)(p) - 1u) % NALGO) : 0)
#define PART_V8(p) ((uint8_t)(((p) & 0x80u) && (((p) >> 3) & 15u) < H1_OLD_NINST ? PART(((p) >> 3) & 15u, (p) & 7u) : 0u))

extern H1State st;

#ifdef __clang__
#define SMALL __attribute__((minsize))           /* a picture's own code built small: its time goes in the primitives */
#define APART __attribute__((minsize, noinline)) /* and kept out of a caller that is built for speed */
#else
#define SMALL
#define APART
#endif

/* ---- what an instrument's DSP gets every control block -------------------------------- */
typedef struct Blk {
    f32 k[4];                    /* the four knobs, 0..1: smoothed, with the LFO on its target */
    int algo;
    f32 sr, isr;
    f32 lfo;                     /* -1..1 */
    f32 bend;                    /* semitones added to every voice (tune, LFO on pitch) */
    f32 time;                    /* seconds since boot */
    f32 bpm;
    f32 step;                    /* seconds the instrument's own clocks move on in this call: a control block's, or
                                  * 0 when its block() is called again in the same block for another part */
} Blk;

/* ---- what an instrument's picture gets every frame ------------------------------------ */
typedef struct VVoice {
    uint8_t on, gate, note;
    uint8_t vel;                 /* 1..127 */
    uint8_t seq;                 /* the sequencer played it, not a hand */
    uint32_t serial;
    f32 pitch, env, age;
} VVoice;

typedef struct Vis {
    f32 t, dt;                   /* seconds; dt is clamped */
    f32 k[4];                    /* the knobs as heard (with the LFO), eased for the eye */
    int algo, algo_prev;
    f32 algo_t;                  /* 0..1 across an algorithm change */
    f32 enter;                   /* seconds since this instrument came up */
    int fresh;                   /* 1 on the first frame after it came up */
    VVoice v[NVOICE];
    f32 level;                   /* output level, 0..1 */
    f32 lfo;
    f32 beat;                    /* 0..1 within the beat */
    uint32_t notes;              /* note-ons so far, all voices */
    f32 last_pitch;              /* of the newest note */
    uint32_t hand;               /* the note-ons of those that were played: the keys, MIDI in, the arpeggiator */
    f32 hand_pitch;              /* (what is hidden in a picture answers to these, not to a loop going round) */
    const f32 *scope;            /* the last SCOPE_N mono samples, scope_pos = the oldest */
    int scope_pos;
    uint32_t *rng;
} Vis;

typedef struct InstDef {
    const char *name;
    uint8_t ink[3];
    const char *knob[4];
    const char *algo[NALGO];
    InstParams def;
    f32 gain;
    void (*block)(const Blk *b);
    void (*note)(Voice *v, const Blk *b);
    void (*render)(Voice *v, const Blk *b, f32 *out, int n);
    void (*post)(const Blk *b, f32 *l, f32 *r, int n);
    void (*draw)(const Vis *vis);
} InstDef;

extern const InstDef inst_pip, inst_bonk, inst_urchin, inst_twang, inst_lava, inst_knot, inst_ink, inst_tide, inst_haunt,
                     inst_bard, inst_turbo, inst_orbit, inst_cozy, inst_koi, inst_tilt, inst_plunk;
extern const InstDef *const insts[NINST];
/* how many algorithms an instrument has: eight, or nine where it names a ninth */
#define NALGOS(i) (insts[i]->algo[NALGO - 1] ? NALGO : NALGO - 1)
/* a part that names an instrument and one of the algorithms it has */
static inline int part_ok(uint32_t p) { return PART_SOME(p) && PART_ALGO(p) < NALGOS(PART_INST(p)); }

/* ---- settings pages ------------------------------------------------------------------- */
enum { PG_HOME, PG_FX, PG_KEYS, PG_ENV, PG_LFO, PG_GLO, PG_ARP, PG_SEQ, PG_MIDI, PG_SETUP, NPAGE };
#define PG_BUTTONS PG_MIDI       /* the pages from here on have no button: SELECT goes on to them from SEQ */

/* The MIDI and SETUP pages' settings (0.3.1): each 0 as it was before them. Saved packed in H1State.rsv, so
 * the state stays version 9 and a 0.3.0 reads it (and leaves them off). */
enum { OPT_CHAN, OPT_BEND, OPT_WHEEL, OPT_PEDAL, OPT_ANIM, OPT_EGGS, OPT_SCREEN, OPT_LEDS, NOPT };
extern uint8_t opt[NOPT];
void opt_unpack(void);
void opt_pack(void);
#define EGGS_ON (!opt[OPT_EGGS])
enum { PK_PCT, PK_ENUM, PK_INT };

typedef struct ParamDef {
    const char *name;
    uint8_t kind;
    uint8_t max;                 /* PCT: 100. ENUM: options - 1. INT: max */
    uint8_t min;
    int8_t show_off;             /* INT: added to the stored value when shown */
    const char *const *opts;
} ParamDef;

typedef struct PageDef {
    const char *title;
    ParamDef p[4];
} PageDef;

extern const PageDef pages[NPAGE];
const ParamDef *page_param(int page, int i);     /* (the SEQ page's last column depends on its mode) */
uint8_t *page_value(int page, int i);

/* ---- engine (synth.c): everything below runs in the audio interrupt, except where noted - */
enum { EV_NOTE_ON = 1, EV_NOTE_OFF, EV_PANIC, EV_INST, EV_UNLATCH, EV_REC, EV_PLAY,
       EV_CLEAR,                 /* a = 0: everything recorded; a = 1 + an instrument: only what was played on it */
       EV_STEP_MOVE,             /* the step cursor: a = detents + 128 */
       EV_STEP_REST,             /* empty the step under the cursor and move on */
       EV_SEQ_MODE,              /* loop <-> step: stop whichever was running */
       EV_CUE,                   /* a: one of the cues below */
       EV_SUSTAIN,               /* MIDI's sustain pedal: a = 1 down, 0 up */
       EV_CLEAR_LAY };           /* a = 1..NLAYER: what that layer holds, and the layer */
enum { LOOP_EMPTY, LOOP_IDLE, LOOP_ARMED, LOOP_REC, LOOP_PLAY, LOOP_DUB };
enum { SEQ_LOOP, SEQ_STEP };
enum { CLICK_REC, CLICK_ON, CLICK_OFF };       /* the metronome: while recording, always, never */

/* what the screen needs to know about the sequencer */
typedef struct SeqInfo {
    int mode;                    /* SEQ_LOOP or SEQ_STEP */
    int loop_state;
    f32 loop_pos;                /* 0..1 */
    int playing, write;          /* step mode */
    int nsteps, per_bar, bars;
    int cursor, at;              /* the step being edited, the step sounding */
} SeqInfo;
void eng_seq_info(SeqInfo *si);
int seq_bar_ticks(void);                         /* 96 ticks to the quarter note */
int seq_grid_ticks(void);
int seq_accent(int tick_in_bar);                 /* 2 the bar's first beat, 1 another beat, 0 between */

/* A cue: the short noise of something hidden in a picture (TILT's buzzer when the table tilts, and the
 * others' below). The picture asks for it from the main loop; it is mixed in where the metronome is,
 * after the effects. It is not a note: nothing of it is recorded, and nothing goes out over MIDI.
 *
 * What is hidden, and what brings it out:
 *   TILT   hammer the table until it tilts                       the buzzer
 *   PIP    a run of eight notes, each higher than the last       one up
 *   TURBO  play fast enough for long enough                      a siren, and what is in the mirror
 *   ORBIT  hold one note, alone, for five seconds                something answers
 *   COZY   the same note over and over walks the pot off the sill
 *   KOI    five notes down at once: a stone in the pond          the frog
 *   HAUNT  hold a tritone                                        thunder, and what was behind the dark
 * Only what is played counts (the keys, MIDI in, the arpeggiator): a loop or a pattern going round sets
 * nothing off. And each goes off once, and is ready again when the playing has stopped for a moment or
 * the keys are let go, so the arpeggiator does not set it off every time round either. */
enum { CUE_TILT, CUE_PIP, CUE_TURBO, CUE_ORBIT, CUE_COZY, CUE_KOI, CUE_HAUNT, NCUE };
#define eng_cue(i) eng_post(EV_CUE, (i))
uint32_t eng_cues(void);                         /* cues started so far (tests/eggs.c) */

void eng_init(uint32_t sample_rate);
void eng_post(int type, int a);                  /* main loop -> audio, lock-free */
void eng_post_note(int on, int note, int vel, int from_midi);   /* a note, with its velocity (1..127) and whether it
                                                  * came in over MIDI (then it is not sent out again) */
/* MIDI out (h1_midi_out): the engine calls it from inside the render, the UI from the main loop */
extern void (*eng_midi_fn)(uint8_t status, uint8_t d1, uint8_t d2);
/* the loop, saved apart from the state (h1_loop): main loop */
const uint8_t *eng_loop_blob(uint32_t *size);    /* the loop as saved, in place (h1_loop) */
int eng_loop_load(const uint8_t *b, uint32_t n);
uint32_t eng_loop_gen(void);                     /* changes whenever what is recorded in the loop changes */
int eng_loop_taking(void);                       /* 1: a take is going into the loop (it is changing) */
int eng_loop_quiet(void);                        /* 1: nothing is recording or playing (the loop may be written) */
int eng_layer_count(void);                       /* main loop: layers the recorded notes are on (LAYERS) */
int eng_layer_rec(void);                         /* main loop: the layer a take is going into, 0: none */
int eng_loop_used(void);                         /* main loop: the loop's events (a note is two), of NLEV */
uint32_t eng_loop_full(void);                    /* main loop: moves each time a full loop turns a note away */
void eng_render(int32_t *lr, uint32_t n);
void eng_stems(int16_t *(*get)(uint32_t off, uint32_t *run), uint32_t stride);
void eng_snapshot(Vis *vis);                     /* main loop: copy what the picture needs */
uint32_t eng_sounding_lo(void);                  /* notes sounding, as key bits for the LEDs */
int eng_seq_leds(uint32_t *out, int page_open, int blink);   /* the pattern on the white keys; 0: not showing */
uint32_t eng_voices(void);
void eng_mout3(uint8_t status, uint8_t d1, uint8_t d2);   /* MIDI out, on MIDI's CHAN */
int eng_silent(void);
void eng_load(int pct);
int eng_vcap(void);
extern volatile f32 eng_bend, eng_wheel;         /* MIDI in: the pitch bend in semitones, the mod wheel 0..1 */
void eng_two_cores(int (*run)(void (*fn)(uint32_t), uint32_t arg), int (*wait)(void));
int eng_seq_has(int inst);                       /* main loop: something recorded was played on this instrument */
extern Voice voices[NVOICE];
extern f32 ks_line[NVOICE][2][KS_LEN];
extern uint32_t eng_rng;

/* scale helper shared by UI and engine */
int scale_snap(int note);

/* ui.c */
void ui_init(void);
void ui_frame(uint32_t now_ms);
int ui_page(void);
