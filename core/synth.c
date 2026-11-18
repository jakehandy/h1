/* H1 - the engine: voices, envelopes, LFO, arpeggiator, the sequencer (a loop recorder and a step
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

const InstDef *const insts[NINST] = {&inst_pip, &inst_bonk, &inst_urchin, &inst_twang, &inst_lava,
                                     &inst_knot, &inst_ink, &inst_tide, &inst_haunt};

static f32 SR = 44100.0f, ISR = 1.0f / 44100.0f;
static const InstDef *cur;
static int cur_i;
static volatile int pending_i = -1;
static int fade_blocks;
static Blk blk;

static f32 knob_s[4];
static f32 lfo_ph, lfo_sh, lfo_drift;
static f32 run_time;
static uint32_t stamp_ctr;
static f32 last_pitch = 60.0f;
static volatile uint32_t note_count;
static volatile f32 level_out;
static volatile f32 lfo_out;
static volatile f32 beat_out;

static f32 scope[SCOPE_N];
static volatile uint32_t scope_w;

/* ---- the queue from the main loop ------------------------------------------------------ */
static volatile uint32_t evq[64];
static volatile uint32_t ev_w, ev_r;

void eng_post(int type, int a)
{
    uint32_t w = ev_w, nx = (w + 1u) & 63u;
    if (nx == ev_r)
        return;                                  /* full: drop (64 events inside one block) */
    evq[w] = ((uint32_t)type << 8) | ((uint32_t)a & 255u);
    ev_w = nx;
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
static void voice_on(int note)
{
    Voice *v = 0;
    int i;
    if (note < 0 || note > 127)
        return;
    for (i = 0; i < NVOICE && !v; i++)
        if (voices[i].stage && voices[i].gate && voices[i].note == note)
            v = &voices[i];
    for (i = 0; i < NVOICE && !v; i++)
        if (!voices[i].stage)
            v = &voices[i];
    if (!v) {
        uint32_t best = 0xFFFFFFFFu;
        for (i = 0; i < NVOICE; i++)
            if (!voices[i].gate && voices[i].stamp < best) {
                best = voices[i].stamp;
                v = &voices[i];
            }
    }
    if (!v) {
        uint32_t best = 0xFFFFFFFFu;
        for (i = 0; i < NVOICE; i++)
            if (voices[i].stamp < best) {
                best = voices[i].stamp;
                v = &voices[i];
            }
    }
    v->target = (f32)note;
    v->pitch = st.keys[2] ? last_pitch : (f32)note;
    last_pitch = (f32)note;
    v->note = (uint8_t)note;
    v->gate = 1;
    v->stage = 1;
    v->age = 0.0f;
    v->stamp = ++stamp_ctr;
    v->pan = clampf(((f32)note - 64.0f) * (1.0f / 40.0f), -0.45f, 0.45f);
    v->r1 = rng_f(&eng_rng);
    v->r2 = rng_f(&eng_rng);
    cur->note(v, &blk);
    v->serial++;
    note_count++;
}

static void voice_off(int note)
{
    int i;
    for (i = 0; i < NVOICE; i++)
        if (voices[i].stage && voices[i].gate && voices[i].note == note) {
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
static uint8_t held[16];
static int nheld;
static int arp_i, arp_stepn;
static f32 arp_acc = 1e9f, arp_off;
static uint8_t arp_cur[16];
static int arp_ncur;
static int arp_was;
static const f32 RATE_TICKS[6] = {96.0f, 48.0f, 32.0f, 24.0f, 16.0f, 12.0f};

static void arp_release(void)
{
    int i;
    for (i = 0; i < arp_ncur; i++)
        voice_off(arp_cur[i]);
    arp_ncur = 0;
}

static void arp_fire(const InstParams *ip, f32 len)
{
    uint8_t s[16];
    int n = nheld, i, j, range = iclamp(ip->arp[2], 1, 4), total, idx = 0, mode = ip->arp[0];
    f32 gate = (f32)ip->arp[3] * 0.01f;
    arp_release();
    if (!n)
        return;
    for (i = 0; i < n; i++)
        s[i] = held[i];
    if (mode != 5)
        for (i = 1; i < n; i++) {                /* sort ascending */
            uint8_t x = s[i];
            for (j = i; j > 0 && s[j - 1] > x; j--)
                s[j] = s[j - 1];
            s[j] = x;
        }
    total = n * range;
    if (mode == 6) {                             /* CHORD: everything held, octave walking */
        int oct = arp_i % range;
        for (i = 0; i < n; i++) {
            int note = s[i] + 12 * oct;
            if (note <= 127) {
                voice_on(note);
                arp_cur[arp_ncur++] = (uint8_t)note;
            }
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
        if (note <= 127) {
            voice_on(note);
            arp_cur[arp_ncur++] = (uint8_t)note;
        }
    }
    arp_off = gate >= 0.995f ? 1e9f : len * maxf(0.06f, gate);
}

static void arp_tick(const InstParams *ip, f32 dticks)
{
    f32 step, sw, len;
    if (ip->arp[0] == 0) {
        if (arp_was) {
            arp_release();
            arp_was = 0;
            nheld = 0;
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

/* a note after the latch and the loop: to the arpeggiator when it runs, else to a voice */
static void route(int note, int on)
{
    int i;
    if (st.ip[cur_i].arp[0] == 0) {
        if (on) voice_on(note);
        else voice_off(note);
        return;
    }
    if (on) {
        for (i = 0; i < nheld; i++)
            if (held[i] == note)
                return;
        if (nheld < 16)
            held[nheld++] = (uint8_t)note;
    } else {
        for (i = 0; i < nheld; i++)
            if (held[i] == note) {
                for (; i + 1 < nheld; i++)
                    held[i] = held[i + 1];
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
static int step_bars(void)
{
    int bars = 1 << (st.seq[1] & 3), pb = step_per_bar();
    while (bars > 1 && bars * pb > NSTEP)
        bars >>= 1;
    return bars;
}
static int step_count(void) { return imin(NSTEP, step_bars() * step_per_bar()); }

/* ---- loop recorder -------------------------------------------------------------------- */
typedef struct LEv { uint16_t tick; uint8_t note; uint8_t on; } LEv;   /* on: bit 0 = on, bit 7 = recorded this pass */
#define NLEV 384
static LEv lev[NLEV];
static int nlev;
static volatile int loop_state = LOOP_EMPTY;
static volatile f32 loop_pos, loop_len = 768.0f;
static uint32_t loop_held[4];
static f32 click_env, click_ph, click_f;
static int click_at = -1;
static int loop_fresh;                           /* the take about to start replaces the loop */

static void loop_release(void)
{
    int n;
    for (n = 0; n < 128; n++)
        if (loop_held[n >> 5] & (1u << (n & 31)))
            route(n, 0);
    loop_held[0] = loop_held[1] = loop_held[2] = loop_held[3] = 0;
}

static void loop_play_range(f32 a, f32 b)
{
    int i;
    for (i = 0; i < nlev; i++) {
        f32 t = (f32)lev[i].tick;
        if (t >= a && t < b && !(lev[i].on & 0x80)) {
            int n = lev[i].note;
            if (lev[i].on & 1) {
                loop_held[n >> 5] |= 1u << (n & 31);
                route(n, 1);
            } else if (loop_held[n >> 5] & (1u << (n & 31))) {
                loop_held[n >> 5] &= ~(1u << (n & 31));
                route(n, 0);
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
    if (s != LOOP_PLAY) {                        /* the click, while recording: on the meter's beats */
        int bt = seq_bar_ticks(), tick = (int)b % bt, e = (int)b / 24;
        if (e != click_at && seq_accent(tick - tick % 24)) {
            click_env = 1.0f;
            click_f = tick < 24 ? 2400.0f : 1400.0f;
        }
        click_at = e;
    }
    if (b >= loop_len) {
        b -= loop_len;
        loop_release();
        for (i = 0; i < nlev; i++)
            lev[i].on &= 0x7F;
        if (s == LOOP_REC)
            loop_state = LOOP_PLAY;
        click_at = -1;
        loop_play_range(0.0f, b);
    }
    loop_pos = b;
}

static void loop_record(int note, int on)
{
    static const f32 Q[3] = {0.0f, 24.0f, 48.0f};
    f32 t = loop_pos, q = Q[st.seq[3] < 3 ? st.seq[3] : 0];
    if (nlev >= NLEV)
        return;
    if (on && q > 0.0f) {
        t = (f32)ifloor(t / q + 0.5f) * q;
        if (t >= loop_len)
            t -= loop_len;
    }
    lev[nlev].tick = (uint16_t)t;
    lev[nlev].note = (uint8_t)note;
    lev[nlev].on = (uint8_t)((on ? 1 : 0) | 0x80);
    nlev++;
}

static void loop_stop(void)
{
    loop_release();
    loop_state = nlev ? LOOP_IDLE : LOOP_EMPTY;
    loop_pos = 0.0f;
}

/* ---- step sequencer -------------------------------------------------------------------------
 * A row of steps, st.steps, up to four notes each. Stopped with WRITE on, a key (or a chord)
 * fills the step under the cursor and the cursor moves on when the hand lifts; running with
 * WRITE on, what is played lands on the nearest step. */
static volatile int step_playing, step_write, step_cursor, step_at;
static f32 step_acc, step_off;
static uint8_t step_snd[4];
static int step_nsnd, step_gesture;

static void step_release(void)
{
    int i;
    for (i = 0; i < step_nsnd; i++)
        route(step_snd[i], 0);
    step_nsnd = 0;
}

static void step_fire(int i, f32 len)
{
    int j;
    step_release();
    for (j = 0; j < 4; j++)
        if (st.steps[i][j]) {
            int n = st.steps[i][j] - 1;
            route(n, 1);
            step_snd[step_nsnd++] = (uint8_t)n;
        }
    step_off = len;
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
        step_at = at;
        step_fire(at, step_len(at) * 0.85f);
    }
}

static void step_put(int i, int note)
{
    int j;
    for (j = 0; j < 4; j++)
        if (st.steps[i][j] == note + 1)
            return;
    for (j = 0; j < 3 && st.steps[i][j]; j++)
        ;
    st.steps[i][j] = (uint8_t)(note + 1);
}

static void step_clear(int i) { st.steps[i][0] = st.steps[i][1] = st.steps[i][2] = st.steps[i][3] = 0; }

static void step_record(int note, int on)
{
    int n = step_count();
    if (step_playing) {
        if (on) {
            int i = step_at;
            if (step_acc > step_len(i) * 0.5f)
                i = i + 1 >= n ? 0 : i + 1;
            step_put(i, note);
        }
        return;
    }
    if (on) {
        if (step_gesture++ == 0)
            step_clear(step_cursor);
        step_put(step_cursor, note);
    } else if (step_gesture > 0 && --step_gesture == 0)
        step_cursor = step_cursor + 1 >= n ? 0 : step_cursor + 1;
}

/* ---- live notes: the latch, then the recorder, then route -------------------------------- */
static uint32_t latched[4];
static int phys_down;

static void eff_note(int note, int on)
{
    if (st.seq[0] == SEQ_STEP) {
        if (step_write)
            step_record(note, on);
        route(note, on);
        return;
    }
    if (on && loop_state == LOOP_ARMED) {
        if (loop_fresh)
            nlev = 0;
        loop_len = (f32)((1 << (st.seq[1] & 3)) * seq_bar_ticks());
        loop_pos = 0.0f;
        click_at = -1;
        loop_state = LOOP_REC;
        arp_acc = 1e9f;
    }
    if (loop_state == LOOP_REC || loop_state == LOOP_DUB)
        loop_record(note, on);
    route(note, on);
}

static void unlatch(void)
{
    int n;
    for (n = 0; n < 128; n++)
        if (latched[n >> 5] & (1u << (n & 31)))
            eff_note(n, 0);
    latched[0] = latched[1] = latched[2] = latched[3] = 0;
}

static void live_note(int note, int on)
{
    if (!st.keys[3]) {
        eff_note(note, on);
        return;
    }
    if (on) {
        if (phys_down == 0)
            unlatch();                           /* a new gesture replaces what was held */
        phys_down++;
        if (!(latched[note >> 5] & (1u << (note & 31)))) {
            latched[note >> 5] |= 1u << (note & 31);
            eff_note(note, 1);
        }
    } else if (phys_down > 0)
        phys_down--;
}

static void panic(void)
{
    int i;
    unlatch();
    loop_release();
    step_release();
    step_gesture = 0;
    nheld = 0;
    arp_release();
    phys_down = 0;
    for (i = 0; i < NVOICE; i++)
        if (voices[i].stage) {
            voices[i].gate = 0;
            voices[i].stage = 4;
        }
}

static void handle(uint32_t e)
{
    int type = (int)(e >> 8), a = (int)(e & 255u);
    switch (type) {
    case EV_NOTE_ON: live_note(a & 127, 1); break;
    case EV_NOTE_OFF: live_note(a & 127, 0); break;
    case EV_PANIC: panic(); break;
    case EV_UNLATCH: unlatch(); phys_down = 0; break;
    case EV_INST:
        if (a < NINST && a != cur_i) {
            pending_i = a;
            fade_blocks = 0;
            unlatch();
            nheld = 0;
            arp_release();
            voices_release_all();
        }
        break;
    case EV_SEQ_MODE:
        loop_stop();
        step_stop();
        step_write = 0;
        step_gesture = 0;
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
            step_clear(step_at);
        else {
            int n = step_count();
            step_clear(step_cursor);
            step_cursor = step_cursor + 1 >= n ? 0 : step_cursor + 1;
        }
        break;
    case EV_REC:
        if (st.seq[0] == SEQ_STEP) {
            step_write = !step_write;
            step_gesture = 0;
            break;
        }
        switch (loop_state) {
        case LOOP_EMPTY:
        case LOOP_IDLE:
            loop_fresh = 1;                      /* a new take; REC while it plays adds to it instead */
            loop_state = LOOP_ARMED;
            break;
        case LOOP_ARMED: loop_state = nlev ? LOOP_IDLE : LOOP_EMPTY; break;
        case LOOP_REC: loop_state = LOOP_PLAY; break;
        case LOOP_PLAY: loop_state = LOOP_DUB; break;
        default: loop_state = LOOP_PLAY; break;
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
            loop_state = LOOP_PLAY;
            break;
        }
        case LOOP_ARMED: loop_state = nlev ? LOOP_IDLE : LOOP_EMPTY; break;
        default: loop_stop(); break;
        }
        break;
    case EV_CLEAR:
        if (st.seq[0] == SEQ_STEP) {
            int i;
            step_stop();
            for (i = 0; i < NSTEP; i++)
                step_clear(i);
            step_cursor = 0;
            break;
        }
        loop_release();
        nlev = 0;
        loop_pos = 0.0f;
        loop_state = LOOP_EMPTY;
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

/* ---- the block ------------------------------------------------------------------------- */
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
    for (j = 0; j < 4; j++)
        knob_s[j] = (f32)st.ip[i].knob[j] * 0.01f;
}

static void block(f32 *L, f32 *R)
{
    const InstParams *ip;
    f32 tmp[CTRL];
    f32 dt = (f32)CTRL * ISR, brate = SR / (f32)CTRL, bpm, dticks;
    f32 att, kd, kr, sus, kg, lfo, depth, vol = 1.0f, panmod = 0.0f, peak = 0.0f;
    int i, n, target;

    if (pending_i >= 0) {                        /* an instrument change: fade, then swap */
        int any = 0;
        for (i = 0; i < NVOICE; i++)
            any |= voices[i].stage;
        if (!any || ++fade_blocks > 24) {
            switch_instrument(pending_i);
            pending_i = -1;
        }
    }
    while (pending_i < 0 && ev_r != ev_w) {
        uint32_t e = evq[ev_r];
        ev_r = (ev_r + 1u) & 63u;
        handle(e);
    }
    ip = &st.ip[cur_i];
    bpm = (f32)iclamp(st.glo[0], 40, 240);
    dticks = bpm * (96.0f / 60.0f) * dt;
    if (pending_i < 0) {
        if (st.seq[0] == SEQ_STEP) step_tick(dticks);
        else loop_tick(dticks);
        arp_tick(ip, dticks);
    }
    beat_out = fractf(beat_out + dticks * (1.0f / 96.0f));

    /* the LFO */
    {
        f32 f = 0.05f * exp2fx((f32)ip->lfo[1] * 0.09f);
        lfo_ph += f * dt;
        if (lfo_ph >= 1.0f) {
            lfo_ph -= (f32)ifloor(lfo_ph);
            lfo_sh = rng_s(&eng_rng);
        }
        lfo_drift += (lfo_sh - lfo_drift) * minf(1.0f, f * dt * 3.0f);
        switch (ip->lfo[0]) {
        case 1: lfo = 4.0f * absf(lfo_ph - 0.5f) - 1.0f; break;
        case 2: lfo = 1.0f - 2.0f * lfo_ph; break;
        case 3: lfo = lfo_ph < 0.5f ? 1.0f : -1.0f; break;
        case 4: lfo = lfo_sh; break;
        case 5: lfo = clampf(lfo_drift * 1.6f, -1.0f, 1.0f); break;
        default: lfo = tsin(lfo_ph); break;
        }
        depth = (f32)ip->lfo[2] * 0.01f;
        target = ip->lfo[3];
        lfo_out = lfo * depth;
    }

    /* the knobs, smoothed (~12 ms), plus the LFO on its target */
    for (i = 0; i < 4; i++) {
        knob_s[i] += ((f32)ip->knob[i] * 0.01f - knob_s[i]) * 0.06f;
        blk.k[i] = knob_s[i];
    }
    blk.bend = ((f32)st.glo[2] - 50.0f) * 0.01f;
    if (target == 0)
        blk.bend += lfo * depth * depth * 12.0f;
    else if (target <= 4)
        blk.k[target - 1] = sat(blk.k[target - 1] + lfo * depth * 0.5f);
    else if (target == 5)
        vol = 1.0f - depth * (0.5f + 0.5f * lfo);
    else
        panmod = lfo * depth;
    blk.algo = ip->algo < NALGO ? ip->algo : 0;
    blk.sr = SR;
    blk.isr = ISR;
    blk.lfo = lfo;
    blk.time = run_time;
    blk.bpm = bpm;
    run_time += dt;
    cur->block(&blk);

    /* envelope rates */
    att = dt / (0.0015f * exp2fx((f32)ip->env[0] * 0.114f));
    kd = onepole_k(0.02f * exp2fx((f32)ip->env[1] * 0.08f), brate);
    sus = (f32)ip->env[2] * 0.01f;
    kr = onepole_k(0.015f * exp2fx((f32)ip->env[3] * 0.084f), brate);
    if (pending_i >= 0)
        kr = 0.3f;
    kg = st.keys[2] ? onepole_k(0.004f * exp2fx((f32)st.keys[2] * 0.08f), brate) : 1.0f;

    for (n = 0; n < CTRL; n++)
        L[n] = R[n] = 0.0f;

    for (i = 0; i < NVOICE; i++) {
        Voice *v = &voices[i];
        f32 e0, e1, g, gl, gr, de, pan;
        if (!v->stage)
            continue;
        e0 = v->env;
        switch (v->stage) {
        case 1:
            v->env += att;
            if (v->env >= 1.0f) {
                v->env = 1.0f;
                v->stage = 2;
            }
            break;
        case 2:
        case 3:
            v->env += (sus - v->env) * kd;
            break;
        default:
            v->env -= v->env * kr + 0.00002f;
            if (v->env <= 0.0003f)
                v->env = 0.0f;
            break;
        }
        e1 = v->env;
        v->env_prev = e0;
        v->pitch += (v->target - v->pitch) * kg;
        cur->render(v, &blk, tmp, CTRL);
        v->age += dt;
        g = cur->gain * vol;
        pan = clampf(v->pan + panmod, -1.0f, 1.0f);
        gl = g * (1.0f - maxf(0.0f, pan) * 0.8f);
        gr = g * (1.0f + minf(0.0f, pan) * 0.8f);
        de = (e1 - e0) * (1.0f / (f32)CTRL);
        for (n = 0; n < CTRL; n++) {
            f32 s = tmp[n] * e0;
            e0 += de;
            L[n] += s * gl;
            R[n] += s * gr;
        }
        if (v->stage == 4 && v->env <= 0.0f)
            v->stage = 0;
    }

    if (cur->post)
        cur->post(&blk, L, R, CTRL);

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

    fx_block(L, R, ip, bpm);

    if (click_env > 0.001f) {
        for (n = 0; n < CTRL; n++) {
            f32 c = tsin(click_ph) * click_env * 0.25f;
            click_ph += click_f * ISR;
            if (click_ph >= 1.0f) click_ph -= 1.0f;
            click_env *= 0.9965f;
            L[n] += c;
            R[n] += c;
        }
    }
}

void eng_render(int32_t *lr, uint32_t n)
{
    static f32 L[CTRL], R[CTRL];
    static uint32_t have, pos;                   /* frames of a block not yet handed out */
    while (n) {
        uint32_t take, i;
        if (!have) {
            block(L, R);
            have = CTRL;
            pos = 0;
        }
        take = have < n ? have : n;
        for (i = 0; i < take; i++) {
            lr[0] = (int32_t)(softclip(L[pos + i]) * 7549747.0f);     /* 0.9 of 24-bit full scale */
            lr[1] = (int32_t)(softclip(R[pos + i]) * 7549747.0f);
            lr += 2;
        }
        pos += take;
        have -= take;
        n -= take;
    }
}

void eng_init(uint32_t sample_rate)
{
    int i;
    if (sample_rate) {                           /* 0: keep the rate (a state load) */
        SR = (f32)sample_rate;
        ISR = 1.0f / SR;
    }
    dsp_init();
    fx_init(SR);
    switch_instrument(st.inst < NINST ? st.inst : 0);
    for (i = 0; i < NVOICE; i++)
        voices[i].serial = 0;
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
    vis->scope = scope;
    vis->scope_pos = (int)(scope_w & (SCOPE_N - 1));
}

uint32_t eng_sounding_lo(void)
{
    uint32_t m = 0;
    int i, base = 53 + 12 * st.octave;
    for (i = 0; i < NVOICE; i++)
        if (voices[i].stage && voices[i].gate) {
            int k = (int)voices[i].note - base;
            if (k >= 0 && k < H1_NKEYS)
                m |= 1u << k;
        }
    return m;
}

uint32_t eng_voices(void)
{
    uint32_t n = 0;
    int i;
    for (i = 0; i < NVOICE; i++)
        n += voices[i].stage != 0;
    return n;
}
