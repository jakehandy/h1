/* h1 - the engine: voices, envelopes, LFO, arpeggiator, the sequencer (a loop recorder and a step
 * sequencer, one at a time) and the block loop that
 * calls the current instrument. Runs in the audio interrupt; the main loop talks to it through a
 * small lock-free queue (eng_post) and reads back plain floats for the picture. */
#include "internal.h"
#include "dsp.h"

void fx_init(f32 sr);
void fx_block(f32 *l, f32 *r, const InstParams *ip, f32 bpm);

f32 sin_tab[SIN_N + 1];
H1State st;
Voice voices[NVOICE];
f32 ks_line[NVOICE][2][KS_LEN] H1_POOL;
uint32_t eng_rng = 0x2F6E2B1u;

const InstDef *const insts[NINST] = {&inst_cozy, &inst_orbit, &inst_koi, &inst_haunt, &inst_tide,
                                     &inst_pip, &inst_bonk, &inst_urchin, &inst_twang, &inst_lava,
                                     &inst_knot, &inst_ink, &inst_bard, &inst_turbo, &inst_tilt, &inst_plunk, &inst_blab};

/* Each algorithm against the rest, so that turning the encoder through the instruments is not a ride on
 * the volume: measured on the same phrase played on every one of them (the order is insts[]'s). */
static const f32 algo_gain[NINST][NALGO] = {           /* (0: an algorithm the instrument does not have) */
    /* COZY   */ {1.334f, 1.413f, 1.000f, 0.794f, 1.334f, 1.189f, 1.334f, 1.334f, 0.000f},
    /* ORBIT  */ {1.122f, 0.794f, 0.794f, 0.794f, 0.794f, 0.668f, 0.668f, 0.750f, 0.000f},
    /* KOI    */ {1.000f, 1.000f, 1.189f, 1.189f, 1.122f, 1.000f, 0.750f, 1.259f, 0.000f},
    /* HAUNT  */ {0.794f, 0.841f, 0.750f, 1.679f, 0.668f, 1.413f, 1.000f, 1.000f, 0.000f},
    /* TIDE   */ {1.000f, 1.000f, 1.334f, 1.259f, 0.891f, 1.334f, 1.259f, 0.841f, 0.000f},
    /* PIP    */ {0.841f, 1.000f, 1.000f, 1.000f, 0.841f, 1.585f, 1.000f, 1.000f, 0.000f},
    /* BONK   */ {0.794f, 1.000f, 1.000f, 1.000f, 0.668f, 0.794f, 0.708f, 1.000f, 0.000f},
    /* URCHIN */ {1.000f, 1.122f, 0.891f, 1.122f, 1.413f, 1.189f, 1.189f, 1.000f, 0.000f},
    /* TWANG  */ {1.000f, 1.189f, 0.841f, 0.750f, 1.000f, 1.000f, 1.000f, 1.000f, 0.000f},
    /* LAVA   */ {0.841f, 0.794f, 1.000f, 0.891f, 1.000f, 0.891f, 1.496f, 1.000f, 1.426f},
    /* KNOT   */ {1.000f, 0.891f, 1.000f, 0.668f, 0.841f, 1.259f, 0.841f, 0.841f, 0.000f},
    /* INK    */ {1.122f, 0.750f, 1.000f, 1.000f, 1.000f, 1.000f, 1.000f, 0.841f, 0.000f},
    /* BARD   */ {1.000f, 1.000f, 1.000f, 1.000f, 1.189f, 1.122f, 1.334f, 1.189f, 0.000f},
    /* TURBO  */ {1.189f, 1.000f, 1.000f, 1.189f, 1.585f, 1.000f, 1.259f, 1.000f, 0.000f},
    /* TILT   */ {0.750f, 0.668f, 0.750f, 0.750f, 1.059f, 1.000f, 1.189f, 0.447f, 0.000f},
    /* PLUNK  */ {1.000f, 1.189f, 1.000f, 1.122f, 1.000f, 0.891f, 1.334f, 1.122f, 0.000f},
    /* BLAB   */ {0.841f, 1.122f, 0.891f, 1.000f, 1.122f, 1.000f, 0.944f, 1.000f, 0.000f},
};

static f32 SR = 44100.0f, ISR = 1.0f / 44100.0f;
static const InstDef *cur;
static int cur_i;
static volatile int pending_i = -1;
static int fade_blocks;
static Blk blk;                                  /* the numbers of the instrument in hand, for the picture */

/* What the engine keeps for each instrument while it sounds. With LAYERS on, notes that were recorded on
 * other instruments go on sounding beside the one in hand, so every one of them needs its own eased
 * knobs, LFO and envelope rates. */
typedef struct Lfo { f32 ph, sh, drift; } Lfo;
typedef struct Mod {                             /* what the LFO and ENV pages make of a control block */
    f32 kmod;                                    /* what the LFO adds to a knob, and which (-1: none) */
    int kmod_k;
    f32 vol, panmod, bend, lfo;
    f32 att, kd, sus, kr;                        /* the envelope's rates */
} Mod;
typedef struct IRun {
    Blk b;                                       /* its numbers for this control block */
    f32 knob_s[4];                               /* the knobs of the algorithm in hand, eased */
    Mod m;
    Lfo lf;                                      /* its own LFO, while it is not the one in hand */
    f32 ig;                                      /* its own volume (INST_VOL) as a gain, eased: what the mix takes of it */
    f32 last_pitch;
    uint32_t at;                                 /* the control block b is for */
    uint32_t adv;                                /* the control block its own clocks last moved in */
    uint16_t linger;                             /* control blocks its post still runs after its last voice */
    uint8_t owner;                               /* the part its block() was last called for */
} IRun;
#define LINGER 400                               /* 0.3 s */
static IRun ir[NINST];
/* a closed layer's numbers: its own LFO, envelope and knobs, as they were left (lay_set) */
typedef struct LRun { Blk b; Mod m; Lfo lf; uint32_t at; f32 g; } LRun;
static LRun lrun[NLAYER];
static uint32_t blkno = 16;
static Lfo lfo_live;                             /* the LFO of the instrument in hand: one clock across instruments */
static uint32_t lfo_rng = 0x1F0C1F0Cu;
static int lay_was;
static f32 run_time;
static uint32_t stamp_ctr;
static f32 last_pitch = 60.0f;
static volatile uint32_t note_count, hand_count;
static volatile f32 hand_pitch;
static volatile f32 level_out;
static volatile f32 lfo_out;
static volatile f32 beat_out;

static f32 scope[SCOPE_N];
static volatile uint32_t scope_w;

/* ---- the queue from the main loop ------------------------------------------------------ */
static volatile uint32_t evq[64];
static volatile uint32_t ev_w, ev_r;

static void post(uint32_t e)
{
    uint32_t w = ev_w, nx = (w + 1u) & 63u;
    if (nx == ev_r)
        return;                                  /* full: drop (64 events inside one block) */
    evq[w] = e;
    ev_w = nx;
}
void eng_post(int type, int a) { post(((uint32_t)type << 8) | ((uint32_t)a & 255u)); }
/* bits 16..22 the velocity, bit 23 "it came in over MIDI" */
void eng_post_note(int on, int note, int vel, int from_midi)
{
    post(((uint32_t)(on ? EV_NOTE_ON : EV_NOTE_OFF) << 8) | ((uint32_t)note & 127u) |
         ((uint32_t)iclamp(vel, 1, 127) << 16) | (from_midi ? 1u << 23 : 0u));
}

/* ---- MIDI out: what is played on the keys, the arpeggiator and the sequencer, on channel 1 (or MIDI's
 * CHAN). Not what came in over MIDI (a computer that sends a note in would hear it echoed back). ------ */
void (*eng_midi_fn)(uint8_t status, uint8_t d1, uint8_t d2);
void eng_mout3(uint8_t status, uint8_t d1, uint8_t d2)
{
    void (*fn)(uint8_t, uint8_t, uint8_t) = eng_midi_fn;
    if (fn)
        fn((uint8_t)(status | (opt[OPT_CHAN] ? (opt[OPT_CHAN] - 1u) & 15u : 0u)), d1, d2);
}
static void mout(int on, int note, int vel)
{
    if (note >= 0 && note < 128)
        eng_mout3(on ? 0x90u : 0x80u, (uint8_t)note, (uint8_t)(on ? iclamp(vel, 1, 127) : 0));
}

void dsp_init(void)
{
    int i;
    for (i = 0; i <= SIN_N; i++)
        sin_tab[i] = sin_turns((f32)i / (f32)SIN_N);
}

/* ---- scales --------------------------------------------------------------------------- */
static const uint16_t SCALE_MASK[] = {
    0xFFF,      /* CHROMA */
    0xAB5,      /* MAJOR   1 0 1 0 1 1 0 1 0 1 0 1 */
    0x5AD,      /* MINOR   1 0 1 1 0 1 0 1 1 0 1 0 */
    0x295,      /* PENTA   major pentatonic */
    0x4A9,      /* MINPENT minor pentatonic */
    0x4E9,      /* BLUES */
    0x6AD,      /* DORIAN */
    0x555,      /* WHOLE */
};

int scale_snap(int note)
{
    uint32_t mask = SCALE_MASK[st.keys[1] < 8 ? st.keys[1] : 0];
    int root = st.keys[0] % 12, i;
    for (i = 0; i < 12; i++) {
        int pc = ((note - i - root) % 12 + 12) % 12;
        if (mask & (1u << pc))
            return note - i;
    }
    return note;
}

/* ---- voices --------------------------------------------------------------------------- */
static int layers(void) { return st.seq[6] == 1; }
static uint8_t live_part(void) { return PART(cur_i, st.ip[cur_i].algo < NALGOS(cur_i) ? st.ip[cur_i].algo : 0); }
static const Blk *part_set(int part);

/* Velocity: louder, and brighter. The gain is a curve with about 20 dB between the softest note and the
 * hardest; the brightness is a low-pass on the voice (two poles, 12 dB an octave), wide open at 127, near
 * 6 kHz at 100, 2 kHz at 64 and 400 Hz at 1. The panel's keys play at 127, so they sound as they always
 * have, to the bit. */
static void vel_set(Voice *v, int vel)
{
    f32 x, fc;
    vel = iclamp(vel, 1, 127);
    v->vel = (uint8_t)vel;
    if (vel >= 127) {
        v->vamp = 1.0f;
        v->vk = 1.0f;
        return;
    }
    x = (f32)vel * (1.0f / 127.0f);
    v->vamp = 0.1f + 0.9f * x * x;
    fc = 12000.0f * exp2fx((x - 1.0f) * 5.0f);
    v->vk = 1.0f - exp2fx(-H1_TAU * fc * ISR * 1.442695f);
}

/* A note starts, on a part: an instrument and one of its algorithms. A voice that last sounded another
 * instrument is emptied first: what an instrument keeps in a voice means nothing to the next. */
static const Blk *part_set(int part);
static const Blk *lay_set(int part, int lay);
/* The CPU guard: the port says how much of each block's time the sound took (h1_load). Kept over ~88 % for
 * a moment, fewer voices may sound at once (a new note takes over the oldest, and what is letting go dies
 * away quicker); back under 60 %, they come back one at a time. Four instruments layered on a loop could
 * take more than the block and crackle (#66). Without a port that tells it, it never steps in. */
volatile f32 eng_bend, eng_wheel;
static volatile int vcap = NVOICE;
static int fast_rel;
static f32 load_s;
static uint32_t load_t;
void eng_load(int pct)
{
    load_s += ((f32)iclamp(pct, 0, 400) - load_s) * 0.05f;
    if (++load_t < 48u)
        return;
    load_t = 0;
    if (load_s > 88.0f && vcap > 3)
        vcap--;
    else if (load_s < 60.0f && vcap < NVOICE)
        vcap++;
}
int eng_vcap(void) { return vcap; }
static int voice_key(const Voice *v);
static void voice_on_part(int note, int part, int seq, int lay, int vel)
{
    Voice *v = 0;
    const Blk *b;
    f32 was;
    int i, j, g, inst = PART_INST(part);
    if (note < 0 || note > 127)
        return;
    for (i = 0; i < NVOICE && !v; i++)
        if (voices[i].stage && voices[i].gate && voices[i].note == note && voices[i].part == part)
            v = &voices[i];
    for (i = j = g = 0; i < NVOICE; i++) {
        j += voices[i].stage != 0;
        g += voices[i].stage && voices[i].gate;
    }
    for (i = 0; i < NVOICE && !v && j < vcap; i++)     /* (past the CPU guard's cap: one is taken over instead) */
        if (!voices[i].stage)
            v = &voices[i];
    if (!v && g < vcap) {                        /* (and with as many held as the cap, the oldest held one) */
        uint32_t best = 0xFFFFFFFFu;
        for (i = 0; i < NVOICE; i++)
            if (voices[i].stage && !voices[i].gate && voices[i].stamp < best) {
                best = voices[i].stamp;
                v = &voices[i];
            }
    }
    if (!v) {
        uint32_t best = 0xFFFFFFFFu;
        for (i = 0; i < NVOICE; i++)
            if (voices[i].stage && voices[i].stamp < best) {
                best = voices[i].stamp;
                v = &voices[i];
            }
    }
    /* Taken over while it still sounds: what it was last putting out is kept, and the new note starts from
     * there and lets go of the difference over a millisecond or two (the mix, in block()). Without that a
     * stolen voice jumps, and a fast run on eight voices crackles. */
    was = v->stage ? v->last : 0.0f;
    if (v->part && PART_INST(v->part) != inst) {
        uint32_t serial = v->serial;
        uint8_t idx = v->idx, *p = (uint8_t *)v;
        for (j = 0; j < (int)sizeof(Voice); j++)
            p[j] = 0;
        v->serial = serial;
        v->idx = idx;
        for (j = 0; j < KS_LEN; j++)
            ks_line[idx][0][j] = ks_line[idx][1][j] = 0.0f;
    }
    v->last = was;
    v->declick = was != 0.0f;
    v->part = (uint8_t)part;
    v->seq = (uint8_t)seq;
    v->lay = (uint8_t)lay;
    vel_set(v, vel);
    v->target = (f32)note;
    if (inst == cur_i) {
        v->pitch = st.keys[2] ? last_pitch : (f32)note;
        last_pitch = (f32)note;
    } else                                       /* (a glide is from the instrument's own last note) */
        v->pitch = st.keys[2] && ir[inst].last_pitch > 0.0f ? ir[inst].last_pitch : (f32)note;
    ir[inst].last_pitch = (f32)note;
    v->note = (uint8_t)note;
    v->gate = 1;
    v->stage = 1;
    v->age = 0.0f;
    v->stamp = ++stamp_ctr;
    v->pan = clampf(((f32)note - 64.0f) * (1.0f / 40.0f), -0.45f, 0.45f);
    v->r1 = rng_f(&eng_rng);
    v->r2 = rng_f(&eng_rng);
    /* (a closed layer's note starts on the layer's own settings, as it then sounds: an instrument that reads
     * its knobs at the note, as TILT's BOUNCE does, would take the hand's otherwise, #52) */
    b = voice_key(v) >> 8 ? lay_set(part, lay) : part_set(part);
    insts[inst]->note(v, b);
    v->serial++;
    if (inst == cur_i) {
        note_count++;                            /* (the picture's count: the notes of the instrument it shows) */
        if (!seq) {
            hand_pitch = (f32)note;
            hand_count++;
        }
    }
}

