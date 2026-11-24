/* h1 - the public calls that are not the UI's or the engine's own: start-up and saved state. */
#include "internal.h"

void knobs_default(int inst, int algo)
{
    int a, k;
    for (a = 0; a < NALGO; a++)
        if (algo < 0 || a == algo)
            for (k = 0; k < 4; k++)
                KNOBS(inst, a)[k] = insts[inst]->def.knob0[k];
}

static void inst_default(int i)
{
    st.ip[i] = insts[i]->def;
    knobs_default(i, -1);
    st.ink[i][0] = insts[i]->ink[0];
    st.ink[i][1] = insts[i]->ink[1];
    st.ink[i][2] = insts[i]->ink[2];
}

static void defaults(void)
{
    int i;
    uint8_t *p = (uint8_t *)&st;
    for (i = 0; i < (int)sizeof st; i++)
        p[i] = 0;
    st.magic = H1_MAGIC;
    st.version = H1_VERSION;
    st.glo[0] = 112;                             /* tempo */
    st.glo[2] = 50;                              /* tune: centre */
    st.glo[3] = 100;                             /* bright */
    st.seq[1] = 1;                               /* 2 bars */
    st.seq[3] = 1;                               /* the loop snaps to 1/16 */
    st.seq[4] = 1;                               /* steps of 1/16 */
    st.seq[6] = 1;                               /* LAYERS: record, change instrument, record over it */
    st.ninst = NINST;
    for (i = 0; i < NINST; i++)
        inst_default(i);
}

uint8_t opt[NOPT];
static const uint8_t OPT_MAX[NOPT] = {16, 2, 1, 1, 1, 1, 2, 1};

/* rsv[0]: the channel (0 all, 1..16), PEDAL << 5, WHEEL << 6, LEDS << 7. rsv[1]: BEND, ANIM << 2, EGGS << 3,
 * SCREEN << 4. */
SMALL void opt_unpack(void)
{
    int i;
    opt[OPT_CHAN] = st.rsv[0] & 31u;
    opt[OPT_PEDAL] = st.rsv[0] >> 5 & 1u;
    opt[OPT_WHEEL] = st.rsv[0] >> 6 & 1u;
    opt[OPT_LEDS] = st.rsv[0] >> 7 & 1u;
    opt[OPT_BEND] = st.rsv[1] & 3u;
    opt[OPT_ANIM] = st.rsv[1] >> 2 & 1u;
    opt[OPT_EGGS] = st.rsv[1] >> 3 & 1u;
    opt[OPT_SCREEN] = st.rsv[1] >> 4 & 3u;
    for (i = 0; i < NOPT; i++)
        if (opt[i] > OPT_MAX[i])
            opt[i] = 0;
    opt_pack();
}

SMALL void opt_pack(void)
{
    st.rsv[0] = (uint8_t)(opt[OPT_CHAN] | opt[OPT_PEDAL] << 5 | opt[OPT_WHEEL] << 6 | opt[OPT_LEDS] << 7);
    st.rsv[1] = (uint8_t)(opt[OPT_BEND] | opt[OPT_ANIM] << 2 | opt[OPT_EGGS] << 3 | opt[OPT_SCREEN] << 4);
    st.rsv[2] = st.rsv[3] = 0;
}

SMALL void h1_init(uint32_t sample_rate)
{
    defaults();
    opt_unpack();
    eng_init(sample_rate);
    ui_init();
}

void h1_frame(uint32_t now_ms) { ui_frame(now_ms); }
void h1_render(int32_t *lr, uint32_t n) { eng_render(lr, n); }

const void *h1_state(uint32_t *size)
{
    if (size)
        *size = (uint32_t)sizeof st;
    return &st;
}

/* Take a saved blob. Call it right after h1_init, before the audio starts. Anything that does not
 * look like a state of this version or of one it can read (4, 5, 6 and 8) is refused, and every value is clamped to
 * its range.
 *
 * The blob is bytes at any address: it is copied out a byte at a time and nothing is read from it as
 * a word. (A byte buffer may sit at an odd address, and the FM-1's CPU stops on a word read from one:
 * reading the magic straight out of the blob kept the first h1 from starting once it had saved.) */