static void voice_off_part(int note, int part, int lay)   /* the sequencer lets go of a note it started */
{
    int i;
    for (i = 0; i < NVOICE; i++)
        if (voices[i].stage && voices[i].gate && voices[i].seq && voices[i].note == note && voices[i].part == part &&
            voices[i].lay == lay) {
            voices[i].gate = 0;
            voices[i].stage = 4;
        }
}

static void voice_on(int note, int vel) { voice_on_part(note, live_part(), 0, 0, vel); }

/* a note played by hand, or by the arpeggiator, ends: whatever algorithm it began on */
static void voice_off(int note)
{
    int i;
    for (i = 0; i < NVOICE; i++)
        if (voices[i].stage && voices[i].gate && !voices[i].seq && voices[i].note == note && PART_INST(voices[i].part) == cur_i) {
            voices[i].gate = 0;
            voices[i].stage = 4;
        }
}

static void voices_release_all(void)
{
    int i;
    for (i = 0; i < NVOICE; i++)
        if (voices[i].stage) {
            voices[i].gate = 0;
            voices[i].stage = 4;
        }
}

/* ---- arpeggiator ---------------------------------------------------------------------- */
static uint8_t held[16], held_vel[16];
static uint32_t direct[4];        /* notes sounding from a key without the ARP (route) */
static int nheld;
static int arp_i, arp_stepn;
static f32 arp_acc = 1e9f, arp_off;
static uint8_t arp_cur[16];
static int arp_ncur;
static int arp_was;
static const f32 RATE_TICKS[6] = {96.0f, 48.0f, 32.0f, 24.0f, 16.0f, 12.0f};

static void arp_take(int note, int on, int vel);  /* (the loop recorder, below) */

static void arp_release(void)
{
    int i;
    for (i = 0; i < arp_ncur; i++) {
        voice_off(arp_cur[i]);
        mout(0, arp_cur[i], 0);
        arp_take(arp_cur[i], 0, 0);
    }
    arp_ncur = 0;
}

static void arp_play(int note, int vel)
{
    voice_on(note, vel);
    mout(1, note, vel);
    arp_take(note, 1, vel);
    arp_cur[arp_ncur++] = (uint8_t)note;
}

static void arp_fire(const InstParams *ip, f32 len)
{
    uint8_t s[16], sv[16];
    int n = nheld, i, j, range = iclamp(ip->arp[2], 1, 4), total, idx = 0, mode = ip->arp[0];
    f32 gate = (f32)ip->arp[3] * 0.01f;
    arp_release();
    if (!n)
        return;
    for (i = 0; i < n; i++) {
        s[i] = held[i];
        sv[i] = held_vel[i];
    }
    if (mode != 5)
        for (i = 1; i < n; i++) {                /* sort ascending */
            uint8_t x = s[i], xv = sv[i];
            for (j = i; j > 0 && s[j - 1] > x; j--) {
                s[j] = s[j - 1];
                sv[j] = sv[j - 1];
            }
            s[j] = x;
            sv[j] = xv;
        }
    total = n * range;
    if (mode == 6) {                             /* CHORD: everything held, octave walking */
        int oct = arp_i % range;
        for (i = 0; i < n; i++) {
            int note = s[i] + 12 * oct;
            if (note <= 127)
                arp_play(note, sv[i]);
        }
        arp_i++;
    } else {
        int note;
        switch (mode) {
        case 2: idx = total - 1 - (arp_i % total); break;
        case 3: {
            int period = total > 1 ? 2 * total - 2 : 1, p = arp_i % period;
            idx = p < total ? p : period - p;
            break;
        }
        case 4: idx = (int)(rng_next(&eng_rng) % (uint32_t)total); break;
        default: idx = arp_i % total; break;
        }
        arp_i++;
        note = s[idx % n] + 12 * (idx / n);
        if (note <= 127)
            arp_play(note, sv[idx % n]);
    }
    arp_off = gate >= 0.995f ? 1e9f : len * maxf(0.06f, gate);
}

static void unlatch(void);
static void arp_tick(const InstParams *ip, f32 dticks)
{
    f32 step, sw, len;
    if (ip->arp[0] == 0) {
        if (arp_was) {
            arp_release();
            arp_was = 0;
            nheld = 0;
            if (st.keys[3] == 2)
                unlatch();                       /* (HOLD ARP: what it held goes with the ARP) */
        }
        return;
    }
    arp_was = 1;
    if (arp_ncur) {
        arp_off -= dticks;
        if (arp_off <= 0.0f)
            arp_release();
    }
    if (!nheld) {
        arp_acc = 1e9f;
        arp_i = 0;
        return;
    }
    step = RATE_TICKS[ip->arp[1] < 6 ? ip->arp[1] : 3];
    sw = (f32)st.glo[1] * 0.01f * 0.34f;
    len = ((arp_stepn - 1) & 1) ? step * (1.0f - sw) : step * (1.0f + sw);
    arp_acc += dticks;
    if (arp_acc >= len) {
        if (arp_acc > 1e8f) {
            arp_acc = 0.0f;
            arp_stepn = 0;
        } else
            arp_acc -= len;
        arp_fire(ip, (arp_stepn & 1) ? step * (1.0f - sw) : step * (1.0f + sw));
        arp_stepn++;
    }
}

/* a note after the latch and the loop: to the arpeggiator when it runs, else to a voice (and out, unless
 * it came in over MIDI) */
static void route(int note, int on, int vel, int from_midi)
{
    int i;
    uint32_t bit = 1u << (note & 31);
    /* A note started straight (ARP off) is ended straight, whatever ARP has become since: the ARP turned on
     * while the key was down took its release into the held list, and the voice rang on (#56). */
    if (!on && ((direct[note >> 5 & 3] & bit) || st.ip[cur_i].arp[0] == 0)) {
        direct[note >> 5 & 3] &= ~bit;
        voice_off(note);
        if (!from_midi)
            mout(0, note, 0);
        if (st.ip[cur_i].arp[0] == 0)
            return;
    }
    if (st.ip[cur_i].arp[0] == 0) {
        voice_on(note, vel);
        direct[note >> 5 & 3] |= bit;
        if (!from_midi)
            mout(1, note, vel);
        return;
    }
    if (on) {
        for (i = 0; i < nheld; i++)
            if (held[i] == note) {
                held_vel[i] = (uint8_t)vel;
                return;
            }
        if (nheld < 16) {
            held_vel[nheld] = (uint8_t)vel;
            held[nheld++] = (uint8_t)note;
        }
    } else {
        for (i = 0; i < nheld; i++)
            if (held[i] == note) {
                for (; i + 1 < nheld; i++) {
                    held[i] = held[i + 1];
                    held_vel[i] = held_vel[i + 1];
                }
                nheld--;
                break;
            }
    }
}

/* ---- the sequencer's clock: bars, meters, grids -------------------------------------------- */
static const uint16_t METER_TICKS[5] = {384, 288, 480, 288, 336};      /* 4/4 3/4 5/4 6/8 7/8 */
static const uint8_t GRID_TICKS[3] = {48, 24, 12};                     /* 1/8 1/16 1/32 */

int seq_bar_ticks(void) { return METER_TICKS[st.seq[2] < 5 ? st.seq[2] : 0]; }
int seq_grid_ticks(void) { return GRID_TICKS[st.seq[4] < 3 ? st.seq[4] : 1]; }

int seq_accent(int tick)
{
    if (tick == 0)
        return 2;
    switch (st.seq[2]) {
    case 3: return tick == 144;                              /* 6/8: two groups of three */
    case 4: return tick == 96 || tick == 192;                /* 7/8: 2 + 2 + 3 */
    default: return tick % 96 == 0;
    }
}

static int step_per_bar(void) { return seq_bar_ticks() / seq_grid_ticks(); }
static int loop_bars(void) { return 1 << (st.seq[1] < 8 ? st.seq[1] : 1); }   /* 1, 2, 4 ... 128 */
static int step_bars(void)
{
    int bars = loop_bars(), pb = step_per_bar();
    while (bars > 1 && bars * pb > NSTEP)
        bars >>= 1;
    return bars;
}
static int step_count(void) { return imin(NSTEP, step_bars() * step_per_bar()); }

/* ---- loop recorder -------------------------------------------------------------------- */
/* on: bit 0 = on, bit 7 = recorded this pass; part: what it was played on; lay: the layer (0 none); vel: 1..127 */
typedef struct LEv { uint16_t tick; uint8_t note; uint8_t on; uint8_t part, lay, vel, rsv; } LEv;
/* The events are kept in order of tick (a new one goes in after those of its tick), so a block finds the
 * ones it plays by halving instead of reading them all: with 2048 of them, reading every one in every
 * block would cost the sound more than the loop itself. */
static struct { uint8_t head[12]; LEv ev[NLEV]; } loop_mem;   /* the saved loop's own bytes (eng_loop_blob) */
#define lev loop_mem.ev
static int nlev;
typedef char loop_max_is_h1s[H1_LOOP_MAX == 12u + 8u * NLEV ? 1 : -1];
typedef char lev_is_8_bytes[sizeof(LEv) == 8 && sizeof loop_mem == 12 + 8 * NLEV ? 1 : -1];
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "the loop's events are saved as they lie in RAM: little-endian only"
#endif
static volatile uint32_t loop_gen;               /* changes with what is recorded: the loop is saved when it moves */
static volatile uint32_t loop_full;              /* counts the note-ons a full loop turned away (the UI says so) */
static uint8_t rec_part[128], rec_lay[128];      /* where each note held in a take went in, for its note-off */
static uint32_t rec_open[4];                     /* notes whose note-on went in and whose note-off has room kept */
static int nopen;

static f32 take_len, take_left;                 /* a short take (take_fit): its length, what is left of it */
#define TAKE_TAG 1u                              /* LEv.rsv of a short take's notes, until it is repeated out */
static void take_close(void);
static void rec_forget(void)
{
    int i;
    rec_open[0] = rec_open[1] = rec_open[2] = rec_open[3] = 0;
    nopen = 0;
    if (take_left > 0.0f)                        /* (a short take given up: its notes stay, once, as recorded) */
        for (i = 0; i < nlev; i++)
            lev[i].rsv = 0;
    take_len = take_left = 0.0f;
}