typedef char v4_is_664_bytes[H1_VERSION_4_SIZE == 664u ? 1 : -1];   /* what the FM-1s out there hold: these */
typedef char v5_is_920_bytes[H1_VERSION_5_SIZE == 920u ? 1 : -1];   /* must not move with the layout */
typedef char v6_is_1880_bytes[H1_VERSION_6_SIZE == 1880u ? 1 : -1];
typedef char v8_is_3160_bytes[H1_VERSION_8_SIZE == 3160u ? 1 : -1];
typedef char v9_is_3344_bytes[sizeof(H1State) == 3344u ? 1 : -1];
typedef char old_head_is_the_same[offsetof(H1Old, ip) == offsetof(H1State, ip) ? 1 : -1];
typedef char the_slots_hold_the_instruments[NINST <= NSLOT && NSLOT * NALGO < 255 ? 1 : -1];
typedef char meta_holds_the_layers[H1_META_N == 6 + 3 * NLAYER ? 1 : -1];
typedef char v9_fits_a_store_object[sizeof(H1State) <= 3840u ? 1 : -1];
static void bytes(void *to, const uint8_t *from, uint32_t n)
{
    uint8_t *d = (uint8_t *)to;
    while (n--)
        *d++ = *from++;
}

SMALL int h1_state_load(const void *blob, uint32_t size)
{
    const uint8_t *b = (const uint8_t *)blob;
    uint8_t *d = (uint8_t *)&st;
    uint32_t magic = 0, was, n;
    int i, j, pg, a, had;
    if (!blob)
        return -1;
    was = size == sizeof st ? H1_VERSION : size == H1_VERSION_8_SIZE ? 8u : size == H1_VERSION_6_SIZE ? 6u
        : size == H1_VERSION_5_SIZE ? 5u : size == H1_VERSION_4_SIZE ? 4u : 0u;
    if (!was)
        return -1;
    for (n = 0; n < 4u; n++)
        ((uint8_t *)&magic)[n] = b[n];
    if (magic != H1_MAGIC || b[offsetof(H1State, version)] != was)
        return -1;                               /* (refused: nothing has been touched) */
    if (was == H1_VERSION) {
        bytes(d, b, (uint32_t)sizeof st);
        had = st.ninst < NINST ? st.ninst : NINST;
    } else {
        /* An older state, of fourteen instruments of eight algorithms: each of its tables goes to the start
         * of this one's, and its parts are counted now. Before version 8 a step's notes have no velocity
         * (they play at full, as they did) and no layer (they follow their part, as they did), and LAYERS
         * comes on: recording over a loop on another instrument is what h1 does now. Before version 6 a
         * step held four notes, with in version 5 the part of each after all the notes, and an
         * instrument's algorithms shared four knobs, so every algorithm starts from where those stood and
         * nothing sounds different after the update. The ninth algorithm (HOOVER) starts as new. */
        const uint8_t *notes = b + offsetof(H1Old, steps);
        for (n = 0; n < sizeof st; n++)
            d[n] = 0;
        bytes(d, b, (uint32_t)offsetof(H1Old, ip));
        bytes(st.ip, b + offsetof(H1Old, ip), H1_OLD_NINST * (uint32_t)sizeof(InstParams));
        bytes(st.ink, b + offsetof(H1Old, ink), H1_OLD_NINST * 3u);
        if (was >= 6u) {
            bytes(st.steps, notes, (uint32_t)sizeof st.steps);
            bytes(st.step_part, b + offsetof(H1Old, step_part), (uint32_t)sizeof st.step_part);
            for (i = 0; i < (int)H1_OLD_NINST; i++)
                for (a = 0; a < (int)H1_OLD_NALGO; a++)
                    bytes(st.knob[i][a], b + offsetof(H1Old, knob) + ((uint32_t)i * H1_OLD_NALGO + (uint32_t)a) * 4u, 4u);
        } else {
            const uint8_t *parts = was >= 5u ? notes + NSTEP * H1_OLD_SNOTE : 0;
            for (i = 0; i < NSTEP; i++)
                for (j = 0; j < NSNOTE; j++) {
                    st.steps[i][j] = j < (int)H1_OLD_SNOTE ? notes[(uint32_t)i * H1_OLD_SNOTE + (uint32_t)j] : 0;
                    st.step_part[i][j] = parts && j < (int)H1_OLD_SNOTE ? parts[(uint32_t)i * H1_OLD_SNOTE + (uint32_t)j] : 0;
                }
            for (i = 0; i < (int)H1_OLD_NINST; i++)
                for (a = 0; a < (int)H1_OLD_NALGO; a++)
                    for (j = 0; j < 4; j++)
                        st.knob[i][a][j] = st.ip[i].knob0[j];
        }
        if (was >= 8u) {
            bytes(st.step_vel, b + offsetof(H1Old, step_vel), (uint32_t)sizeof st.step_vel);
            bytes(st.step_lay, b + offsetof(H1Old, step_lay), (uint32_t)sizeof st.step_lay);
            bytes(st.layer, b + offsetof(H1Old, layer), (uint32_t)sizeof st.layer);
        } else
            st.seq[6] = 1;
        for (i = 0; i < NSTEP; i++)
            for (j = 0; j < NSNOTE; j++)
                st.step_part[i][j] = PART_V8(st.step_part[i][j]);
        for (i = 0; i < NLAYER; i++)
            st.layer[i].part = PART_V8(st.layer[i].part);
        for (i = 0; i < (int)H1_OLD_NINST && i < NINST; i++)
            for (a = (int)H1_OLD_NALGO; a < NALGO; a++)
                knobs_default(i, a);
        had = (int)H1_OLD_NINST < NINST ? (int)H1_OLD_NINST : NINST;
    }
    for (i = had; i < NSLOT; i++) {              /* instruments that came after the state was saved: as new */
        uint8_t *p = (uint8_t *)&st.ip[i];
        for (n = 0; n < sizeof st.ip[i]; n++)
            p[n] = 0;
        for (n = 0; n < 3u; n++)
            st.ink[i][n] = 0;
        for (a = 0; a < NALGO; a++)
            for (j = 0; j < 4; j++)
                st.knob[i][a][j] = 0;
        if (i < NINST)
            inst_default(i);
    }
    opt_unpack();                                /* (0.3.0 and before kept zeros there: every setting as it was) */
    st.ninst = NINST;
    st.version = H1_VERSION;
    if (st.inst >= NINST) st.inst = 0;
    st.octave = (int8_t)iclamp(st.octave, -2, 2);
    st.hud &= 1;
    for (i = 0; i < NINST; i++) {
        uint8_t keep = st.inst;
        st.inst = (uint8_t)i;
        for (j = 0; j < 4; j++) {
            if (st.ip[i].knob0[j] > 100) st.ip[i].knob0[j] = 100;
            for (a = 0; a < NALGO; a++)
                if (KNOBS(i, a)[j] > 100) KNOBS(i, a)[j] = 100;
        }
        if (st.ip[i].algo >= NALGOS(i)) st.ip[i].algo = 0;
        if (st.ip[i].mix[IP_CUT] > 100) st.ip[i].mix[IP_CUT] = 0;           /* (not a volume: full, rather than an instrument gone quiet) */
        for (pg = 1; pg < NPAGE; pg++)
            for (j = 0; j < 4; j++) {
                uint8_t *v = page_value(pg, j);
                *v = (uint8_t)iclamp(*v, page_param(pg, j)->min, page_param(pg, j)->max);
            }
        st.inst = keep;
    }
    st.seq[0] &= 1;
    if (st.seq[1] > 7) st.seq[1] = 1;              /* bars: 1 to 128 (0.2.2 had 1 to 8) */
    if (st.seq[3] > 2) st.seq[3] = 1;
    if (st.seq[4] > 2) st.seq[4] = 1;
    if (st.seq[5] > 2) st.seq[5] = CLICK_REC;
    st.seq[6] &= 1;
    for (i = 0; i < NSTEP; i++)
        for (j = 0; j < NSNOTE; j++) {
            if (st.steps[i][j] > 128) st.steps[i][j] = 0;
            if (st.step_vel[i][j] > 127) st.step_vel[i][j] = 127;
            if (st.step_lay[i][j] > NLAYER) st.step_lay[i][j] = 0;
            if (!part_ok(st.step_part[i][j])) st.step_part[i][j] = 0;
        }
    for (i = 0; i < NLAYER; i++) {
        Layer *l = &st.layer[i];
        if (!part_ok(l->part)) {
            uint8_t *p = (uint8_t *)l;
            for (n = 0; n < sizeof *l; n++)
                p[n] = 0;
            continue;
        }
        l->open = 0;
        if (l->rsv[0] > 100) l->rsv[0] = 0;      /* how far the layer is turned down */
        l->rsv[1] &= LAY_MUTE | LAY_SOLO;
        for (j = 0; j < 4; j++) {
            if (l->knob[j] > 100) l->knob[j] = 100;
            if (l->env[j] > 100) l->env[j] = 100;
            l->lfo[j] = (uint8_t)iclamp(l->lfo[j], page_param(PG_LFO, j)->min, page_param(PG_LFO, j)->max);
        }
    }
    eng_init(0);
    ui_init();
    return 0;
}

int h1_instrument(void) { return st.inst; }
const char *h1_instrument_name(int i) { return i >= 0 && i < NINST ? insts[i]->name : ""; }
int h1_algorithm(void) { return st.ip[st.inst].algo; }
int h1_page(void) { return ui_page(); }
int h1_layers(void) { return st.seq[6] == 1; }
int h1_instrument_volume(int inst) { return inst >= 0 && inst < NINST ? INST_VOL(inst) : 0; }
void h1_two_cores(int (*run)(void (*fn)(uint32_t), uint32_t arg), int (*wait)(void)) { eng_two_cores(run, wait); }
int h1_instrument_count(void) { return NINST; }
const char *h1_knob_name(int inst, int k) { return inst >= 0 && inst < NINST && k >= 0 && k < 4 ? insts[inst]->knob[k] : ""; }
const char *h1_algo_name(int inst, int a) { return inst >= 0 && inst < NINST && a >= 0 && a < NALGOS(inst) ? insts[inst]->algo[a] : ""; }
uint32_t h1_voices_active(void) { return eng_voices(); }
int h1_silent(void) { return eng_silent(); }
void h1_load(int pct) { eng_load(pct); }
int h1_screen(void) { return opt[OPT_SCREEN]; }
int h1_midi_channel(void) { return opt[OPT_CHAN]; }