/* the first event at or after tick t */
static int lev_find(f32 t)
{
    int lo = 0, hi = nlev;
    while (lo < hi) {
        int mid = (lo + hi) >> 1;
        if ((f32)lev[mid].tick < t)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

/* room for one more, in order: after every event of its tick or before (0: no room) */
static LEv *lev_insert(uint16_t tick)
{
    int i = nlev;
    if (nlev >= NLEV)
        return 0;
    while (i > 0 && lev[i - 1].tick > tick) {
        lev[i] = lev[i - 1];
        i--;
    }
    nlev++;
    return &lev[i];
}

/* ---- layers ----------------------------------------------------------------------------------
 * With LAYERS on, every take on a part is a layer of its own (st.layer): the part, and once the take is over
 * that part's knobs, ENV and LFO as they were. So a bass and a pad can both be COZY on the same algorithm,
 * each with its own envelope. Notes keep the layer they went in on (LEv.lay, st.step_lay). */
static void layer_snap(Layer *l)
{
    int inst = PART_INST(l->part), a = PART_ALGO(l->part), k;
    for (k = 0; k < 4; k++) {
        l->knob[k] = KNOBS(inst, a)[k];
        l->env[k] = st.ip[inst].env[k];
        l->lfo[k] = st.ip[inst].lfo[k];
    }
}

/* every open take ends: its layers keep the settings they have now */
static void layers_close(void)
{
    int i;
    for (i = 0; i < NLAYER; i++)
        if (st.layer[i].open) {
            layer_snap(&st.layer[i]);
            st.layer[i].open = 0;
        }
}

/* the layers no recorded note is on (in the loop or the steps) let go, all of their settings with them: after
 * a clear, so the layer view shows only what is there and a mute or solo left behind hushes nothing (#55, #71) */
/* a layer gone: out of SONG's parts (its slot taken again later is a new layer to the song) */
static void song_drop(int i)
{
    int j;
    songp.known &= (uint16_t)~(1u << i);
    for (j = 0; j < NSONG; j++)
        songp.mask[j] &= (uint16_t)~(1u << i);
}

static void layers_tidy(void)
{
    uint8_t used[NLAYER];
    int i, j;
    for (i = 0; i < NLAYER; i++)
        used[i] = 0;
    for (i = 0; i < nlev; i++)
        if (lev[i].lay)
            used[(lev[i].lay - 1) & (NLAYER - 1)] = 1;
    for (i = 0; i < NSTEP; i++)
        for (j = 0; j < NSNOTE; j++)
            if (st.steps[i][j] && st.step_lay[i][j])
                used[(st.step_lay[i][j] - 1) & (NLAYER - 1)] = 1;
    for (i = 0; i < NLAYER; i++)
        if (!used[i] && !st.layer[i].open) {
            uint8_t *p = (uint8_t *)&st.layer[i];
            song_drop(i);
            for (j = 0; j < (int)sizeof(Layer); j++)
                p[j] = 0;
        }
}

/* the layer what is played now goes into: the open one of the part in hand, or a new one. 0 when there is no
 * room (then the notes follow their part as it is, as they did before layers had settings) */
static APART int layer_take(void)
{
    int part = live_part(), i;
    for (i = 0; i < NLAYER; i++)
        if (st.layer[i].part == part && st.layer[i].open)
            return i + 1;
    for (i = 0; i < NLAYER && st.layer[i].part; i++)
        ;
    if (i == NLAYER) {                           /* full: the layers no recorded note is on any more are let go */
        layers_tidy();
        for (i = 0; i < NLAYER && st.layer[i].part; i++)
            ;
        if (i == NLAYER)
            return 0;
    }
    st.layer[i].part = (uint8_t)part;
    st.layer[i].open = 1;
    st.layer[i].rsv[0] = st.layer[i].rsv[1] = 0; /* (a new layer: at full, heard) */
    layer_snap(&st.layer[i]);
    return i + 1;
}

/* a recorded note's layer, if it is still the layer of that part */
static int layer_of(int lay, int part)
{
    return lay >= 1 && lay <= NLAYER && st.layer[lay - 1].part == part ? lay : 0;
}

/* The layer view (0.3.1): a layer muted, or another soloed, plays nothing (its notes are skipped, as a
 * muted track's are); its volume is a gain on its voices in the mix. Only with LAYERS on: off, every
 * note follows the hand, layers or not. */
static uint32_t lay_hushed;                      /* bit l - 1: layer l is not heard, as of this block */
SongParts songp;
static int song_cur;                             /* the part playing (not saved: a song starts at its first) */
int eng_song_part(void) { return songp.n ? song_cur % songp.n : 0; }
static void lay_hush_set(void);
static void song_next(void)
{
    if (songp.on && songp.n) {
        song_cur = (song_cur + 1) % songp.n;
        lay_hush_set();
    }
}
static void lay_hush_set(void)
{
    uint32_t solo = 0, mute = 0;
    int i;
    for (i = 0; i < NLAYER; i++)
        if (st.layer[i].part) {
            if (st.layer[i].rsv[1] & LAY_SOLO) solo |= 1u << i;
            if (st.layer[i].rsv[1] & LAY_MUTE) mute |= 1u << i;
        }
    if (songp.on && songp.n && !solo)            /* SONG: the part says what is heard (a solo still wins) */
        mute = songp.known & ~songp.mask[song_cur % songp.n];
    lay_hushed = layers() ? (solo ? ~solo : mute) : 0u;
}
static int lay_hush(int lay) { return lay >= 1 && lay <= NLAYER && (lay_hushed >> (lay - 1) & 1u); }
static f32 lay_gain(int lay)
{
    f32 c = (f32)(100 - iclamp(st.layer[(lay - 1) & (NLAYER - 1)].rsv[0], 0, 100)) * 0.01f;
    return c * c;
}
static volatile int loop_state = LOOP_EMPTY;
static volatile f32 loop_pos, loop_len = 768.0f;
static uint32_t loop_held[4];
static f32 click_env, click_ph, click_ph2, click_f, click_pe, click_lp, click_amp;
static f32 sub_l, sub_r;                         /* what is below hearing in the mix, to take off it */
#define SUB_K (H1_TAU * 18.0f * ISR)             /* 18 Hz */
static uint32_t click_rng = 0x7407C0DEu;
static int click_at = -1;
static f32 free_pos;                             /* the metronome's own bar, when nothing is running under it */
static int free_on;

/* ---- cues (internal.h): one at a time, a few segments each. A segment is a wave that slides from one
 * note to another while its level goes from one value to another; the noise takes its note as where it is
 * cut off. ------------------------------------------------------------------------------------------- */
enum { CW_SIN, CW_SQR, CW_SAW, CW_NOISE, CW_CHOP = 4, CW_VIB = 8, CW_END = 255 };
/* wave, from note, to note, hundredths of a second, level: from << 4 | to */
static const uint8_t CUES[] = {
    CW_SAW | CW_CHOP, 38, 38, 75, 0xEE, CW_SAW, 38, 33, 12, 0xE0, CW_END,                        /* TILT: the buzzer */
    CW_SQR, 72, 72, 6, 0x99, CW_SQR, 76, 76, 6, 0x99, CW_SQR, 79, 79, 6, 0x99,                   /* PIP: one up */
    CW_SQR, 84, 84, 6, 0x99, CW_SQR, 88, 88, 32, 0x90, CW_END,
    CW_SQR, 69, 76, 28, 0x66, CW_SQR, 76, 69, 28, 0x66, CW_SQR, 69, 76, 28, 0x66,                /* TURBO: the siren */
    CW_SQR, 76, 69, 28, 0x66, CW_SQR, 69, 76, 28, 0x64, CW_SQR, 76, 69, 44, 0x40, CW_END,
    CW_SIN | CW_VIB, 86, 74, 60, 0x4B, CW_SIN | CW_VIB, 74, 93, 130, 0xB0, CW_END,               /* ORBIT: the answer */
    CW_NOISE, 112, 86, 9, 0xF6, CW_SIN, 98, 98, 5, 0xA3, CW_SIN, 105, 105, 5, 0x92,              /* COZY: the pot */
    CW_SIN, 93, 93, 24, 0x80, CW_END,
    CW_SQR | CW_CHOP, 45, 43, 13, 0xCC, CW_SQR, 43, 43, 7, 0x00,                                 /* KOI: the frog */
    CW_SQR | CW_CHOP, 47, 44, 22, 0xD0, CW_END,
    CW_NOISE, 112, 98, 5, 0xB8, CW_NOISE, 74, 52, 60, 0xF9, CW_NOISE, 52, 40, 170, 0x90, CW_END, /* HAUNT: thunder */
};
#define CUE_AMP 0.3f
static const uint8_t *cue_p;                     /* the segment sounding, 0: none */
static uint32_t cue_n, cue_count, cue_rng = 0xC0E5EEDu;
static f32 cue_ph, cue_mod, cue_g, cue_lp;

static APART void cue_start(int i)
{
    const uint8_t *p = CUES;
    if (i >= NCUE)
        return;
    for (; i; i--) {
        while (*p != CW_END)
            p += 5;
        p++;
    }
    cue_p = p;
    cue_n = 0;
    cue_count++;
}
uint32_t eng_cues(void) { return cue_count; }

static APART void cue_block(f32 *L, f32 *R)
{
    const uint8_t *s = cue_p;
    f32 len = (f32)s[3] * 0.01f * SR, u = (f32)cue_n / len, note = (f32)s[1] + (f32)((int)s[2] - (int)s[1]) * u;
    f32 amp = ((f32)(s[4] >> 4) + (f32)((int)(s[4] & 15) - (int)(s[4] >> 4)) * u) * (CUE_AMP / 15.0f), inc, g = cue_g, ph = cue_ph, x;
    int n, wave = s[0] & 3;
    cue_mod = fractf(cue_mod + (s[0] & CW_VIB ? 9.0f : 32.0f) * (f32)CTRL * ISR);
    if (s[0] & CW_VIB)
        note += 1.5f * tsin(cue_mod);
    if ((s[0] & CW_CHOP) && cue_mod > 0.5f)
        amp *= 0.15f;
    inc = mtof(note) * ISR;
    if (wave == CW_NOISE) {                      /* (cut low, it is brought back up to be heard) */
        inc = minf(1.0f, H1_TAU * inc);
        amp *= minf(4.0f, rsqrtf(inc));
    }
    for (n = 0; n < CTRL; n++) {
        g += (amp - g) * 0.02f;                  /* (no step in level is a click) */
        if (wave == CW_NOISE) {
            cue_lp += (noise_s(&cue_rng) - cue_lp) * inc;
            x = cue_lp;
        } else {
            ph += inc;
            if (ph >= 1.0f) ph -= 1.0f;
            x = wave == CW_SIN ? tsin(ph) : (wave == CW_SQR ? (ph < 0.5f ? 0.6f : -0.6f) : ph * 1.4f - 0.7f);
        }
        x *= g;
        L[n] += x;
        R[n] += x;
    }
    cue_ph = ph;
    cue_g = g;
    cue_n += CTRL;
    if ((f32)cue_n >= len) {
        cue_n = 0;
        cue_p = s[5] == CW_END ? 0 : s + 5;
    }
}

/* ---- the metronome: a soft knock on the meter's beats, a little higher on the one ------------- */
static int click_mode(void) { return st.seq[5] < 3 ? st.seq[5] : CLICK_REC; }

static void click_run(f32 pos, int sound)
{
    int bt = seq_bar_ticks(), tick = (int)pos % bt, e = (int)pos / 24;
    if (e != click_at && sound && seq_accent(tick - tick % 24)) {
        click_env = 1.0f;
        click_pe = 1.0f;
        click_ph = click_ph2 = 0.0f;
        click_f = tick < 24 ? 392.0f : 294.0f;
        click_amp = tick < 24 ? 0.3f : 0.23f;
    }
    click_at = e;
}
static int loop_fresh;                           /* the take about to start replaces the loop */

static void loop_release(void)
{
    int n;
    for (n = 0; n < 128; n++)
        if (loop_held[n >> 5] & (1u << (n & 31)))
            route(n, 0, 0, 0);
    loop_held[0] = loop_held[1] = loop_held[2] = loop_held[3] = 0;
    for (n = 0; n < NVOICE; n++)                 /* and with LAYERS, the voices it started itself */
        if (voices[n].stage && voices[n].gate && voices[n].seq) {
            voices[n].gate = 0;
            voices[n].stage = 4;
            mout(0, voices[n].note, 0);
        }
}

static void loop_play_range(f32 a, f32 b)
{
    int i;
    for (i = lev_find(a); i < nlev && (f32)lev[i].tick < b; i++) {
        if (!(lev[i].on & 0x80)) {
            int n = lev[i].note, vel = lev[i].vel ? lev[i].vel : 127;
            if (layers()) {                      /* each note on the part it was played on, past the arpeggiator */
                int part = lev[i].part ? lev[i].part : live_part(), lay = layer_of(lev[i].lay, part);
                if (lev[i].on & 1) {
                    if (lay_hush(lay))
                        continue;
                    voice_on_part(n, part, 1, lay, vel);
                    mout(1, n, vel);
                } else {
                    voice_off_part(n, part, lay);
                    mout(0, n, 0);
                }
            } else if (lev[i].on & 1) {
                loop_held[n >> 5] |= 1u << (n & 31);
                route(n, 1, vel, 0);
            } else if (loop_held[n >> 5] & (1u << (n & 31))) {
                loop_held[n >> 5] &= ~(1u << (n & 31));
                route(n, 0, 0, 0);
            }
        }
    }
}

static void loop_tick(f32 dticks)
{
    f32 a, b;
    int i, s = loop_state;
    if (s != LOOP_REC && s != LOOP_PLAY && s != LOOP_DUB)
        return;
    a = loop_pos;
    b = a + dticks;
    if (s != LOOP_REC)
        loop_play_range(a, b);
    if (s == LOOP_DUB && take_left > 0.0f && (take_left -= dticks) <= 0.0f) {
        loop_pos = b < loop_len ? b : b - loop_len;   /* (the take's end, where its held notes end) */
        take_close();
    }
    click_run(b, s == LOOP_PLAY ? click_mode() == CLICK_ON : click_mode() != CLICK_OFF);
    if (b >= loop_len) {
        b -= loop_len;
        loop_release();
        for (i = 0; i < nlev; i++)
            lev[i].on &= 0x7F;
        if (s == LOOP_REC) {
            loop_state = LOOP_PLAY;
            rec_forget();
            layers_close();
        }
        click_at = -1;
        song_next();                             /* SONG: the next part, as it comes round */
        loop_play_range(0.0f, b);
    }
    loop_pos = b;
}

static int arp_recorded(void);
static APART void loop_record(int note, int on, int vel)
{
    static const f32 Q[3] = {0.0f, 24.0f, 48.0f};
    f32 t = loop_pos, q = Q[st.seq[3] < 3 ? st.seq[3] : 0];
    if (q > 0.0f && arp_recorded())              /* the arpeggiator's notes to its own RATE, not to SNAP's 1/16: its */
        q = RATE_TICKS[st.ip[cur_i].arp[1] < 6 ? st.ip[cur_i].arp[1] : 3];   /* triplets and 1/32s kept (#49) */
    int part, lay, kept;
    LEv *e;
    note &= 127;
    kept = (rec_open[note >> 5] >> (note & 31)) & 1;
    if (on) {                                    /* a note-on keeps room for its note-off: none ever goes missing */
        if (kept || nlev + nopen + 2 > NLEV) {
            if (!kept)
                loop_full++;
            return;
        }
    } else if (!kept && nlev + nopen + 1 > NLEV)
        return;
    if (on) {                                    /* (its note-off goes in where it did, whatever is in hand by then) */
        part = live_part();
        lay = layers() ? layer_take() : 0;
        rec_part[note] = (uint8_t)part;
        rec_lay[note] = (uint8_t)lay;
    } else {
        part = rec_part[note] ? rec_part[note] : live_part();
        lay = layer_of(rec_lay[note], part);     /* (a note-on from an earlier take: its layer may be another's now) */
    }
    if (on && q > 0.0f) {
        t = (f32)ifloor(t / q + 0.5f) * q;
        if (t >= loop_len)
            t -= loop_len;
    }
    if (kept) {                                  /* (the room it kept is the room it takes) */
        rec_open[note >> 5] &= ~(1u << (note & 31));
        nopen--;
    }
    e = lev_insert((uint16_t)t);
    if (!e)
        return;
    e->tick = (uint16_t)t;
    e->note = (uint8_t)note;
    e->on = (uint8_t)((on ? 1 : 0) | 0x80);
    e->part = (uint8_t)part;
    e->lay = (uint8_t)lay;
    e->vel = (uint8_t)(on ? iclamp(vel, 1, 127) : 0);
    e->rsv = take_left > 0.0f ? TAKE_TAG : 0u;
    if (on) {
        rec_open[note >> 5] |= 1u << (note & 31);
        nopen++;
    }
    loop_gen++;
}

/* ---- a take's own length (0.4.0, #69): with LAYERS on and a loop there, BARS is the next take's length.
 * Longer than the loop: the loop is repeated out to it first (each layer plays again, as it was) and the take
 * goes on over all of it. Shorter: the take closes itself once that long, and what it recorded is repeated
 * out over the loop (a 2-bar drum under 16 bars). Bars are powers of two, so one always divides the other;
 * a meter changed since (no whole number of takes in the loop) leaves the take as long as the loop. */

static void loop_extend(f32 want)               /* every event again, a loop's length on, out to want */
{
    int n0 = nlev, k, i, copies = (int)(want / loop_len + 0.5f);
    for (k = 1; k < copies; k++)
        for (i = 0; i < n0; i++) {
            if (nlev + nopen + 1 > NLEV) {
                loop_full++;
                goto done;
            }
            lev[nlev] = lev[i];                  /* (each copy after the last, a loop later: still in order) */
            lev[nlev].tick = (uint16_t)(lev[i].tick + (f32)k * loop_len);
            lev[nlev].on &= 0x7F;
            nlev++;
        }
done:
    loop_len = want;
    loop_gen++;
}

static void take_fit(void)                       /* at a take's start over a loop: BARS against the loop */
{
    f32 want = (f32)(loop_bars() * seq_bar_ticks()), r;
    take_len = take_left = 0.0f;
    if (!layers() || !nlev || want == loop_len)
        return;
    r = want > loop_len ? want / loop_len : loop_len / want;
    if (r != (f32)(int)r)
        return;
    if (want > loop_len)
        loop_extend(want);
    else
        take_len = take_left = want;
}

static void loop_record(int note, int on, int vel);
static void take_close(void)                     /* a short take, its length up: closed, and repeated out */
{
    int i, k, n, copies = (int)(loop_len / take_len + 0.5f);
    for (i = 0; i < 128; i++)                    /* (keys still down: their notes end here, in the take) */
        if (rec_open[i >> 5] >> (i & 31) & 1u)
            loop_record(i, 0, 0);
    n = nlev;
    for (i = 0; i < n; i++) {
        int j;
        LEv e;
        for (j = 0; j < nlev && lev[j].rsv != TAKE_TAG; j++)
            ;
        if (j == nlev)
            break;
        lev[j].rsv = 0;
        lev[j].on &= 0x7F;                       /* (heard from now on, as a closed take's notes are) */
        e = lev[j];
        for (k = 1; k < copies; k++) {
            f32 t = (f32)e.tick + (f32)k * take_len;
            LEv *d;
            if (t >= loop_len)
                t -= loop_len;
            if (nlev + nopen + 1 > NLEV) {
                loop_full++;
                break;
            }
            d = lev_insert((uint16_t)t);
            *d = e;
            d->tick = (uint16_t)t;
        }
    }
    take_len = take_left = 0.0f;
    loop_state = LOOP_PLAY;
    rec_forget();
    layers_close();
    loop_gen++;
}

/* With LAYERS a take is played back note for note, past the arpeggiator, so what goes into it is what
 * the arpeggiator played, not the keys that were held. */
static int arp_recorded(void) { return layers() && st.ip[cur_i].arp[0] != 0; }
static void arp_take(int note, int on, int vel)
{
    if (arp_recorded() && st.seq[0] != SEQ_STEP && (loop_state == LOOP_REC || loop_state == LOOP_DUB))
        loop_record(note, on, vel);
}

static void loop_stop(void)
{
    rec_forget();
    loop_release();
    loop_state = nlev ? LOOP_IDLE : LOOP_EMPTY;
    loop_pos = 0.0f;
    layers_close();
}

/* ---- step sequencer -------------------------------------------------------------------------
 * A row of steps, st.steps, up to four notes each. Stopped with WRITE on, a key (or a chord)
 * fills the step under the cursor and the cursor moves on when the hand lifts; running with
 * WRITE on, what is played lands on the nearest step. */
static volatile int step_playing, step_write, step_cursor, step_at;
static f32 step_acc, step_off;
static uint8_t step_snd[NSNOTE], step_sp[NSNOTE], step_sl[NSNOTE];   /* the notes the step holds, the part of each (0:
                                                                         * through route) and its layer */
static int step_nsnd, step_gesture;

static void step_release(void)
{
    int i;
    for (i = 0; i < step_nsnd; i++) {
        if (step_sp[i]) {
            voice_off_part(step_snd[i], step_sp[i], step_sl[i]);
            mout(0, step_snd[i], 0);
        } else
            route(step_snd[i], 0, 0, 0);
    }
    step_nsnd = 0;
}

static void step_fire(int i, f32 len)
{
    int j;
    step_release();
    for (j = 0; j < NSNOTE; j++)
        if (st.steps[i][j]) {
            int n = st.steps[i][j] - 1, part = 0, lay = 0, vel = st.step_vel[i][j] ? st.step_vel[i][j] : 127;
            if (layers()) {
                part = part_ok(st.step_part[i][j]) ? st.step_part[i][j] : live_part();
                lay = layer_of(st.step_lay[i][j], part);
                if (lay_hush(lay))
                    continue;
                voice_on_part(n, part, 1, lay, vel);
                mout(1, n, vel);
            } else
                route(n, 1, vel, 0);
            step_snd[step_nsnd] = (uint8_t)n;
            step_sl[step_nsnd] = (uint8_t)lay;
            step_sp[step_nsnd++] = (uint8_t)part;
        }
    step_off = len;
}

/* everything the sequencer holds, however it started it */
static void seq_release_all(void)
{
    loop_release();
    step_release();
}

static f32 step_len(int i)
{
    f32 g = (f32)seq_grid_ticks(), sw = st.seq[4] < 2 ? (f32)st.glo[1] * 0.01f * 0.34f : 0.0f;
    return (i & 1) ? g * (1.0f - sw) : g * (1.0f + sw);
}

static void step_stop(void)
{
    step_release();
    step_playing = 0;
    step_at = 0;
    step_acc = 0.0f;
}

static void step_tick(f32 dticks)
{
    int n = step_count();
    if (step_nsnd) {
        step_off -= dticks;
        if (step_off <= 0.0f)
            step_release();
    }
    if (step_cursor >= n)
        step_cursor = 0;
    if (!step_playing)
        return;
    step_acc += dticks;
    if (step_at >= n || step_acc >= step_len(step_at)) {
        int at = step_at;
        step_acc = at >= n ? 0.0f : step_acc - step_len(at);
        at = at + 1 >= n ? 0 : at + 1;
        if (at == 0) {
            click_at = -1;
            song_next();                         /* SONG: the next part, as the pattern comes round */
        }
        step_at = at;
        step_fire(at, step_len(at) * 0.85f);
    }
    click_run((f32)(step_at * seq_grid_ticks()) + step_acc, click_mode() == CLICK_ON);
}

/* with nothing playing, the metronome keeps its own bar: while a take is armed, or always if it is ON */
static void free_tick(f32 dticks)
{
    int cm = click_mode(), s = loop_state, run;
    if (st.seq[0] == SEQ_STEP)
        run = !step_playing && cm == CLICK_ON;
    else
        run = (s == LOOP_ARMED && cm != CLICK_OFF) || ((s == LOOP_EMPTY || s == LOOP_IDLE) && cm == CLICK_ON);
    if (!run) {
        free_on = 0;
        return;
    }
    if (!free_on) {
        free_on = 1;
        free_pos = 0.0f;
        click_at = -1;
    } else {
        free_pos += dticks;
        if (free_pos >= (f32)seq_bar_ticks()) {
            free_pos -= (f32)seq_bar_ticks();
            click_at = -1;
        }
    }
    click_run(free_pos, 1);
}

static void step_put(int i, int note, int vel)
{
    int j, part = live_part();
    for (j = 0; j < NSNOTE; j++)
        if (st.steps[i][j] == note + 1 && (!layers() || st.step_part[i][j] == part)) {
            st.step_vel[i][j] = (uint8_t)iclamp(vel, 1, 127);
            return;
        }
    for (j = 0; j < NSNOTE - 1 && st.steps[i][j]; j++)
        ;
    st.steps[i][j] = (uint8_t)(note + 1);
    st.step_part[i][j] = (uint8_t)part;
    st.step_vel[i][j] = (uint8_t)iclamp(vel, 1, 127);
    st.step_lay[i][j] = (uint8_t)(layers() ? layer_take() : 0);
}

static void step_clear(int i)
{
    int j;
    for (j = 0; j < NSNOTE; j++)
        st.steps[i][j] = st.step_part[i][j] = st.step_vel[i][j] = st.step_lay[i][j] = 0;
}

/* a note's instrument: the one it was played on, or with none kept, the one in hand */
static int tag_inst(int part) { return part_ok((uint32_t)part) ? PART_INST(part) : cur_i; }

/* only the notes of one instrument out of a step: the others close up */
static void step_clear_inst(int i, int inst)
{
    int j, k = 0;
    for (j = 0; j < NSNOTE; j++)
        if (st.steps[i][j] && tag_inst(st.step_part[i][j]) != inst) {
            st.steps[i][k] = st.steps[i][j];
            st.step_part[i][k] = st.step_part[i][j];
            st.step_vel[i][k] = st.step_vel[i][j];
            st.step_lay[i][k] = st.step_lay[i][j];
            k++;
        }
    for (; k < NSNOTE; k++)
        st.steps[i][k] = st.step_part[i][k] = st.step_vel[i][k] = st.step_lay[i][k] = 0;
}

/* a new chord on a step, or a rest: with LAYERS only the instrument in hand gives up its place there */
static void step_empty(int i)
{
    if (layers())
        step_clear_inst(i, cur_i);
    else
        step_clear(i);
}

static void step_record(int note, int on, int vel)
{
    int n = step_count();
    if (step_playing) {
        if (on) {
            int i = step_at;
            if (step_acc > step_len(i) * 0.5f)
                i = i + 1 >= n ? 0 : i + 1;
            step_put(i, note, vel);
        }
        return;
    }
    if (on) {
        if (step_gesture++ == 0)
            step_empty(step_cursor);
        step_put(step_cursor, note, vel);
    } else if (step_gesture > 0 && --step_gesture == 0)
        step_cursor = step_cursor + 1 >= n ? 0 : step_cursor + 1;
}

/* ---- live notes: the latch, then the recorder, then route -------------------------------- */
static uint32_t latched[4];
static int phys_down;

static void eff_note(int note, int on, int vel, int from_midi)
{
    if (st.seq[0] == SEQ_STEP) {
        if (step_write)
            step_record(note, on, vel);
        route(note, on, vel, from_midi);
        return;
    }
    if (on && loop_state == LOOP_ARMED && loop_fresh && nlev && layers()) {
        int i;                                   /* LAYERS: a take over a stopped loop joins it from the top, */
        for (i = 0; i < nlev; i++)               /* and nothing is erased (holding REC is what clears) */
            lev[i].on &= 0x7F;
        loop_pos = 0.0f;
        click_at = -1;
        loop_state = LOOP_DUB;
        arp_acc = 1e9f;
        song_cur = 0;
        take_fit();
    }
    if (on && loop_state == LOOP_ARMED) {
        if (loop_fresh) {
            nlev = 0;
            rec_forget();
            loop_gen++;
        }
        loop_len = (f32)(loop_bars() * seq_bar_ticks());
        loop_pos = 0.0f;
        click_at = -1;
        if (free_on && free_pos < (f32)(seq_bar_ticks() - 48)) {
            loop_pos = free_pos;                 /* the click was counting: the take starts on its bar, not on your first note */
            click_at = (int)free_pos / 24;
        }
        loop_state = LOOP_REC;
        arp_acc = 1e9f;
    }
    if ((loop_state == LOOP_REC || loop_state == LOOP_DUB) && !arp_recorded())
        loop_record(note, on, vel);
    route(note, on, vel, from_midi);
}

static uint32_t latched_midi[4];                 /* (which latched notes came in over MIDI: their note-off is not sent) */
static void unlatch(void)
{
    int n;
    for (n = 0; n < 128; n++)
        if (latched[n >> 5] & (1u << (n & 31)))
            eff_note(n, 0, 0, (int)(latched_midi[n >> 5] >> (n & 31) & 1u));
    latched[0] = latched[1] = latched[2] = latched[3] = 0;
    latched_midi[0] = latched_midi[1] = latched_midi[2] = latched_midi[3] = 0;
}

/* MIDI's sustain pedal (MIDI page, PEDAL): while it is down a key let go of goes on sounding, as if it
 * were still held; the pedal up lets them all go. (Before HOLD and the sequencer: a take records what
 * was heard.) */
static uint32_t sus_keep[4];
static int sus_on;
static void live_note(int note, int on, int vel, int from_midi);
static void sustain(int down)
{
    int n;
    sus_on = down;
    if (down)
        return;
    for (n = 0; n < 128; n++)
        if (sus_keep[n >> 5] & (1u << (n & 31))) {
            sus_keep[n >> 5] &= ~(1u << (n & 31));
            live_note(n, 0, 0, 0);
        }
}

/* KEYS HOLD: OFF, ON (what is played stays until the next), ARP (the same, only while the ARP runs) */
static int hold_on(void) { return st.keys[3] == 1 || (st.keys[3] == 2 && st.ip[cur_i].arp[0]); }

static void live_note(int note, int on, int vel, int from_midi)
{
    uint32_t bit = 1u << (note & 31);
    if (on)
        sus_keep[note >> 5 & 3] &= ~bit;
    else if (sus_on) {
        sus_keep[note >> 5 & 3] |= bit;
        return;
    }
    if (!hold_on()) {
        eff_note(note, on, vel, from_midi);
        return;
    }
    if (on) {
        if (phys_down == 0)
            unlatch();                           /* a new gesture replaces what was held */
        phys_down++;
        if (!(latched[note >> 5] & (1u << (note & 31)))) {
            latched[note >> 5] |= 1u << (note & 31);
            if (from_midi)
                latched_midi[note >> 5] |= 1u << (note & 31);
            eff_note(note, 1, vel, from_midi);
        }
    } else if (phys_down > 0)
        phys_down--;
}

static void panic(int from_midi)
{
    int i;
    if (eng_midi_fn && !from_midi)               /* (all notes off, out too; not back to where a CC 123 came from:
                                                  * a computer that passes MIDI through would send it back, and
                                                  * the two would trade it for ever) */
        eng_mout3(0xB0u, 123u, 0u);
    unlatch();
    loop_release();
    step_release();
    step_gesture = 0;
    nheld = 0;
    direct[0] = direct[1] = direct[2] = direct[3] = 0;
    sus_keep[0] = sus_keep[1] = sus_keep[2] = sus_keep[3] = 0;
    sus_on = 0;
    arp_release();
    phys_down = 0;
    for (i = 0; i < NVOICE; i++)
        if (voices[i].stage) {
            voices[i].gate = 0;
            voices[i].stage = 4;
        }
}

/* one layer: its notes, in the loop and in the steps, and the layer itself (the layer view: a key and REC) */
static APART void clear_layer(int lay)
{
    int i, j, k;
    if (lay < 1 || lay > NLAYER)
        return;
    seq_release_all();
    for (i = k = 0; i < nlev; i++)
        if (lev[i].lay != lay)
            lev[k++] = lev[i];
    nlev = k;
    for (i = 0; i < NSTEP; i++) {
        for (j = k = 0; j < NSNOTE; j++)
            if (st.steps[i][j] && st.step_lay[i][j] != lay) {
                st.steps[i][k] = st.steps[i][j];
                st.step_part[i][k] = st.step_part[i][j];
                st.step_vel[i][k] = st.step_vel[i][j];
                st.step_lay[i][k] = st.step_lay[i][j];
                k++;
            }
        for (; k < NSNOTE; k++)
            st.steps[i][k] = st.step_part[i][k] = st.step_vel[i][k] = st.step_lay[i][k] = 0;
    }
    {
        uint8_t *p = (uint8_t *)&st.layer[lay - 1];
        for (i = 0; i < (int)sizeof(Layer); i++)
            p[i] = 0;
    }
    song_drop(lay - 1);
    rec_forget();
    loop_gen++;
    if (!nlev && (loop_state == LOOP_IDLE || loop_state == LOOP_PLAY)) {
        loop_pos = 0.0f;
        loop_state = LOOP_EMPTY;
    }
}

static void handle(uint32_t e)
{
    int type = (int)(e >> 8 & 255u), a = (int)(e & 255u), vel = (int)(e >> 16 & 127u), midi = (int)(e >> 23 & 1u);
    switch (type) {
    case EV_NOTE_ON: live_note(a & 127, 1, vel ? vel : 127, midi); break;
    case EV_NOTE_OFF: live_note(a & 127, 0, 0, midi); break;
    case EV_PANIC: panic(a & 1); cue_p = 0; break;
    case EV_CUE: cue_start(a); break;
    case EV_SUSTAIN: sustain(a & 1); break;
    case EV_CLEAR_LAY: clear_layer(a); break;
    case EV_UNLATCH: unlatch(); phys_down = 0; break;
    case EV_INST:
        if (a < NINST && a != cur_i) {
            unlatch();
            nheld = 0;
            arp_release();
            layers_close();                      /* (what is played on the next instrument is a layer of its own) */
            if (layers()) {                      /* at once: what sounds keeps its own instrument, and a take goes on */
                int i;
                for (i = 0; i < NVOICE; i++)
                    if (voices[i].stage && voices[i].gate && !voices[i].seq) {
                        voices[i].gate = 0;
                        voices[i].stage = 4;
                    }
                cur_i = a;
                cur = insts[a];
                break;
            }
            pending_i = a;
            fade_blocks = 0;
            voices_release_all();
        }
        break;
    case EV_SEQ_MODE:
        loop_stop();
        step_stop();
        step_write = 0;
        step_gesture = 0;
        layers_close();
        break;
    case EV_STEP_MOVE: {
        int n = step_count(), c = ((step_cursor + a - 128) % n + n) % n;
        step_cursor = c;
        step_gesture = 0;
        if (!step_playing)
            step_fire(c, 30.0f);                 /* hear what is there */
        break;
    }
    case EV_STEP_REST:
        if (step_playing)
            step_empty(step_at);
        else {
            int n = step_count();
            step_empty(step_cursor);
            step_cursor = step_cursor + 1 >= n ? 0 : step_cursor + 1;
        }
        break;
    case EV_REC:
        if (st.seq[0] == SEQ_STEP) {
            step_write = !step_write;
            step_gesture = 0;
            if (!step_write)
                layers_close();
            break;
        }
        switch (loop_state) {
        case LOOP_EMPTY:
        case LOOP_IDLE:
            loop_fresh = 1;                      /* a new take; REC while it plays adds to it instead */
            loop_state = LOOP_ARMED;
            break;
        case LOOP_ARMED: loop_state = nlev ? LOOP_IDLE : LOOP_EMPTY; break;
        case LOOP_REC: loop_state = LOOP_PLAY; rec_forget(); layers_close(); break;
        case LOOP_PLAY: loop_state = LOOP_DUB; take_fit(); break;
        default:
            if (take_left > 0.0f) {
                take_close();                    /* (closed early: what it has, repeated out all the same) */
                break;
            }
            loop_state = LOOP_PLAY; rec_forget(); layers_close(); break;
        }
        break;
    case EV_PLAY:
        if (st.seq[0] == SEQ_STEP) {
            if (step_playing)
                step_stop();
            else {
                step_playing = 1;
                step_at = 0;
                step_acc = 0.0f;
                song_cur = 0;
                click_at = -1;
                arp_acc = 1e9f;
                step_fire(0, step_len(0) * 0.85f);
            }
            break;
        }
        switch (loop_state) {
        case LOOP_EMPTY: break;
        case LOOP_IDLE: {
            int i;
            for (i = 0; i < nlev; i++)
                lev[i].on &= 0x7F;
            loop_pos = 0.0f;
            click_at = -1;
            arp_acc = 1e9f;
            song_cur = 0;
            loop_state = LOOP_PLAY;
            break;
        }
        case LOOP_ARMED: loop_state = nlev ? LOOP_IDLE : LOOP_EMPTY; break;
        default: loop_stop(); break;
        }
        break;
    case EV_CLEAR:
        if (a > 0 && a <= NINST) {               /* only what was played on one instrument */
            int i, k = 0, inst = a - 1;
            seq_release_all();
            if (st.seq[0] == SEQ_STEP) {
                for (i = 0; i < NSTEP; i++)
                    step_clear_inst(i, inst);
                layers_tidy();
                break;
            }
            for (i = 0; i < nlev; i++)
                if (tag_inst(lev[i].part) != inst)
                    lev[k++] = lev[i];
            nlev = k;
            rec_forget();
            loop_gen++;
            if (!nlev) {
                loop_pos = 0.0f;
                loop_state = LOOP_EMPTY;
            }
            layers_tidy();
            break;
        }
        if (st.seq[0] == SEQ_STEP) {
            int i;
            step_stop();
            for (i = 0; i < NSTEP; i++)
                step_clear(i);
            step_cursor = 0;
            layers_tidy();
            break;
        }
        loop_release();
        nlev = 0;
        rec_forget();
        loop_gen++;
        loop_pos = 0.0f;
        loop_state = LOOP_EMPTY;
        songp.on = songp.n = 0;
        songp.known = 0;
        song_cur = 0;
        layers_tidy();
        break;
    default: break;
    }
}

void eng_seq_info(SeqInfo *si)
{
    si->mode = st.seq[0] == SEQ_STEP ? SEQ_STEP : SEQ_LOOP;
    si->loop_state = loop_state;
    si->loop_pos = loop_len > 0.0f ? loop_pos / loop_len : 0.0f;
    si->playing = step_playing;
    si->write = step_write;
    si->per_bar = step_per_bar();
    si->bars = step_bars();
    si->nsteps = step_count();
    si->cursor = imin(step_cursor, si->nsteps - 1);
    si->at = imin(step_at, si->nsteps - 1);
}

/* The sequencer on the keys, as the stock firmware shows it: the sixteen white keys are the pattern from
 * left to right. One key a step up to sixteen steps, two steps a key up to thirty-two, and so on up. A key
 * is lit where its steps hold notes, and the one being played is turned over (a lit one goes dark, a dark
 * one lights), so the place runs across the keyboard. The loop is shown the same way, in sixteenths.
 * Returns 0 when the keys are not the sequencer's (then they show what is sounding). */
int eng_seq_leds(uint32_t *out, int page_open, int blink)
{
    static const uint8_t WHITE[16] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};
    uint32_t m = 0;
    int n, per, at = -1, wr = -1, i;
    if (st.seq[0] == SEQ_STEP) {
        if (!step_playing && !step_write && !page_open)
            return 0;
        n = step_count();
        per = (n + 15) / 16;
        for (i = 0; i < n; i++)
            if (st.steps[i][0])
                m |= 1u << WHITE[i / per];
        if (step_playing)
            at = imin(step_at, n - 1) / per;
        else if (step_write)
            wr = imin(step_cursor, n - 1) / per;
    } else {
        int s = loop_state, ne = nlev;
        if (s != LOOP_REC && s != LOOP_PLAY && s != LOOP_DUB && !(page_open && s == LOOP_IDLE))
            return 0;
        n = imax(1, (int)loop_len / 24);
        per = (n + 15) / 16;
        for (i = 0; i < ne; i++)
            if (lev[i].on & 1)
                m |= 1u << WHITE[imin(lev[i].tick / 24, n - 1) / per];
        if (s != LOOP_IDLE)
            at = imin((int)loop_pos / 24, n - 1) / per;
    }
    if (at >= 0)
        m ^= 1u << WHITE[at];
    if (wr >= 0)                                            /* the step being written blinks */
        m = blink ? m | 1u << WHITE[wr] : m & ~(1u << WHITE[wr]);
    *out = m;
    return 1;
}

/* ---- the block ------------------------------------------------------------------------- */
/* An instrument's own volume as a gain: a square, as MASTER's is. */
static f32 inst_gain(int i)
{
    f32 v = (f32)INST_VOL(i) * 0.01f;
    return v * v;
}

static void switch_instrument(int i)
{
    int v, j;
    cur_i = i;
    cur = insts[i];
    for (v = 0; v < NVOICE; v++) {
        uint32_t serial = voices[v].serial;
        uint8_t *p = (uint8_t *)&voices[v];
        for (j = 0; j < (int)sizeof(Voice); j++)
            p[j] = 0;
        voices[v].serial = serial;
        voices[v].idx = (uint8_t)v;
        for (j = 0; j < KS_LEN; j++)
            ks_line[v][0][j] = ks_line[v][1][j] = 0.0f;
    }
    ir[i].at = blkno - 2u;                       /* (its knobs are taken as they stand, not eased from where they were) */
    for (j = 0; j < NINST; j++)
        ir[j].linger = 0;                        /* (every voice is gone: nothing of another instrument rings on) */
}

/* An instrument's numbers for this control block: the knobs of the algorithm it is on eased (~12 ms), its
 * LFO moved on and put on its target, its envelope rates. Once a block for each instrument that sounds.
 * (Every algorithm has its own four knobs. The eased ones are those of the algorithm in hand, the only
 * ones a hand can turn, and they ease across a change of algorithm too. A part that goes on sounding on
 * another algorithm takes its knobs as they stand: part_knobs.) */
static f32 blk_time;

/* An LFO moved on a control block and put on its target, and the envelope's rates: from an LFO page and an
 * ENV page (an instrument's, or a layer's as it was left) */
static void mod_run(Mod *m, const uint8_t *lfo_p, const uint8_t *env_p, Lfo *lf, uint32_t *rng, int live)
{
    f32 dt = (f32)CTRL * ISR, brate = SR / (f32)CTRL, lfo, depth, f;
    int target;
    f = 0.05f * exp2fx((f32)lfo_p[1] * 0.09f);
    lf->ph += f * dt;
    if (lf->ph >= 1.0f) {
        lf->ph -= (f32)ifloor(lf->ph);
        lf->sh = rng_s(rng);
    }
    lf->drift += (lf->sh - lf->drift) * minf(1.0f, f * dt * 3.0f);
    switch (lfo_p[0]) {
    case 1: lfo = 4.0f * absf(lf->ph - 0.5f) - 1.0f; break;
    case 2: lfo = 1.0f - 2.0f * lf->ph; break;
    case 3: lfo = lf->ph < 0.5f ? 1.0f : -1.0f; break;
    case 4: lfo = lf->sh; break;
    case 5: lfo = clampf(lf->drift * 1.6f, -1.0f, 1.0f); break;
    default: lfo = tsin(lf->ph); break;
    }
    depth = (f32)lfo_p[2] * 0.01f;
    if (live)                                    /* MIDI's mod wheel: more of the LFO (on PITCH, a vibrato) */
        depth = minf(1.0f, depth + eng_wheel * 0.35f);
    target = lfo_p[3];
    if (live)
        lfo_out = lfo * depth;
    m->vol = 1.0f;
    m->panmod = 0.0f;
    m->kmod_k = -1;
    m->kmod = 0.0f;
    m->lfo = lfo;
    m->bend = ((f32)st.glo[2] - 50.0f) * 0.01f + (live ? eng_bend : 0.0f);
    if (target == 0)
        m->bend += lfo * depth * depth * 12.0f;
    else if (target <= 4) {
        m->kmod_k = target - 1;
        m->kmod = lfo * depth * 0.5f;
    }
    else if (target == 5)
        m->vol = 1.0f - depth * (0.5f + 0.5f * lfo);
    else
        m->panmod = lfo * depth;
    m->att = dt / (0.0015f * exp2fx((f32)env_p[0] * 0.114f));
    m->kd = onepole_k(0.02f * exp2fx((f32)env_p[1] * 0.08f), brate);
    m->sus = (f32)env_p[2] * 0.01f;
    m->kr = onepole_k(0.015f * exp2fx((f32)env_p[3] * 0.084f), brate);
}

static void inst_touch(int i)
{
    IRun *r = &ir[i];
    const InstParams *ip = &st.ip[i];
    const uint8_t *kn = KNOBS_NOW(i);
    int k;
    if (r->at == blkno)
        return;
    if (r->at != blkno - 1u)                     /* it was silent: the knobs where they stand */
        for (k = 0; k < 4; k++)
            r->knob_s[k] = (f32)kn[k] * 0.01f;
    r->at = blkno;
    mod_run(&r->m, ip->lfo, ip->env, i == cur_i ? &lfo_live : &r->lf, i == cur_i ? &eng_rng : &lfo_rng, i == cur_i);
    for (k = 0; k < 4; k++)
        r->knob_s[k] += ((f32)kn[k] * 0.01f - r->knob_s[k]) * 0.06f;
    r->b.bend = r->m.bend;
    r->b.sr = SR;
    r->b.isr = ISR;
    r->b.lfo = r->m.lfo;
    r->b.time = blk_time;
    r->b.bpm = (f32)iclamp(st.glo[0], 40, 240);
}

/* The instrument's block() for a part, if what the instrument holds is another part's numbers or its
 * own clocks have not moved in this control block. They move once a block, whoever asks first. */
static const Blk *part_set(int part)
{
    int i = PART_INST(part), k;
    IRun *r = &ir[i];
    inst_touch(i);
    if (r->owner == part && r->adv == blkno)
        return &r->b;
    r->b.algo = PART_ALGO(part);
    /* the part's own four knobs: the eased ones on the algorithm the instrument is on, and as they
     * stand on any other */
    if (PART_ALGO(part) == (st.ip[i].algo < NALGOS(i) ? st.ip[i].algo : 0))
        for (k = 0; k < 4; k++)
            r->b.k[k] = r->knob_s[k];
    else
        for (k = 0; k < 4; k++)
            r->b.k[k] = (f32)KNOBS(i, PART_ALGO(part))[k] * 0.01f;
    if (r->m.kmod_k >= 0)
        r->b.k[r->m.kmod_k] = sat(r->b.k[r->m.kmod_k] + r->m.kmod);
    r->b.step = r->adv == blkno ? 0.0f : (f32)CTRL * ISR;
    r->adv = blkno;
    r->owner = (uint8_t)part;
    insts[i]->block(&r->b);
    return &r->b;
}

/* The instrument's block() for a closed layer: the layer's own knobs, LFO and envelope, as it was left */
static const Blk *lay_set(int part, int lay)
{
    int i = PART_INST(part), k;
    IRun *r = &ir[i];
    LRun *l = &lrun[(lay - 1) & (NLAYER - 1)];
    const Layer *ly = &st.layer[(lay - 1) & (NLAYER - 1)];
    if (l->at != blkno) {
        if (l->at != blkno - 1u) {               /* (it was silent: its LFO starts where the instrument's stands) */
            l->lf = i == cur_i ? lfo_live : r->lf;
            l->g = lay_gain(lay);
        }
        l->at = blkno;
        mod_run(&l->m, ly->lfo, ly->env, &l->lf, &lfo_rng, 0);
    }
    l->b.algo = PART_ALGO(part);
    for (k = 0; k < 4; k++)
        l->b.k[k] = (f32)ly->knob[k] * 0.01f;
    if (l->m.kmod_k >= 0)
        l->b.k[l->m.kmod_k] = sat(l->b.k[l->m.kmod_k] + l->m.kmod);
    l->b.bend = l->m.bend;
    l->b.sr = SR;
    l->b.isr = ISR;
    l->b.lfo = l->m.lfo;
    l->b.time = blk_time;
    l->b.bpm = (f32)iclamp(st.glo[0], 40, 240);
    l->b.step = r->adv == blkno ? 0.0f : (f32)CTRL * ISR;
    r->adv = blkno;
    r->owner = 0;                                /* (what the instrument holds is no part's now) */
    insts[i]->block(&l->b);
    return &l->b;
}


/* ---- a part's voices, on one core or two ------------------------------------------------------
 * A voice's control block (its envelope, its glide, the instrument's render) touches the voice and
 * its own buffer and nothing else, so the voices of a part can be made in any order, or half of them
 * on a second core at the same time. They are mixed afterwards, in voice order, so the sound is the
 * same to the bit whichever core made which. */
static f32 vt[NVOICE][CTRL];                     /* each voice's block before it is mixed */
static struct {                                  /* what a voice's block needs: set before any job is handed over */
    const InstDef *def;
    const Blk *b;
    f32 att, kd, sus, kr, kg, dt;
} vj;
static int (*c2_run)(void (*fn)(uint32_t), uint32_t arg);
static int (*c2_wait)(void);

void eng_two_cores(int (*run)(void (*fn)(uint32_t), uint32_t arg), int (*wait)(void))
{
    c2_run = run;
    c2_wait = wait;
}

static void voice_block(int i)
{
    Voice *v = &voices[i];
    f32 e0 = v->env;
    switch (v->stage) {
    case 1:
        v->env += vj.att;
        if (v->env >= 1.0f) {
            v->env = 1.0f;
            v->stage = 2;
        }
        break;
    case 2:
    case 3:
        v->env += (vj.sus - v->env) * vj.kd;
        break;
    default:
        v->env -= v->env * (fast_rel ? maxf(vj.kr, 0.3f) : vj.kr) + 0.00002f;
        if (v->env <= 0.0003f)
            v->env = 0.0f;
        break;
    }
    v->env_prev = e0;
    v->pitch += (v->target - v->pitch) * vj.kg;
    vj.def->render(v, vj.b, vt[i], CTRL);
    v->age += vj.dt;
}

static void voices_job(uint32_t mask)            /* (this is what the second core is handed) */
{
    int i;
    for (i = 0; i < NVOICE; i++)
        if (mask >> i & 1u)
            voice_block(i);
}

/* the group a voice sounds in: its part, and above it its layer while that is closed (a layer still being
 * recorded sounds as the instrument does) */
static int voice_key(const Voice *v)
{
    int l = v->lay;
    if (l && l <= NLAYER && !st.layer[l - 1].open && st.layer[l - 1].part == v->part)
        return v->part | l << 8;
    return v->part;
}

/* ---- stems (USB audio's channels 3..10) --------------------------------------------------------
 * Four stereo stems beside the master, each an instrument after its post and its own volume, before what runs
 * over the whole mix (the rumble filter, TONE, ECHO, SPACE, the metronome, the cue): 3-4, 5-6 and 7-8 are the
 * instruments of the recorded layers in the layer view's order, then the instrument in hand, one pair each; 9-10
 * everything else. Two layers on one instrument share its pair (an instrument's post runs over all its voices at
 * once). A layer no note is on any more (the loop cleared, the layers kept) takes none; a muted one keeps its
 * own. The limiter's gain goes on them as on the master, so they add up to the master without its effects. Only
 * while a computer records (the port's get gives a frame, h1_stems); otherwise none of it runs. */
#define NSTEM 4
static f32 stem_b[2][NSTEM][2][CTRL];           /* two blocks: the one heard (the limiter looks a block ahead) and the next */
static uint8_t stem_ok[2], stem_w, stem_of[NINST];
static int stem_on;
static int16_t *(*stem_get)(uint32_t off, uint32_t *run);
static uint32_t stem_stride;
static volatile uint32_t lay_used = ~0u;         /* bit l - 1: layer l has notes (eng_layer_count, the main loop) */

void eng_stems(int16_t *(*get)(uint32_t off, uint32_t *run), uint32_t stride)
{
    stem_get = get;
    stem_stride = stride;
}

static APART void stem_map(void)
{
    int i, k = 0, in;
    for (i = 0; i < NINST; i++)
        stem_of[i] = 255;
    for (i = 0; i <= NLAYER; i++) {
        in = i < NLAYER ? (layers() && st.layer[i].part && (lay_used >> i & 1u) ? PART_INST(st.layer[i].part) : -1)
                        : cur_i;                 /* (a muted layer keeps its stem: a take in a DAW keeps its tracks) */
        if (in >= 0 && stem_of[in] == 255 && k < NSTEM - 1)
            stem_of[in] = (uint8_t)k++;
    }
    for (i = 0; i < NINST; i++)
        if (stem_of[i] == 255)
            stem_of[i] = NSTEM - 1;
}

static void block(f32 *L, f32 *R)
{
    static f32 bl[CTRL], br[CTRL];               /* one instrument's voices, before its post */
    const InstParams *ip;
    const Blk *b;
    f32 dt = (f32)CTRL * ISR, bpm, dticks, kg, peak = 0.0f;
    uint32_t imask = 0;
    int i, n, inst, lay = layers(), live;

    blkno++;
    if (lay != lay_was) {                        /* LAYERS switched: what the sequencer holds, it held the other way */
        lay_was = lay;
        seq_release_all();
        layers_close();
    }
    if (pending_i >= 0) {                        /* an instrument change: fade, then swap */
        int any = 0;
        for (i = 0; i < NVOICE; i++)
            any |= voices[i].stage;
        if (!any || ++fade_blocks > 24) {
            switch_instrument(pending_i);
            pending_i = -1;
        }
    }
    ip = &st.ip[cur_i];
    live = live_part();
    if (!lay)                                    /* without LAYERS a sounding note follows the algorithm in hand */
        for (i = 0; i < NVOICE; i++)
            if (voices[i].stage && PART_INST(voices[i].part) == cur_i) {
                voices[i].part = (uint8_t)live;
                voices[i].lay = 0;
            }
    bpm = (f32)iclamp(st.glo[0], 40, 240);
    dticks = bpm * (96.0f / 60.0f) * dt;
    beat_out = fractf(beat_out + dticks * (1.0f / 96.0f));

    blk_time = run_time;
    run_time += dt;
    blk = *part_set(live);                       /* the instrument in hand: its LFO, its knobs, its block() */

    /* the panel's events and the sequencers, after the instrument has its numbers for this block:
     * a note that starts in an instrument's very first block finds them set */
    while (pending_i < 0 && ev_r != ev_w) {
        uint32_t e = evq[ev_r];
        ev_r = (ev_r + 1u) & 63u;
        handle(e);
    }
    ip = &st.ip[cur_i];                          /* (LAYERS: an event may have changed the instrument in hand) */
    live = live_part();
    if (pending_i < 0) {
        if (st.seq[0] == SEQ_STEP) step_tick(dticks);
        else loop_tick(dticks);
        free_tick(dticks);
        arp_tick(ip, dticks);
    }

    kg = st.keys[2] ? onepole_k(0.004f * exp2fx((f32)st.keys[2] * 0.08f), SR / (f32)CTRL) : 1.0f;
    lay_hush_set();
    if (lay_hushed)                              /* a layer just muted: what it was sounding lets go */
        for (i = 0; i < NVOICE; i++)
            if (voices[i].stage && voices[i].gate && voices[i].seq && lay_hush(voices[i].lay)) {
                voices[i].gate = 0;
                voices[i].stage = 4;
                mout(0, voices[i].note, 0);
            }
    fast_rel = 0;
    if (vcap < NVOICE) {                         /* the CPU guard is in: what is letting go goes in a few ms */
        for (i = n = 0; i < NVOICE; i++)
            n += voices[i].stage != 0;
        fast_rel = n > vcap;
    }

    for (n = 0; n < CTRL; n++)
        L[n] = R[n] = 0.0f;
    for (i = 0; i < NVOICE; i++)
        if (voices[i].stage)
            imask |= 1u << PART_INST(voices[i].part);
    stem_ok[stem_w] = (uint8_t)stem_on;
    if (stem_on) {
        stem_map();
        f32 *z = &stem_b[stem_w][0][0][0];
        for (n = 0; n < NSTEM * 2 * CTRL; n++)
            z[n] = 0.0f;
    }

    /* Instrument by instrument: each part's voices into the instrument's own pair of buffers, then the
     * instrument's post over the lot. An instrument holds one part's numbers at a time, so its block()
     * runs again before each part, and the part in hand goes last: its numbers are what the post and
     * the next note find. A closed layer is a group of its own (key: the part, and the layer above it),
     * on its own settings. */
    for (inst = 0; inst < NINST; inst++) {
        IRun *r = &ir[inst];
        const InstDef *def = insts[inst];
        uint16_t parts[NVOICE + 1];
        f32 fade = 1.0f, kr;
        int np = 0, p, j, in_hand = inst == cur_i, has_live = 0;
        if (imask >> inst & 1u)
            r->linger = LINGER;
        else if (!in_hand) {
            if (!r->linger) {
                r->ig = inst_gain(inst);         /* (silent: its volume is where it stands when it next sounds) */
                continue;
            }
            fade = (f32)r->linger * (1.0f / (f32)LINGER);     /* its post rings out, then it is left alone */
            r->linger--;
        }
        for (i = 0; i < NVOICE; i++) {
            int pt = voices[i].part, key;
            if (!voices[i].stage || PART_INST(pt) != inst)
                continue;
            key = voice_key(&voices[i]);
            if (in_hand && key == live) {
                has_live = 1;
                continue;
            }
            for (j = 0; j < np && parts[j] != key; j++)
                ;
            if (j == np)
                parts[np++] = (uint16_t)key;
        }
        if (has_live)
            parts[np++] = (uint16_t)live;
        for (n = 0; n < CTRL; n++)
            bl[n] = br[n] = 0.0f;
        b = &r->b;
        for (p = 0; p < np; p++) {
            uint32_t mine = 0, other = 0;
            int nv = 0, gl_lay = parts[p] >> 8;
            const Mod *m = gl_lay ? &lrun[(gl_lay - 1) & (NLAYER - 1)].m : &r->m;
            f32 lg = 1.0f;
            b = gl_lay ? lay_set(parts[p] & 255, gl_lay) : part_set(parts[p]);
            if (gl_lay) {                        /* the layer's own volume, eased (~5 ms) */
                LRun *lr = &lrun[(gl_lay - 1) & (NLAYER - 1)];
                f32 t = lay_gain(gl_lay);
                lr->g += (t - lr->g) * 0.15f;
                if (absf(t - lr->g) < 0.0005f)
                    lr->g = t;
                lg = lr->g;
            }
            kr = pending_i >= 0 ? 0.3f : m->kr;
            for (i = 0; i < NVOICE; i++)         /* every other voice of the part to the second core, if there is one */
                if (voices[i].stage && voice_key(&voices[i]) == parts[p]) {
                    if (c2_run && (nv & 1))
                        other |= 1u << i;
                    else
                        mine |= 1u << i;
                    nv++;
                }
            vj.def = def;
            vj.b = b;
            vj.att = m->att;
            vj.kd = m->kd;
            vj.sus = m->sus;
            vj.kr = kr;
            vj.kg = kg;
            vj.dt = dt;
            if (other) {
                int on2 = c2_run(voices_job, other);
                voices_job(mine);
                if (!on2 || c2_wait() != 0)      /* it did not take them, or it dropped them: here, then */
                    voices_job(other);
            } else
                voices_job(mine);
            for (i = 0; i < NVOICE; i++) {
                Voice *v = &voices[i];
                const f32 *tmp = vt[i];
                f32 e0, e1, g, gl, gr, de, pan, pl, pr, s = 0.0f, tail, vk, y;
                if (!((mine | other) >> i & 1u))
                    continue;
                e0 = v->env_prev;
                e1 = v->env;
                g = def->gain * m->vol * algo_gain[inst][PART_ALGO(parts[p])] * v->vamp * lg;
                pan = clampf(v->pan + m->panmod, -1.0f, 1.0f);
                pl = 1.0f - maxf(0.0f, pan) * 0.8f;
                pr = 1.0f + minf(0.0f, pan) * 0.8f;
                gl = g * pl;
                gr = g * pr;
                de = (e1 - e0) * (1.0f / (f32)CTRL);
                vk = v->vk;
                if (v->declick) {                /* taken over: the new note starts where the old one was */
                    f32 s0 = tmp[0] * e0;
                    if (vk < 1.0f) {
                        f32 q = v->vlp[0] + (s0 - v->vlp[0]) * vk;
                        s0 = v->vlp[1] + (q - v->vlp[1]) * vk;
                    }
                    v->tail = v->last - s0 * g;
                    v->declick = 0;
                }
                tail = v->tail;
                if (vk >= 1.0f && tail == 0.0f)
                    for (n = 0; n < CTRL; n++) {
                        s = tmp[n] * e0;
                        e0 += de;
                        bl[n] += s * gl;
                        br[n] += s * gr;
                    }
                else {
                    f32 lp = v->vlp[0], lp2 = v->vlp[1];
                    for (n = 0; n < CTRL; n++) {
                        s = tmp[n] * e0;
                        e0 += de;
                        if (vk < 1.0f) {
                            lp += (s - lp) * vk;
                            lp2 += (lp - lp2) * vk;
                            s = lp2;
                        }
                        y = tail;
                        tail *= 0.985f;          /* (about 1.5 ms) */
                        bl[n] += s * gl + y * pl;
                        br[n] += s * gr + y * pr;
                    }
                    v->vlp[0] = lp;
                    v->vlp[1] = lp2;
                    if (absf(tail) < 1.0e-6f)
                        tail = 0.0f;
                    v->tail = tail;
                }
                v->last = s * g + tail;
                if (v->stage == 4 && v->env <= 0.0f) {
                    v->stage = 0;
                    v->last = v->tail = v->vlp[0] = v->vlp[1] = 0.0f;
                }
            }
        }
        if (in_hand && !has_live)
            b = part_set(live);
        if (def->post)
            def->post(b, bl, br, CTRL);
        {
            /* Into the mix at the instrument's own volume, after its post: turned down it is the same
             * sound, quieter. The gain eases (about 10 ms) and ramps across the block, so a turn of the
             * knob does not zip. At full, and that is how every instrument comes, the samples go in
             * untouched. */
            f32 tg = inst_gain(inst), g0 = r->ig, g1 = g0 + (tg - g0) * 0.07f;
            if (absf(tg - g1) < 0.0004f)
                g1 = tg;
            r->ig = g1;
            if (fade < 1.0f || g0 != 1.0f || g1 != 1.0f) {
                f32 g = g0 * fade, dg = (g1 - g0) * fade * (1.0f / (f32)CTRL);
                for (n = 0; n < CTRL; n++) {
                    bl[n] *= g;
                    br[n] *= g;
                    g += dg;
                }
            }
            for (n = 0; n < CTRL; n++) {
                L[n] += bl[n];
                R[n] += br[n];
            }
            if (stem_on) {                       /* and into its stem (USB audio) */
                f32 *sl = stem_b[stem_w][stem_of[inst]][0], *sr = stem_b[stem_w][stem_of[inst]][1];
                for (n = 0; n < CTRL; n++) {
                    sl[n] += bl[n];
                    sr[n] += br[n];
                }
            }
        }
    }

    /* Nothing below hearing goes on from here: an offset or a rumble is not a sound, but it is level, and it
     * would lean on the limiter at the end. */
    for (n = 0; n < CTRL; n++) {
        f32 x = L[n], y = R[n];
        sub_l += SUB_K * (x - sub_l);
        sub_r += SUB_K * (y - sub_r);
        if (absf(sub_l) < 1.0e-12f) sub_l = 0.0f;
        if (absf(sub_r) < 1.0e-12f) sub_r = 0.0f;
        L[n] = x - sub_l;
        R[n] = y - sub_r;
    }

    {
        uint32_t w = scope_w;
        for (n = 0; n < CTRL; n++) {
            f32 m = (L[n] + R[n]) * 0.5f, am = absf(m);
            scope[w & (SCOPE_N - 1)] = m;
            w++;
            if (am > peak)
                peak = am;
        }
        scope_w = w;
    }
    {
        f32 lv = level_out;
        lv += (peak - lv) * (peak > lv ? 0.5f : 0.02f);
        level_out = lv;
    }

    fx_block(L, R, &st.ip[cur_i], bpm);

    if (cue_p)
        cue_block(L, R);
    if (click_env > 0.001f) {
        for (n = 0; n < CTRL; n++) {
            f32 c;                               /* wood, not a beep: the pitch drops as it lands, and it is gone in a twentieth of a second */
            click_lp += (noise_s(&click_rng) - click_lp) * 0.2f;
            c = (tsin(click_ph) + 0.22f * click_pe * tsin(click_ph2) + 0.5f * click_pe * click_lp) * click_env * click_amp;
            click_ph += click_f * (1.0f + 1.3f * click_pe) * ISR;
            if (click_ph >= 1.0f) click_ph -= 1.0f;
            click_ph2 += click_f * 2.76f * ISR;
            if (click_ph2 >= 1.0f) click_ph2 -= 1.0f;
            click_pe *= 0.9972f;
            click_env *= 0.99925f;
            L[n] += c;
            R[n] += c;
        }
    }
}

#ifdef H1_FUZZ
uint32_t h1_fuzz_nan;
#endif

/* The last thing before the converter. Up to half of full scale it does nothing at all, so a note or a
 * chord at an ordinary level goes out as it was made; above that it bends over into full scale and never
 * passes it. */
static inline f32 limit(f32 x)
{
    f32 a = absf(x);
    if (!(a > 0.5f))                             /* (and what is not a number is not above it: softclip makes it 0) */
        return a <= 0.5f ? x : 0.0f;
    a = 0.5f + 0.5f * softclip((a - 0.5f) * 2.0f);
    return x < 0.0f ? -a : a;
}

/* The limiter. Eight voices of most instruments, a chord with the echo up, come to two or three times full
 * scale, and the soft clip at the end then bends every peak: the crackle and the fuzz on loud chords. So the
 * mix is turned down for as long as it is too loud, and the soft clip only ever sees what is under LIM_T.
 * It looks one control block (0.7 ms) ahead: the gain that block needs is reached by the time it is heard,
 * in a straight line across the block before it, so no sample passes LIM_T and the gain never jumps. It
 * comes back up in about 100 ms. Under LIM_T (a note or two at an ordinary level) it does nothing at all. */
#define LIM_T 0.7f
#define LIM_REL 0.0072f                          /* a control block's step back towards 1 (about 100 ms) */
static f32 lim_g = 1.0f, lim_need = 1.0f;

static f32 lim_peak(const f32 *l, const f32 *r)
{
    f32 p = 0.0f;
    int i;
    for (i = 0; i < CTRL; i++) {
        f32 a = absf(l[i]), b = absf(r[i]);
        if (a > p) p = a;
        if (b > p) p = b;
    }
    return p > LIM_T ? LIM_T / p : 1.0f;         /* (not a number: 1, and limit() makes it silence) */
}

static void lim_block(f32 *L, f32 *R)
{
    static f32 NL[CTRL], NR[CTRL];               /* the next block: the one the gain looks at */
    f32 need_now = lim_need, g0 = lim_g, g1, dg;
    int i;
    for (i = 0; i < CTRL; i++) {
        L[i] = NL[i];
        R[i] = NR[i];
    }
    stem_w ^= 1u;                                /* (the stems: the block heard is the other one) */
    block(NL, NR);
    lim_need = lim_peak(NL, NR);
    g1 = g0 + (1.0f - g0) * LIM_REL;
    if (g1 > need_now) g1 = need_now;            /* this block's own peaks, and the next's */
    if (g1 > lim_need) g1 = lim_need;
    if (g1 > 0.99999f && g0 > 0.99999f) {
        lim_g = 1.0f;
        return;
    }
    dg = (g1 - g0) * (1.0f / (f32)CTRL);
    if (stem_ok[stem_w ^ 1u]) {                  /* the stems heard with this block: the same gain */
        int k;
        for (k = 0; k < 2 * NSTEM; k++) {
            f32 *x = stem_b[stem_w ^ 1u][k >> 1][k & 1], g = g0;
            for (i = 0; i < CTRL; i++) {
                g += dg;
                x[i] *= g;
            }
        }
    }
    for (i = 0; i < CTRL; i++) {
        g0 += dg;
        L[i] *= g0;
        R[i] *= g0;
    }
    lim_g = g1;
}

/* Silence, for what stalls the sound (a flash erase, ~17 ms): no voice, and the output under hearing for half a
 * second, or no voice for six (COZY's crackle never stops, and a long echo at the top of ECHO hangs on faintly:
 * by then a stall in it is lost in it). */
static volatile uint32_t quiet_n, novoice_n;     /* control blocks in a row: the output silent, no voice sounding */
int eng_silent(void)
{
    return !eng_voices() && (quiet_n > (uint32_t)(SR * 0.5f) / CTRL || novoice_n > (uint32_t)(SR * 6.0f) / CTRL);
}

/* the stems of frames pos.. of the block heard, take of them, into the port's channels from frame at of this render */
static APART void stems_out(uint32_t at, uint32_t pos, uint32_t take)
{
    const f32 (*b)[2][CTRL] = stem_b[stem_w ^ 1u];
    uint32_t run = 0, i;
    int16_t *u = 0;
    if (!stem_ok[stem_w ^ 1u])
        return;                                  /* (the channels start at 0: silence) */
    for (i = 0; i < take; i++, run--, u += stem_stride) {
        int k;
        if (!run && !(u = stem_get(at + i, &run)))
            return;
        for (k = 0; k < 2 * NSTEM; k++) {
            f32 x = b[k >> 1][k & 1][pos + i] * 29491.0f;   /* 0.9 of full scale, as the master */
            int32_t v = (int32_t)(x < 0.0f ? x - 0.5f : x + 0.5f);
            u[k] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
        }
    }
}

void eng_render(int32_t *lr, uint32_t n)
{
    static f32 L[CTRL], R[CTRL];
    static uint32_t have, pos;                   /* frames of a block not yet handed out */
    uint32_t at = 0, run;
    stem_on = stem_get && stem_get(0, &run) != 0;   /* a computer records */
    while (n) {
        uint32_t take, i;
        if (!have) {
            f32 pk = 0.0f;
            lim_block(L, R);
            have = CTRL;
            pos = 0;
            for (i = 0; i < CTRL; i++)
                pk = maxf(pk, maxf(absf(L[i]), absf(R[i])));
            quiet_n = pk < 1.5e-4f ? quiet_n + 1u : 0u;     /* (about -76 dB: an echo's tail is gone to the ear) */
            novoice_n = eng_voices() ? 0u : novoice_n + 1u;
        }
        take = have < n ? have : n;
        if (stem_on)
            stems_out(at, pos, take);
        at += take;
        for (i = 0; i < take; i++) {
#ifdef H1_FUZZ
            if (L[pos + i] != L[pos + i] || R[pos + i] != R[pos + i])
                h1_fuzz_nan++;                   /* (tests/fuzz.c: a sound that stopped being numbers) */
#endif
            lr[0] = (int32_t)(limit(L[pos + i]) * 7549747.0f);        /* 0.9 of 24-bit full scale */
            lr[1] = (int32_t)(limit(R[pos + i]) * 7549747.0f);
            lr += 2;
        }
        pos += take;
        have -= take;
        n -= take;
    }
}

SMALL void eng_init(uint32_t sample_rate)
{
    int i;
    if (sample_rate) {                           /* 0: keep the rate (a state load) */
        SR = (f32)sample_rate;
        ISR = 1.0f / SR;
    }
    dsp_init();
    fx_init(SR);
    {
        uint8_t *p = (uint8_t *)ir;
        uint32_t k;
        for (k = 0; k < sizeof ir; k++)
            p[k] = 0;
    }
    for (i = 0; i < NINST; i++)
        ir[i].ig = inst_gain(i);
    {
        uint8_t *p = (uint8_t *)lrun;
        uint32_t k;
        for (k = 0; k < sizeof lrun; k++)
            p[k] = 0;
    }
    for (i = 0; i < 128; i++)
        rec_part[i] = rec_lay[i] = 0;
    for (i = 0; i < NLAYER; i++)                 /* (no take is going on) */
        if (st.layer[i].open) {
            layer_snap(&st.layer[i]);
            st.layer[i].open = 0;
        }
    lim_g = lim_need = 1.0f;
    lay_was = layers();
    switch_instrument(st.inst < NINST ? st.inst : 0);
    for (i = 0; i < NVOICE; i++) {               /* nothing sounds and nothing runs: as the power coming on leaves it */
        voices[i].serial = 0;
        voices[i].stage = 0;
        voices[i].gate = 0;
        voices[i].seq = 0;
    }
    loop_state = LOOP_EMPTY;
    nlev = 0;
    rec_forget();
    loop_pos = 0.0f;
    loop_held[0] = loop_held[1] = loop_held[2] = loop_held[3] = 0;
    latched[0] = latched[1] = latched[2] = latched[3] = 0;
    latched_midi[0] = latched_midi[1] = latched_midi[2] = latched_midi[3] = 0;
    phys_down = nheld = arp_ncur = 0;
    arp_acc = 1e9f;
    step_playing = step_write = step_cursor = step_at = 0;
    step_nsnd = step_gesture = 0;
    step_acc = step_off = 0.0f;
    free_on = 0;
    click_at = -1;
    ev_r = ev_w;                                 /* (and whatever the panel had asked for is not done) */
    cue_p = 0;
    pending_i = -1;
    fade_blocks = 0;
}

/* ---- for the main loop ------------------------------------------------------------------ */
void eng_snapshot(Vis *vis)
{
    int i;
    for (i = 0; i < NVOICE; i++) {
        const Voice *v = &voices[i];
        vis->v[i].on = v->stage != 0;
        vis->v[i].gate = v->gate;
        vis->v[i].note = v->note;
        vis->v[i].vel = v->vel;
        vis->v[i].seq = v->seq;
        vis->v[i].serial = v->serial;
        vis->v[i].pitch = v->pitch;
        vis->v[i].env = v->env;
        vis->v[i].age = v->age;
    }
    for (i = 0; i < 4; i++)
        vis->k[i] = blk.k[i];
    vis->level = level_out;
    vis->lfo = lfo_out;
    vis->beat = beat_out;
    vis->notes = note_count;
    vis->last_pitch = last_pitch;
    vis->hand = hand_count;
    vis->hand_pitch = hand_pitch;
    if (layers())                                /* the picture is the instrument in hand: its own notes move it */
        for (i = 0; i < NVOICE; i++)
            if (PART_INST(voices[i].part) != cur_i)
                vis->v[i].on = 0;
    vis->scope = scope;
    vis->scope_pos = (int)(scope_w & (SCOPE_N - 1));
}

uint32_t eng_sounding_lo(void)
{
    uint32_t m = 0;
    int i, base = 53 + 12 * st.octave + opt[OPT_TRANS] - TRANS_MID;   /* (TRANSPOSE: still on the key played) */
    for (i = 0; i < NVOICE; i++)
        if (voices[i].stage && voices[i].gate) {
            int k = (int)voices[i].note - base;
            if (k >= 0 && k < H1_NKEYS)
                m |= 1u << k;
        }
    return m;
}

/* main loop: is anything recorded that was played on this instrument (LAYERS: what holding REC would clear) */
int eng_seq_has(int inst)
{
    int i, j, ne = nlev;
    if (st.seq[0] == SEQ_STEP) {
        for (i = 0; i < NSTEP; i++)
            for (j = 0; j < NSNOTE; j++)
                if (st.steps[i][j] && tag_inst(st.step_part[i][j]) == inst)
                    return 1;
        return 0;
    }
    for (i = 0; i < ne && i < NLEV; i++)
        if (tag_inst(lev[i].part) == inst)
            return 1;
    return 0;
}

uint32_t eng_voices(void)
{
    uint32_t n = 0;
    int i;
    for (i = 0; i < NVOICE; i++)
        n += voices[i].stage != 0;
    return n;
}

/* ---- the loop, kept across a power-off (h1_loop) ----------------------------------------------
 * "H1LP", a version byte, the loop's length in ticks and its events, eight bytes each, written and read a
 * byte at a time (the blob may sit at any address). The layers the events name are in the state. */
#define LOOP_MAGIC 0x504C3148u                   /* "H1LP" */
#define LOOP_HEAD 12u

static void put16(uint8_t *d, uint32_t v) { d[0] = (uint8_t)v; d[1] = (uint8_t)(v >> 8); }
static uint32_t get16(const uint8_t *b) { return (uint32_t)b[0] | (uint32_t)b[1] << 8; }

/* The loop as it is saved: the header, then the events as they lie in RAM, so no copy of 16 KB is kept for
 * it. An event's bytes are tick (little-endian), note, on, part, layer, velocity, 0; bit 7 of on (played
 * this pass) is left as it is, since eng_loop_load masks it.
 * Main loop only. The events change only while a take records; bit 7 of on may still drop at the next
 * round after one, which a load ignores. */
const uint8_t *eng_loop_blob(uint32_t *size)
{
    int ne = nlev, i;
    uint8_t *h = loop_mem.head;
    for (i = 0; i < ne; i++)                     /* a layer let go since is no layer (as it plays already) */
        lev[i].lay = (uint8_t)layer_of(lev[i].lay, lev[i].part);
    put16(h, LOOP_MAGIC & 0xFFFFu);
    put16(h + 2, LOOP_MAGIC >> 16);
    h[4] = 2;                                    /* the version: 2 since parts are counted (PART), 1 before */
    h[5] = 0;
    put16(h + 6, (uint32_t)ne);
    put16(h + 8, (uint32_t)loop_len);
    put16(h + 10, 0);
    *size = LOOP_HEAD + (uint32_t)ne * 8u;
    return h;
}

/* After the state (h1_state_load), before the audio starts. Anything that does not read as a loop is refused
 * and the loop stays empty; every value is brought into range. */
int eng_loop_load(const uint8_t *b, uint32_t n)
{
    uint32_t ne, len, i, k = 0;
    if (!b || n < LOOP_HEAD || get16(b) != (LOOP_MAGIC & 0xFFFFu) || get16(b + 2) != (LOOP_MAGIC >> 16) || b[4] < 1 || b[4] > 2)
        return -1;
    ne = get16(b + 6);
    len = get16(b + 8);
    if (ne > NLEV || n < LOOP_HEAD + ne * 8u || len < 24u || len > 128u * 480u)
        return -1;
    loop_release();                              /* (what the loop it replaces held: let go) */
    for (i = 0; i < ne; i++) {
        const uint8_t *e = b + LOOP_HEAD + i * 8u;
        uint32_t tick = get16(e);
        if (tick >= len)
            continue;
        {                                        /* in order of tick: 0.2.2 and 0.3.0's first saves kept them as played */
            LEv *d;
            nlev = (int)k;
            d = lev_insert((uint16_t)tick);
            d->tick = (uint16_t)tick;
            d->note = e[2] & 127u;
            d->on = e[3] & 1u;
            d->part = b[4] == 1 ? PART_V8(e[4]) : part_ok(e[4]) ? e[4] : 0u;
            if (d->part && e[5] >= 1 && e[5] <= NLAYER && !st.layer[e[5] - 1].part) {
                st.layer[e[5] - 1].part = d->part;  /* a layer the state lost (saved apart, before 0.3.3): back, at */
                layer_snap(&st.layer[e[5] - 1]);    /* full and heard, on the part's settings as they are now */
            }
            d->lay = (uint8_t)layer_of(e[5], d->part);
            d->vel = e[6] & 127u;
            d->rsv = 0;
        }
        k++;
    }
    nlev = (int)k;
    rec_forget();
    loop_len = (f32)len;
    loop_pos = 0.0f;
    loop_state = nlev ? LOOP_IDLE : LOOP_EMPTY;
    loop_gen++;
    layers_tidy();                               /* (layers a state kept that this loop has no note on) */
    return 0;
}

uint32_t eng_loop_gen(void) { return loop_gen; }

int eng_loop_taking(void)
{
    int s = loop_state;
    return s == LOOP_REC || s == LOOP_DUB;
}

int eng_loop_quiet(void)
{
    int s = loop_state;
    return s == LOOP_EMPTY || s == LOOP_IDLE;
}

/* main loop: the layers notes are on (in the loop or the steps, whichever is showing) */
int eng_layer_count(void)
{
    uint8_t used[NLAYER];
    uint32_t mask = 0;
    int i, j, n = 0, ne = nlev;
    for (i = 0; i < NLAYER; i++)
        used[i] = 0;
    if (st.seq[0] == SEQ_STEP) {
        for (i = 0; i < NSTEP; i++)
            for (j = 0; j < NSNOTE; j++)
                if (st.steps[i][j] && st.step_lay[i][j] && st.step_lay[i][j] <= NLAYER)
                    used[st.step_lay[i][j] - 1] = 1;
    } else
        for (i = 0; i < ne && i < NLEV; i++)
            if (lev[i].lay && lev[i].lay <= NLAYER)
                used[lev[i].lay - 1] = 1;
    for (i = 0; i < NLAYER; i++) {
        n += used[i];
        mask |= (uint32_t)used[i] << i;
    }
    lay_used = mask;                             /* (the stems: only a layer with notes takes one) */
    return n;
}

/* main loop: is a take going into a layer now (1..NLAYER), 0 if not */
int eng_layer_rec(void)
{
    int i;
    for (i = 0; i < NLAYER; i++)
        if (st.layer[i].open)
            return i + 1;
    return 0;
}
int eng_loop_used(void) { return nlev; }
uint32_t eng_loop_full(void) { return loop_full; }