void h1_ink(int inst, uint8_t rgb[3])
{
    int i;
    for (i = 0; i < 3; i++)
        rgb[i] = inst >= 0 && inst < NINST ? st.ink[inst][i] : 255;
}

void h1_set_ink(int inst, uint8_t r, uint8_t g, uint8_t b)
{
    if (inst < 0 || inst >= NINST)
        return;
    st.ink[inst][0] = r;
    st.ink[inst][1] = g;
    st.ink[inst][2] = b;
}

/* ---- the loop, saved apart (h1.h) ---------------------------------------------------------------- */
static uint32_t loop_saved_gen, loop_given_gen;

const void *h1_loop(uint32_t *size)
{
    uint32_t n;
    const void *p;
    loop_given_gen = eng_loop_gen();
    p = eng_loop_blob(&n);
    if (size)
        *size = n;
    return p;
}

SMALL const void *h1_loop_export(uint32_t *size, uint8_t meta[H1_META_N])
{
    uint32_t n;
    const void *p = eng_loop_blob(&n);
    int i;
    meta[0] = 1;
    meta[1] = st.glo[0];
    meta[2] = st.seq[2];
    meta[3] = st.seq[1];
    meta[4] = st.glo[1];
    meta[5] = NINST;
    for (i = 0; i < NLAYER; i++) {
        meta[6 + i] = st.layer[i].part;
        meta[22 + i] = st.layer[i].rsv[0];
        meta[38 + i] = st.layer[i].rsv[1];
    }
    if (size)
        *size = n;
    return p;
}

int h1_loop_load(const void *blob, uint32_t size)
{
    int r = eng_loop_load((const uint8_t *)blob, size);
    loop_saved_gen = eng_loop_gen();
    return r;
}

uint32_t h1_loop_gen(void) { return eng_loop_gen(); }
int h1_loop_room(void) { return (NLEV - eng_loop_used()) / 2; }
int h1_loop_quiet(void) { return eng_loop_quiet(); }
int h1_loop_dirty(void) { return eng_loop_gen() != loop_saved_gen && !eng_loop_taking(); }
void h1_loop_saved(void) { loop_saved_gen = loop_given_gen; }
void h1_midi_out(void (*fn)(uint8_t status, uint8_t d1, uint8_t d2)) { eng_midi_fn = fn; }
