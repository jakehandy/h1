/* h1 - the panel and the screen around the instruments: what every knob and button does, the
 * home overlays (the name flash, the knob strip, toasts) and the settings pages, which are one
 * layout for every page and every instrument: four columns, one per knob, left to right.
 *
 *   PRESETS    the instrument               ALGORITHM   its algorithm (8 each)
 *   KNOB 1-4   home: the instrument's four live modulations. A page: its four columns.
 *   SELECT     home: tempo (the step cursor while writing steps). A page: the next or previous page.
 *   FX SEL ENV LFO GLO ARP SEQ   open that page; again, or HOME, goes back
 *   SEQ held   swaps the sequencer between LOOP and STEP
 *   GLO held   the metronome: while recording, always, or off
 *              (a hold never does the tap as well: GLO, SEQ, HOME, SAVE, REC and EDIT tap on release)
 *   EDIT       pins the knob strip on the home screen (writing steps: a rest)
 *   EDIT held  with SELECT or MASTER: the volume of the instrument in hand, beside MASTER alone, which
 *              is everything's. Either one shows as a ring in the middle of the screen, GLOBAL or
 *              INSTRUMENT. After MASTER has been the instrument's, everything's volume stays where it
 *              was until the knob is turned back through it.
 *   SAVE       saves now; held, resets the four knobs of the algorithm in hand; held on, this
 *              instrument (every algorithm's knobs and its pages); held four seconds, every
 *              instrument and the KEYS and GLOBAL pages (the sequencer and what it holds are left
 *              alone)
 *   PLAY REC   the sequencer. Loop: REC arms a take, or overdubs while it plays. Step: REC is
 *              step-write. REC held clears.
 *   OCT- OCT+  octave; both, back to the middle
 *   BLAB       EDIT is WRITE: the keys are letters, A to Z from the left and a space at the top; OCT- takes
 *              the last one back, OCT+ is a space, both clear. EDIT again (or HOME) and the keys sing it.
 */
#include "internal.h"

/* ---- pages ------------------------------------------------------------------------------ */
static const char *const O_ARPMODE[] = {"OFF", "UP", "DOWN", "UPDN", "RAND", "ORDER", "CHORD"};
static const char *const O_RATE[] = {"1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32"};
static const char *const O_SHAPE[] = {"SINE", "TRI", "RAMP", "SQR", "STEP", "DRIFT"};
static const char *const O_TARGET[] = {"PITCH", "K1", "K2", "K3", "K4", "LEVEL", "PAN"};
static const char *const O_TIME[] = {"1/32", "1/16", "1/8T", "1/8", "3/16", "1/4", "3/8", "1/2"};
static const char *const O_ROOT[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static const char *const O_SCALE[] = {"CHROM", "MAJOR", "MINOR", "PENTA", "MPENT", "BLUES", "DORIAN", "WHOLE"};
static const char *const O_ONOFF[] = {"OFF", "ON"};
static const char *const O_BARS[] = {"1", "2", "4", "8", "16", "32", "64", "128"};
static const char *const O_QUANT[] = {"FREE", "1/16", "1/8"};
static const char *const O_SEQMODE[] = {"LOOP", "STEP"};
static const char *const O_METER[] = {"4/4", "3/4", "5/4", "6/8", "7/8"};
static const char *const O_GRID[] = {"1/8", "1/16", "1/32"};
static const char *const O_CLICK[] = {"CLICK REC", "CLICK ON", "CLICK OFF"};
static const char *const O_HOLD[] = {"OFF", "ON", "ARP"};
static const char *const O_CHAN[] = {"ALL", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16"};
static const char *const O_BEND[] = {"OFF", "2", "12"};
static const char *const O_ONOFF_R[] = {"ON", "OFF"};
static const char *const O_SCREEN[] = {"AUTO", "FAST", "SAFE"};
static const char *const O_LEDS[] = {"PLAYED", "SEQ"};

/* in the order of the buttons that open them, reading the panel's two rows left to right */
const PageDef pages[NPAGE] = {
    {"HOME", {{0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0}}},
    {"FX", {{"ECHO", PK_PCT, 100, 0, 0, 0}, {"TIME", PK_ENUM, 7, 0, 0, O_TIME},
            {"SPACE", PK_PCT, 100, 0, 0, 0}, {"TONE", PK_PCT, 100, 0, -50, 0}}},
    {"KEYS", {{"ROOT", PK_ENUM, 11, 0, 0, O_ROOT}, {"SCALE", PK_ENUM, 7, 0, 0, O_SCALE},
              {"GLIDE", PK_PCT, 100, 0, 0, 0}, {"HOLD", PK_ENUM, 2, 0, 0, O_HOLD}}},
    {"ENV", {{"ATTACK", PK_PCT, 100, 0, 0, 0}, {"DECAY", PK_PCT, 100, 0, 0, 0},
             {"SUSTAIN", PK_PCT, 100, 0, 0, 0}, {"RELEASE", PK_PCT, 100, 0, 0, 0}}},
    {"LFO", {{"SHAPE", PK_ENUM, 5, 0, 0, O_SHAPE}, {"RATE", PK_PCT, 100, 0, 0, 0},
             {"DEPTH", PK_PCT, 100, 0, 0, 0}, {"TO", PK_ENUM, 6, 0, 0, O_TARGET}}},
    {"GLOBAL", {{"TEMPO", PK_INT, 240, 40, 0, 0}, {"SWING", PK_PCT, 100, 0, 0, 0},
                {"TUNE", PK_PCT, 100, 0, -50, 0}, {"BRIGHT", PK_PCT, 100, 20, 0, 0}}},
    {"ARP", {{"MODE", PK_ENUM, 6, 0, 0, O_ARPMODE}, {"RATE", PK_ENUM, 5, 0, 0, O_RATE},
             {"RANGE", PK_INT, 4, 1, 0, 0}, {"GATE", PK_PCT, 100, 5, 0, 0}}},
    {"SEQ", {{"MODE", PK_ENUM, 1, 0, 0, O_SEQMODE}, {"BARS", PK_ENUM, 7, 0, 0, O_BARS},
             {"METER", PK_ENUM, 4, 0, 0, O_METER}, {"SNAP", PK_ENUM, 2, 0, 0, O_QUANT}}},
    /* no buttons: SELECT turns on to them from SEQ (0.3.1). Every one is off, or as before, until set. */
    {"MIDI", {{"CHAN", PK_ENUM, 16, 0, 0, O_CHAN}, {"BEND", PK_ENUM, 2, 0, 0, O_BEND},
              {"WHEEL", PK_ENUM, 1, 0, 0, O_ONOFF}, {"PEDAL", PK_ENUM, 1, 0, 0, O_ONOFF}}},
    {"SETUP", {{"ANIM", PK_ENUM, 1, 0, 0, O_ONOFF_R}, {"EGGS", PK_ENUM, 1, 0, 0, O_ONOFF_R},
               {"SCREEN", PK_ENUM, 2, 0, 0, O_SCREEN}, {"LEDS", PK_ENUM, 1, 0, 0, O_LEDS}}},
};
static const ParamDef P_GRID = {"GRID", PK_ENUM, 2, 0, 0, O_GRID};

/* where each page's button sits on the panel: column 0..5, row 0..1 */
static const uint8_t PAGE_AT[PG_BUTTONS][2] = {{0, 1}, {0, 0}, {1, 0}, {2, 0}, {3, 0}, {5, 0}, {2, 1}, {3, 1}};

const ParamDef *page_param(int pg, int i)
{
    if (pg == PG_SEQ && i == 3 && st.seq[0] == SEQ_STEP)
        return &P_GRID;
    return &pages[pg].p[i];
}

uint8_t *page_value(int pg, int i)
{
    InstParams *ip = &st.ip[st.inst];
    switch (pg) {
    case PG_ARP: return &ip->arp[i];
    case PG_LFO: return &ip->lfo[i];
    case PG_ENV: return &ip->env[i];
    case PG_FX: return &ip->fx[i];
    case PG_KEYS: return &st.keys[i];
    case PG_GLO: return &st.glo[i];
    case PG_SEQ: return &st.seq[i == 3 && st.seq[0] == SEQ_STEP ? 4 : i];
    case PG_MIDI: return &opt[i];
    case PG_SETUP: return &opt[4 + i];
    default: return &KNOBS_NOW(st.inst)[i];
    }
}

/* ---- state ------------------------------------------------------------------------------ */
static int page = PG_HOME;
static uint32_t now, last_ms, frame_n;
static f32 tsec;
static Vis vis;
static f32 vk[4], vkv[4];
static uint32_t vrng = 0x9E3779B9u;

static f32 name_t = 0.0f;                        /* seconds since the instrument came up */
static f32 page_t = 9.0f;                        /* seconds since the page opened */
static f32 knob_touch[4] = {9.0f, 9.0f, 9.0f, 9.0f};
static int active_col = 0;
static f32 col_v[4], col_vv[4];                  /* the gauges, eased */
static char toast_s[24];
static uint32_t loop_full_was;
static f32 toast_t = 9.0f;
static f32 vol_t = 9.0f;                         /* seconds since MASTER last moved */
static int vol_seen = -1;
/* MASTER is a knob with end stops: it says where it stands and nothing else. Turned with EDIT held it
 * sets the volume of the instrument in hand, and then it no longer stands where the volume of
 * everything was left. So that volume is held (0..1000 here, -1 while it follows the knob) until the
 * knob, turned without EDIT, comes back through it and picks it up again. */
static int glob_held = -1;
static int master_raw = -1;                      /* the knob at the last reading */
static int edit_at = -1;                         /* the knob when EDIT went down: everything stays there while EDIT is
                                                  * held, so a turn too small to count yet does not move it (#38) */
/* The volume ring: MASTER's (GLOBAL) or that of the instrument in hand (INSTRUMENT), whichever moved last. */
enum { RING_GLOBAL = 1, RING_INST };
static int ring_kind;
static f32 ring_t = 9.0f, ring_x, ring_v;        /* seconds since it was last touched, and where it shows */
static f32 ring_up = 9.0f;                       /* seconds since it came up */
#define RING_STAYS 0.9f                          /* seconds it stays after the last touch, then 0.4 s to go */
static int toast_pips = -1;
static uint32_t clear_all_ms;                    /* REC held three seconds: when it asked to be pressed again */
static int clear_all;

static SeqInfo seq;                              /* the sequencer, as of this frame */
static int seq_write_was;
static int layer_rec_was, layers_n;              /* a take going into a layer, and how many layers there are */
static uint32_t enc_ms[H1_NENC];
static uint8_t key_note[H1_NKEYS];
static uint8_t key_down[H1_NKEYS];
static uint32_t btn_held;
static uint32_t btn_ms[H1_NBTN];
static uint8_t btn_fired[H1_NBTN];

static uint16_t pal[256];
static uint8_t pal_key[5];
static uint32_t prev_w[FB_W * FB_H / 4] H1_POOL;   /* what the panel shows now; words, as the framebuffer is */
#define prev_fb ((uint8_t *)prev_w)
static int force_full = 1;
#define RQ_MAX 512
static uint8_t rq[RQ_MAX][4];                    /* what is queued for the panel: x, y, w, h (h1_present_begin) */
static int rq_n, rq_i;
static uint32_t rq_bytes;
static int dirty, save_asked;                    /* save_asked: SAVE pressed, saved even while a tail rings */
static uint32_t dirty_ms;

int ui_page(void) { return page; }

static void mark_dirty(void)
{
    dirty = 1;
    dirty_ms = now;
}

/* ---- little text helpers ---------------------------------------------------------------- */
static char *put_s(char *d, const char *s)
{
    while (*s)
        *d++ = *s++;
    *d = 0;
    return d;
}
static char *put_i(char *d, int v)
{
    char b[8];
    int n = 0;
    if (v < 0) {
        *d++ = '-';
        v = -v;
    }
    do {
        b[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v && n < 7);
    while (n)
        *d++ = b[--n];
    *d = 0;
    return d;
}

static void toast(const char *a, int num, int has_num, const char *b, int pips)
{
    char *d = toast_s;
    if (a) d = put_s(d, a);
    if (has_num) d = put_i(d, num);
    if (b) put_s(d, b);
    toast_t = 0.0f;
    toast_pips = pips;
}

/* text no wider than max_w, no taller than size */
static void fit_text(f32 cx, f32 y, const char *s, f32 size, f32 max_w, f32 a)
{
    f32 w = g_text_w(s, size);
    if (w > max_w) {
        f32 ns = size * max_w / w;
        y += (size - ns) * 0.5f;
        size = ns;
    }
    g_text(cx, y, s, size, maxf(1.3f, size * 0.13f), a, 1);
}

static APART void value_text(int pg, int i, char *out)
{
    const ParamDef *p = page_param(pg, i);
    int v = *page_value(pg, i);
    if (pg == PG_SEQ && i == 1 && st.seq[0] == SEQ_STEP) {
        SeqInfo si;                              /* a pattern holds 64 steps: long bars of small steps get fewer bars */
        eng_seq_info(&si);
        put_i(out, si.bars);
        return;
    }
    if (p->kind == PK_ENUM) {
        if (pg == PG_LFO && i == 3 && v >= 1 && v <= 4)
            put_s(out, insts[st.inst]->knob[v - 1]);
        else
            put_s(out, p->opts[v <= p->max ? v : 0]);
    } else {
        int shown = v + p->show_off;
        char *d = out;
        if (p->show_off < 0 && shown > 0)
            *d++ = '+';
        put_i(d, shown);
    }
}

/* ---- input ------------------------------------------------------------------------------ */
static int accel(int enc, int d)
{
    uint32_t gap = now - enc_ms[enc];
    int m = 1, ad = d < 0 ? -d : d;
    /* a slow click is one unit, for fine work; a spin covers the whole range in about a turn */
    if (gap < 22u) m = 6;
    else if (ad >= 3 || gap < 45u) m = 4;
    else if (ad == 2 || gap < 90u) m = 2;
    enc_ms[enc] = now;
    return d * m;
}

static int writing(void) { return blab_writing && insts[st.inst] == &inst_blab; }

static void write_mode(int on)
{
    if (on == blab_writing)
        return;
    if (!on && writing() && st.words[0]) {
        KNOBS_NOW(st.inst)[0] = 0;               /* done: WORDS goes to your own */
        knob_touch[0] = 0.0f;
    }
    if (!on && insts[st.inst] == &inst_blab)    /* (WRITE shows itself: the keys come up) */
        toast(st.words[0] ? "YOUR WORDS" : "WRITE OFF", 0, 0, 0, -1);
    blab_writing = (uint8_t)on;
    mark_dirty();
}

static APART void set_instrument(int i)
{
    blab_writing = 0;
    int hint = seq.mode == SEQ_LOOP && (seq.loop_state == LOOP_PLAY || seq.loop_state == LOOP_IDLE);
    st.inst = (uint8_t)i;
    eng_post(EV_INST, i);
    name_t = 0.0f;
    page = PG_HOME;
    vis.fresh = 1;
    vis.enter = 0.0f;
    vis.algo = vis.algo_prev = st.ip[i].algo;
    vis.algo_t = 1.0f;
    toast_t = 9.0f;
    if (ring_kind == RING_INST)
        ring_t = 9.0f;                           /* (that was the last instrument's volume: the name first) */
    /* a loop is there: say how to play this instrument over it (under the name, where the toasts go) */
    if (hint)
        toast(st.seq[6] ? "REC: NEW LAYER" : "SEQ + REC: LAYERS", 0, 0, 0, -1);
    mark_dirty();
}

static APART void open_page(int pg)
{
    int i;
    if (page == pg)
        pg = PG_HOME;
    if (pg != PG_HOME)
        write_mode(0);
    page = pg;
    page_t = 0.0f;
    if (pg != PG_HOME)
        for (i = 0; i < 4; i++) {
            const ParamDef *p = page_param(pg, i);
            col_v[i] = p->max > p->min ? (f32)(*page_value(pg, i) - p->min) / (f32)(p->max - p->min) : 0.0f;
            col_vv[i] = 0.0f;
        }
}

static APART void set_seq_mode(int m)
{
    if (m == st.seq[0])
        return;
    st.seq[0] = (uint8_t)m;
    eng_post(EV_SEQ_MODE, 0);
    if (page == PG_SEQ) {                        /* the last column is a different setting now */
        const ParamDef *p = page_param(PG_SEQ, 3);
        col_v[0] = (f32)m;
        col_v[3] = (f32)(*page_value(PG_SEQ, 3) - p->min) / (f32)(p->max - p->min);
    }
    mark_dirty();
}

/* ---- the layer view (0.3.1, #55 #63): PLAY held with LAYERS on ---------------------------------
 * The sixteen white keys are the sixteen layers. A tap mutes or unmutes one; held a moment, it is the
 * only one heard (solo), and again, all are. Held with SELECT turned, its volume; held with REC, it is
 * cleared. While the sequencer runs, PLAY stops it only on a quick tap with nothing else touched. */
static const uint8_t WHITE_KEY[NLAYER] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};
#define LV_SOLO_MS 800u
static int lv_play, lv_used, lv_defer, lv_key = -1, lv_key_fired;
static uint32_t lv_key_ms;

static int layview_on(void) { return lv_play && (lv_used || now - btn_ms[H1_BTN_PLAY] > 250u); }

static int seq_running(void)
{
    if (seq.mode == SEQ_STEP)
        return seq.playing;
    return seq.loop_state == LOOP_PLAY || seq.loop_state == LOOP_DUB || seq.loop_state == LOOP_REC;
}

static uint32_t lay_heard(void)                  /* bit k: layer k + 1 is there and heard */
{
    uint32_t solo = 0, mute = 0, have = 0;
    int i;
    for (i = 0; i < NLAYER; i++)
        if (st.layer[i].part) {
            have |= 1u << i;
            if (st.layer[i].rsv[1] & LAY_SOLO) solo |= 1u << i;
            if (st.layer[i].rsv[1] & LAY_MUTE) mute |= 1u << i;
        }
    return solo ? solo : have & ~mute;
}

static uint32_t layview_leds(void)
{
    uint32_t m = 0, heard = lay_heard();
    int i;
    for (i = 0; i < NLAYER; i++)
        if ((heard >> i & 1u) && (!(st.layer[i].rsv[1] & LAY_SOLO) || fractf(tsec * 2.5f) < 0.6f))
            m |= 1u << WHITE_KEY[i];
    return m;
}

static APART void lv_toast(int k, const char *what)
{
    toast("LAYER ", k + 1, 1, what, -1);
}

static APART void lv_key_up(int k)
{
    Layer *l = &st.layer[k];
    if (!l->part) {
        lv_toast(k, " EMPTY");
        return;
    }
    l->rsv[1] ^= LAY_MUTE;
    lv_toast(k, l->rsv[1] & LAY_MUTE ? " MUTED" : " ON");
    mark_dirty();
}

/* a button pressed while PLAY is held: REC on a layer key clears that layer */
static APART int layview_button(int btn)
{
    if (!lv_play || btn != H1_BTN_REC || lv_key < 0)
        return 0;
    lv_used = lv_key_fired = 1;
    btn_fired[btn] = 3;
    if (st.layer[lv_key].part) {
        eng_post(EV_CLEAR_LAY, lv_key + 1);
        lv_toast(lv_key, " CLEARED");
        mark_dirty();
    } else
        lv_toast(lv_key, " EMPTY");
    return 1;
}

/* a key while PLAY is held: 1 when the layer view took it */
static APART int layview_key(int key, int down)
{
    int k;
    if (!lv_play)
        return 0;
    for (k = 0; k < NLAYER && WHITE_KEY[k] != key; k++)
        ;
    lv_used = 1;
    if (down) {
        if (k < NLAYER) {
            lv_key = k;
            lv_key_ms = now;
            lv_key_fired = 0;
        }
    } else if (k < NLAYER && k == lv_key) {
        if (!lv_key_fired)
            lv_key_up(k);
        lv_key = -1;
    }
    return 1;
}

static APART void layview_hold(void)
{
    Layer *l;
    if (lv_key < 0 || lv_key_fired || now - lv_key_ms <= LV_SOLO_MS)
        return;
    lv_key_fired = 1;
    l = &st.layer[lv_key];
    if (!l->part) {
        lv_toast(lv_key, " EMPTY");
        return;
    }
    if (l->rsv[1] & LAY_SOLO) {
        l->rsv[1] &= ~LAY_SOLO;
        lv_toast(lv_key, " SOLO OFF");
    } else {
        int i;
        for (i = 0; i < NLAYER; i++)             /* one at a time: the one held is the one heard */
            st.layer[i].rsv[1] &= ~LAY_SOLO;
        l->rsv[1] = (uint8_t)((l->rsv[1] | LAY_SOLO) & ~LAY_MUTE);
        lv_toast(lv_key, " SOLO");
    }
    mark_dirty();
}

static APART void layview_volume(int d)
{
    Layer *l = &st.layer[lv_key];
    int v;
    lv_key_fired = 1;
    if (!l->part) {
        lv_toast(lv_key, " EMPTY");
        return;
    }
    v = iclamp(100 - l->rsv[0] + d, 0, 100);
    l->rsv[0] = (uint8_t)(100 - v);
    toast("LAYER ", lv_key + 1, 1, 0, -1);
    put_i(put_s(toast_s + 7 + (lv_key >= 9), " VOL "), v);
    mark_dirty();
}

/* the view: a card for each layer, four by four, the white keys' order */
static APART void draw_layview(void)
{
    uint32_t heard = lay_heard();
    int i;
    char b[4];
    g_mode(G_PAINT);
    g_rect(4.0f, 30.0f, 232.0f, 178.0f, 0.0f);
    g_mode(G_MAX);
    g_text(120.0f, 34.0f, "LAYERS", 12.0f, 1.6f, 1.0f, 1);
    for (i = 0; i < NLAYER; i++) {
        const Layer *l = &st.layer[i];
        f32 x = 10.0f + (f32)(i & 3) * 56.0f, y = 54.0f + (f32)(i >> 2) * 37.0f, a;
        int on = (int)(heard >> i & 1u);
        a = !l->part ? 0.2f : (on ? 1.0f : 0.4f);
        g_line(x, y, x + 50.0f, y, i == lv_key ? 3.0f : 1.4f, a);
        put_i(b, i + 1);
        g_text(x, y + 4.0f, b, 9.0f, 1.2f, a, 0);
        if (!l->part)
            continue;
        if (l->rsv[1] & LAY_SOLO)
            g_disc(x + 46.0f, y + 8.0f, 3.0f, 1.0f);
        else if (l->rsv[1] & LAY_MUTE)
            g_text(x + 50.0f, y + 4.0f, "M", 9.0f, 1.2f, 0.7f, 2);
        fit_text(x + 25.0f, y + 16.0f, insts[PART_INST(l->part)]->name, 8.0f, 50.0f, a);
        g_line(x, y + 30.0f, x + 50.0f * (f32)(100 - l->rsv[0]) * 0.01f, y + 30.0f, 2.0f, a * 0.8f);
    }
}

SMALL void h1_key(int key, int down)
{
    if (key < 0 || key >= H1_NKEYS)
        return;
    if ((down || !key_down[key]) && layview_key(key, down))
        return;
    if (down) {
        int note = scale_snap(53 + key + 12 * st.octave);
        if (key_down[key])
            return;
        if (writing()) {                         /* WRITE: a letter, sung at its key */
            blab_type(key);
            mark_dirty();
            if (key >= 26)
                return;
        }
        note = iclamp(note, 0, 127);
        key_down[key] = 1;
        key_note[key] = (uint8_t)note;
        eng_post(EV_NOTE_ON, note);
    } else if (key_down[key]) {
        key_down[key] = 0;
        eng_post(EV_NOTE_OFF, key_note[key]);
    }
}

SMALL void h1_midi(uint8_t status, uint8_t d1, uint8_t d2)
{
    uint8_t type = status & 0xF0u;
    if (opt[OPT_CHAN] && type >= 0x80u && type < 0xF0u && (status & 15u) != opt[OPT_CHAN] - 1u)
        return;                                              /* MIDI page CHAN: one channel only */
    if (type == 0xE0u) {                                     /* pitch bend, by BEND's range */
        if (opt[OPT_BEND])
            eng_bend = (f32)((int)((d2 & 127u) << 7 | (d1 & 127u)) - 8192) * (opt[OPT_BEND] == 1 ? 2.0f : 12.0f) * (1.0f / 8192.0f);
        return;
    }
    if (type == 0xB0u && d1 == 1) {                          /* the mod wheel */
        if (opt[OPT_WHEEL])
            eng_wheel = (f32)(d2 & 127u) * (1.0f / 127.0f);
        return;
    }
    if (type == 0xB0u && d1 == 64) {                         /* the sustain pedal */
        if (opt[OPT_PEDAL])
            eng_post(EV_SUSTAIN, d2 >= 64u);
        return;
    }
    if (type == 0x90u && d2)
        eng_post_note(1, d1 & 127, d2 & 127, 1);             /* with its velocity */
    else if (type == 0x80u || type == 0x90u)
        eng_post_note(0, d1 & 127, 0, 1);
    else if (type == 0xB0u && d1 == 123)
        eng_post(EV_PANIC, 1);                               /* (1: it came in over MIDI, and is not sent back) */
    else if (type == 0xB0u && d1 >= 20 && d1 <= 23) {      /* CC 20-23: the four knobs */
        KNOBS_NOW(st.inst)[d1 - 20] = (uint8_t)(((d2 & 127) * 100 + 63) / 127);
        knob_touch[d1 - 20] = 0.0f;
    }
}

static f32 ring_value(int kind)
{
    int g = glob_held >= 0 ? glob_held : vol_seen;
    return kind == RING_INST ? (f32)INST_VOL(st.inst) * 0.01f : (f32)(g < 0 ? 0 : g) * 0.001f;
}

static void ring_show(int kind)
{
    if (kind != ring_kind || ring_t >= RING_STAYS + 0.4f) {   /* coming up, or the other volume: at its value, not swept there */
        if (ring_t >= RING_STAYS + 0.4f)
            ring_up = 0.0f;
        ring_x = ring_value(kind);
        ring_v = 0.0f;
    }
    ring_kind = kind;
    ring_t = 0.0f;
    name_t = maxf(name_t, 1.05f);                /* (the instrument's name, if it is still up, makes way) */
}

int h1_volume_ring(void) { return ring_t < RING_STAYS + 0.4f ? ring_kind : 0; }

static void set_inst_vol(int v);

int h1_global_volume(void)
{
    int g = glob_held, e = edit_at;
    if (g < 0 && e >= 0 && (btn_held & (1u << H1_BTN_EDIT)))
        g = e;
    return g;
}

/* MASTER, as the port reads it. The knob is an analog reading and never quite still: the ring comes up
 * for a real move (over 1.2 % of the travel) and then follows the small ones for a moment after.
 * With EDIT held a real move is the instrument's volume: the knob's travel, less a little at each end
 * so that both ends are reached, is its 0 to 100. */
void h1_master(int level)
{
    int d, was = master_raw, from;
    level = iclamp(level, 0, 1000);
    master_raw = level;
    if (vol_seen < 0) {                          /* the first reading: where it stands, nothing to show */
        vol_seen = level;
        return;
    }
    d = level > vol_seen ? level - vol_seen : vol_seen - level;
    if (d < (vol_t < 1.2f ? 3 : 12))
        return;
    from = vol_seen;
    vol_seen = level;
    vol_t = 0.0f;
    if (btn_held & (1u << H1_BTN_EDIT)) {
        if (glob_held < 0)
            glob_held = edit_at >= 0 ? edit_at : was;   /* everything stays as loud as it was when EDIT went down */
        btn_fired[H1_BTN_EDIT] = 1;
        set_inst_vol(((level - 15) * 100 + 485) / 970);
        return;
    }
    if (glob_held >= 0) {
        int near = level > glob_held ? level - glob_held : glob_held - level;
        if (near <= 12 || (from <= glob_held && level >= glob_held) || (from >= glob_held && level <= glob_held))
            glob_held = -1;
    edit_at = -1;                      /* the knob has come back through it: it follows again */
    }
    ring_show(RING_GLOBAL);
}

/* the volume of the instrument in hand: saved with it, and back at full when the instrument is reset */
static void set_inst_vol(int v)
{
    v = iclamp(v, 0, 100);
    if (v != INST_VOL(st.inst)) {
        st.ip[st.inst].mix[IP_CUT] = (uint8_t)(100 - v);
        mark_dirty();
    }
    ring_show(RING_INST);
}

static APART void octave(int d)
{
    if (d == 0) st.octave = 0;
    else st.octave = (int8_t)iclamp(st.octave + d, -2, 2);
    toast("OCT ", st.octave, 1, 0, -1);
    if (st.octave > 0) {
        toast_s[4] = '+';
        put_i(toast_s + 5, st.octave);
    }
    mark_dirty();
}

SMALL void h1_button(int btn, int down)
{
    if (btn < 0 || btn >= H1_NBTN)
        return;
    if (!down) {
        btn_held &= ~(1u << btn);
        if (btn == H1_BTN_PLAY) {
            if (lv_defer && !lv_used && now - btn_ms[btn] < 600u)
                eng_post(EV_PLAY, 0);
            lv_play = lv_defer = 0;
            lv_key = -1;
        }
        /* a button with a hold does its tap on the way up, and only if the hold never fired */
        if (btn_fired[btn])
            return;
        if (btn == H1_BTN_REC)
            eng_post(EV_REC, 0);
        if (btn == H1_BTN_GLO)
            open_page(PG_GLO);
        if (btn == H1_BTN_SEQ)
            open_page(PG_SEQ);
        if (btn == H1_BTN_HOME) {
            write_mode(0);
            if (page == PG_HOME)
                name_t = 0.0f;
            page = PG_HOME;
        }
        if (btn == H1_BTN_SAVE) {
            dirty = save_asked = 1;
            dirty_ms = now - 5000u;
            toast("SAVED", 0, 0, 0, -1);
        }
        if (btn == H1_BTN_EDIT) {
            if (seq.mode == SEQ_STEP && seq.write && page == PG_HOME)
                eng_post(EV_STEP_REST, 0);       /* writing steps: a rest */
            else if (page != PG_HOME) {          /* on a page: home, with the knob strip up (#49) */
                page = PG_HOME;
                st.hud = 1;
                mark_dirty();
            } else if (insts[st.inst] == &inst_blab)
                write_mode(!blab_writing);       /* BLAB: WRITE */
            else {
                st.hud ^= 1;
                mark_dirty();
            }
        }
        return;
    }
    if (btn == H1_BTN_EDIT)
        edit_at = master_raw;
    btn_held |= 1u << btn;
    btn_ms[btn] = now;
    btn_fired[btn] = 0;
    if (clear_all) {                             /* asked "TAP REC: CLEAR ALL": REC clears, anything else is a no */
        clear_all = 0;
        if (btn == H1_BTN_REC) {
            btn_fired[btn] = 3;
            eng_post(EV_CLEAR, 0);
            toast("ALL CLEARED", 0, 0, 0, -1);
            return;
        }
    }
    if (layview_button(btn))
        return;
    /* SEQ and REC together: LAYERS. Whichever lands second makes the pair, and neither then does what
     * it does alone (a pair made after one of them has already done its hold does nothing). */
    if (btn == H1_BTN_SEQ || btn == H1_BTN_REC) {
        int other = btn == H1_BTN_SEQ ? H1_BTN_REC : H1_BTN_SEQ;
        if (btn_held & (1u << other)) {
            if (!btn_fired[other]) {
                st.seq[6] ^= 1;
                toast(st.seq[6] ? "LAYERS ON" : "LAYERS OFF", 0, 0, 0, -1);
                mark_dirty();
            }
            btn_fired[btn] = btn_fired[other] = 3;
            return;
        }
    }
    switch (btn) {
    case H1_BTN_FX: open_page(PG_FX); break;
    case H1_BTN_SEL: open_page(PG_KEYS); break;
    case H1_BTN_ENV: open_page(PG_ENV); break;
    case H1_BTN_LFO: open_page(PG_LFO); break;
    case H1_BTN_ARP: open_page(PG_ARP); break;
    case H1_BTN_PLAY:
        lv_used = 0;
        lv_key = -1;
        lv_play = st.seq[6];                     /* LAYERS: held, the layer view */
        lv_defer = lv_play && seq_running();     /* (running: it stops on the way up, if PLAY was only tapped) */
        if (!lv_defer)
            eng_post(EV_PLAY, 0);
        break;
    case H1_BTN_OCTDN:
    case H1_BTN_OCTUP: {
        int both = (btn_held & (1u << (btn == H1_BTN_OCTUP ? H1_BTN_OCTDN : H1_BTN_OCTUP))) != 0;
        if (writing())                           /* WRITE: one back, a space; both, clear */
            blab_type(both ? -2 : btn == H1_BTN_OCTDN ? -1 : 26);
        else
            octave(both ? 0 : btn == H1_BTN_OCTDN ? -1 : 1);
        break;
    }
    default: break;
    }
}

/* how long SAVE is held for each reset, in milliseconds */
#define HOLD_RESET_ALGO 900u
#define HOLD_RESET_INST 2200u
#define HOLD_RESET_ALL 4000u
#define HOLD_EDIT 350u                           /* EDIT held this long shows the instrument's volume, and is not a tap */
#define HOLD_USB 1000u                           /* OCT- and OCT+ held this long: USB host, or back to the computer */

static void (*usb_fn)(int on);
static int usb_host;
void h1_usb_host_fn(void (*fn)(int on)) { usb_fn = fn; }
int h1_usb_host(void) { return usb_host; }

/* a word from the port, shown as a toast: upper case, what the font has, at most 16 characters */
const char *h1_toast_now(void) { return toast_t < 1.25f ? toast_s : ""; }

void h1_toast(const char *text)
{
    int i = 0;
    for (; text && *text && i < 16; text++) {
        char c = *text;
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.'))
            c = ' ';
        if (c == ' ' && (i == 0 || toast_s[i - 1] == ' '))
            continue;
        toast_s[i++] = c;
    }
    while (i > 0 && toast_s[i - 1] == ' ')
        i--;
    toast_s[i] = 0;
    toast_t = 0.0f;
    toast_pips = -1;
}

static APART void holds(void)
{
    layview_hold();
    /* hold EDIT: the ring comes up with the volume of the instrument in hand, for SELECT to turn. (A
     * turn of SELECT before that does the same at once: h1_encoder.) */
    if ((btn_held & (1u << H1_BTN_EDIT)) && !btn_fired[H1_BTN_EDIT] && now - btn_ms[H1_BTN_EDIT] > HOLD_EDIT) {
        btn_fired[H1_BTN_EDIT] = 1;
        ring_show(RING_INST);
    }
    /* hold REC: clear. With LAYERS on, first what was recorded on the instrument in hand, and held on to
     * three seconds, the lot (as SAVE resets a step further the longer it is held). */
    if ((btn_held & (1u << H1_BTN_REC)) && !btn_fired[H1_BTN_REC] && now - btn_ms[H1_BTN_REC] > 700u) {
        btn_fired[H1_BTN_REC] = 1;
        if (st.seq[6] && eng_seq_has(st.inst)) {
            eng_post(EV_CLEAR, 1 + st.inst);
            toast(insts[st.inst]->name, 0, 0, " CLEARED", -1);
            btn_fired[H1_BTN_REC] = 2;           /* (and the lot, if it stays down) */
        } else if (st.seq[6]) {
            toast(insts[st.inst]->name, 0, 0, " EMPTY", -1);   /* nothing of this instrument's to clear */
            btn_fired[H1_BTN_REC] = 2;
        } else {
            eng_post(EV_CLEAR, 0);
            toast(st.seq[0] == SEQ_STEP ? "STEPS CLEARED" : "LOOP CLEARED", 0, 0, 0, -1);
        }
    }
    if ((btn_held & (1u << H1_BTN_REC)) && btn_fired[H1_BTN_REC] == 2 && now - btn_ms[H1_BTN_REC] > 3000u) {
        btn_fired[H1_BTN_REC] = 1;               /* the lot only once REC is pressed again: no clearing it all by
                                                  * holding a moment too long (#49) */
        clear_all_ms = now;
        clear_all = 1;
        toast("TAP REC: CLEAR ALL", 0, 0, 0, -1);
    }
    if (clear_all && now - clear_all_ms > 3000u)
        clear_all = 0;
    if ((btn_held & (1u << H1_BTN_SEQ)) && !btn_fired[H1_BTN_SEQ] && now - btn_ms[H1_BTN_SEQ] > 550u) {
        btn_fired[H1_BTN_SEQ] = 1;
        set_seq_mode(st.seq[0] == SEQ_STEP ? SEQ_LOOP : SEQ_STEP);
        toast(O_SEQMODE[st.seq[0]], 0, 0, 0, -1);
    }
    if ((btn_held & (1u << H1_BTN_GLO)) && !btn_fired[H1_BTN_GLO] && now - btn_ms[H1_BTN_GLO] > 550u) {
        btn_fired[H1_BTN_GLO] = 1;
        st.seq[5] = (uint8_t)((st.seq[5] + 1u) % 3u);
        toast(O_CLICK[st.seq[5]], 0, 0, 0, -1);
        mark_dirty();
    }
    /* hold SAVE: back to how it came, a step further the longer it stays down. First the algorithm in
     * hand (its four knobs), then the instrument (every algorithm's knobs and its pages), then the lot. */
    if ((btn_held & (1u << H1_BTN_SAVE)) && !btn_fired[H1_BTN_SAVE] && now - btn_ms[H1_BTN_SAVE] > HOLD_RESET_ALGO) {
        int i;
        btn_fired[H1_BTN_SAVE] = 1;
        knobs_default(st.inst, st.ip[st.inst].algo);
        for (i = 0; i < 4; i++)
            knob_touch[i] = 0.0f;                /* (the strip shows where they went) */
        toast(insts[st.inst]->algo[st.ip[st.inst].algo], 0, 0, " RESET", -1);
        mark_dirty();
    }
    if ((btn_held & (1u << H1_BTN_SAVE)) && btn_fired[H1_BTN_SAVE] == 1 && now - btn_ms[H1_BTN_SAVE] > HOLD_RESET_INST) {
        int alg = st.ip[st.inst].algo, i;
        btn_fired[H1_BTN_SAVE] = 2;
        st.ip[st.inst] = insts[st.inst]->def;
        knobs_default(st.inst, -1);
        vis.algo_prev = alg;
        vis.algo = st.ip[st.inst].algo;
        vis.algo_t = 0.0f;
        for (i = 0; i < 4; i++)
            knob_touch[i] = 0.0f;
        toast(insts[st.inst]->name, 0, 0, " RESET", -1);
        mark_dirty();
    }
    if ((btn_held & (1u << H1_BTN_SAVE)) && btn_fired[H1_BTN_SAVE] == 2 && now - btn_ms[H1_BTN_SAVE] > HOLD_RESET_ALL) {
        int i;                                   /* still held: everything, as it was the first time it was switched on */
        btn_fired[H1_BTN_SAVE] = 3;
        for (i = 0; i < NINST; i++) {
            st.ip[i] = insts[i]->def;
            knobs_default(i, -1);
        }
        st.keys[0] = st.keys[1] = st.keys[2] = st.keys[3] = 0;
        st.glo[0] = 112;
        st.glo[1] = 0;
        st.glo[2] = 50;
        st.glo[3] = 100;
        st.octave = 0;
        st.hud = 0;
        eng_post(EV_UNLATCH, 0);
        toast("ALL RESET", 0, 0, 0, -1);
        mark_dirty();
    }
    if ((btn_held & (1u << H1_BTN_HOME)) && !btn_fired[H1_BTN_HOME] && now - btn_ms[H1_BTN_HOME] > 700u) {
        btn_fired[H1_BTN_HOME] = 1;
        eng_post(EV_PANIC, 0);
        toast("HUSH", 0, 0, 0, -1);
    }
    /* OCT- and OCT+ held together for a second: the USB port plays host to a keyboard, or goes back to the
     * computer. (A tap of both is the middle octave, as it was.) Never saved: every start is a USB device. */
    if ((btn_held & (1u << H1_BTN_OCTDN)) && (btn_held & (1u << H1_BTN_OCTUP)) &&
        !btn_fired[H1_BTN_OCTDN] && !btn_fired[H1_BTN_OCTUP] &&
        now - btn_ms[H1_BTN_OCTDN] > HOLD_USB && now - btn_ms[H1_BTN_OCTUP] > HOLD_USB) {
        btn_fired[H1_BTN_OCTDN] = btn_fired[H1_BTN_OCTUP] = 1;
        if (usb_fn) {
            usb_host = !usb_host;
            usb_fn(usb_host);
            toast(usb_host ? "USB HOST" : "USB COMPUTER", 0, 0, 0, -1);
        } else
            toast("NO USB HOST", 0, 0, 0, -1);
    }
}

static void set_algo(int a)
{
    InstParams *ip = &st.ip[st.inst];
    a = (a % NALGOS(st.inst) + NALGOS(st.inst)) % NALGOS(st.inst);
    if (a == ip->algo)
        return;
    vis.algo_prev = ip->algo;
    ip->algo = (uint8_t)a;
    vis.algo = a;
    vis.algo_t = 0.0f;
    toast(insts[st.inst]->algo[a], 0, 0, 0, a);
    mark_dirty();
}

static void set_tempo(int bpm)
{
    st.glo[0] = (uint8_t)iclamp(bpm, 40, 240);
    toast(0, st.glo[0], 1, " BPM", -1);
    mark_dirty();
}

static void goto_page(int pg)
{
    pg = ((pg - 1) % (NPAGE - 1) + (NPAGE - 1)) % (NPAGE - 1) + 1;
    if (pg == page)
        return;
    page = PG_HOME;                              /* (so open_page does not read it as a toggle) */
    open_page(pg);
}

static void set_knob(int i, int v)
{
    uint8_t *k = &KNOBS_NOW(st.inst)[i], was = *k;
    *k = (uint8_t)iclamp(v, 0, 100);
    knob_touch[i] = 0.0f;
    mark_dirty();
    if (*k != was && eng_midi_fn)                /* out as CC 20-23, as they come in */
        eng_mout3(0xB0u, (uint8_t)(20 + i), (uint8_t)((*k * 127 + 50) / 100));
}

static APART void set_param(int i, int v)
{
    const ParamDef *p = page_param(page, i);
    uint8_t *q = page_value(page, i);
    v = iclamp(v, p->min, p->max);
    active_col = i;
    if (v == *q)
        return;
    if (page == PG_SEQ && i == 0) {
        set_seq_mode(v);
        return;
    }
    if (page == PG_MIDI && i == OPT_CHAN)
        eng_mout3(0xB0u, 123u, 0u);              /* (what was sent on the old channel is let go there) */
    *q = (uint8_t)v;
    if (page == PG_KEYS && i == 3 && v != 1)
        eng_post(EV_UNLATCH, 0);
    if (page == PG_MIDI || page == PG_SETUP) {
        opt_pack();
        if (page == PG_MIDI && i == OPT_PEDAL && !v)
            eng_post(EV_SUSTAIN, 0);             /* (a pedal down when it was switched off lets go) */
        if (page == PG_MIDI && i == OPT_BEND && !v)
            eng_bend = 0.0f;
        if (page == PG_MIDI && i == OPT_WHEEL && !v)
            eng_wheel = 0.0f;
    }
    mark_dirty();
}

static int step_cursor_live(void) { return page == PG_HOME && seq.mode == SEQ_STEP && seq.write; }

SMALL void h1_encoder(int enc, int d)
{
    if (!d || enc < 0 || enc >= H1_NENC)
        return;
    switch (enc) {
    case H1_ENC_PRESET:
        set_instrument(((st.inst + d) % NINST + NINST) % NINST);
        break;
    case H1_ENC_ALGO:
        set_algo(st.ip[st.inst].algo + d);
        break;
    case H1_ENC_SELECT:
        if (lv_play && lv_key >= 0)              /* the layer view, a layer's key held: its volume */
            layview_volume(accel(enc, d));
        else if (btn_held & (1u << H1_BTN_EDIT)) {    /* EDIT held: the instrument's volume, on any page */
            btn_fired[H1_BTN_EDIT] = 1;
            set_inst_vol(INST_VOL(st.inst) + accel(enc, d));
        } else if (step_cursor_live())
            eng_post(EV_STEP_MOVE, iclamp(d, -100, 100) + 128);
        else if (page == PG_HOME)
            set_tempo(st.glo[0] + accel(enc, d));
        else
            goto_page(page + (d > 0 ? 1 : -1));
        break;
    default: {
        int i = enc - H1_ENC_K1;
        if (page == PG_HOME)
            set_knob(i, KNOBS_NOW(st.inst)[i] + accel(enc, d));
        else {
            const ParamDef *p = page_param(page, i);
            int step = p->kind == PK_ENUM || (p->kind == PK_INT && p->max - p->min < 16) ? d : accel(enc, d);
            set_param(i, *page_value(page, i) + step);
        }
        break;
    }
    }
}

/* ---- every knob as a number: what it is tied to right now, and setting it outright. The
 * simulator's sliders use this; the device only ever turns encoders. -------------------------- */
SMALL int h1_control(int enc, int *lo, int *hi, const char **name, const char **text)
{
    static char buf[H1_NENC][20];
    int v = 0, a = 0, b = 0;
    const char *nm = "", *tx = "";
    char *d;
    if (enc < 0 || enc >= H1_NENC)
        return 0;
    d = buf[enc];
    d[0] = 0;
    tx = d;
    switch (enc) {
    case H1_ENC_PRESET:
        v = st.inst; b = NINST - 1; nm = "PRESETS"; tx = insts[st.inst]->name;
        break;
    case H1_ENC_ALGO:
        v = st.ip[st.inst].algo; b = NALGOS(st.inst) - 1; nm = "ALGORITHM"; tx = insts[st.inst]->algo[v];
        break;
    case H1_ENC_SELECT:
        if ((btn_held & (1u << H1_BTN_EDIT)) && btn_fired[H1_BTN_EDIT]) {     /* (held, not on its way to a tap) */
            v = INST_VOL(st.inst); b = 100; nm = "VOLUME";
            put_i(d, v);
        } else if (step_cursor_live()) {
            v = seq.cursor + 1; a = 1; b = seq.nsteps; nm = "STEP";
            put_i(d, v);
        } else if (page == PG_HOME) {
            v = st.glo[0]; a = 40; b = 240; nm = "TEMPO";
            put_s(put_i(d, v), " BPM");
        } else {
            v = page; a = 1; b = NPAGE - 1; nm = "PAGE"; tx = pages[page].title;
        }
        break;
    default: {
        int i = enc - H1_ENC_K1;
        if (page == PG_HOME) {
            v = KNOBS_NOW(st.inst)[i]; b = 100; nm = insts[st.inst]->knob[i];
            put_i(d, v);
        } else {
            const ParamDef *p = page_param(page, i);
            v = *page_value(page, i); a = p->min; b = p->max; nm = p->name;
            value_text(page, i, d);
        }
        break;
    }
    }
    if (lo) *lo = a;
    if (hi) *hi = b;
    if (name) *name = nm;
    if (text) *text = tx;
    return v;
}

SMALL void h1_control_set(int enc, int v)
{
    if (enc < 0 || enc >= H1_NENC)
        return;
    switch (enc) {
    case H1_ENC_PRESET:
        v = iclamp(v, 0, NINST - 1);
        if (v != st.inst)
            set_instrument(v);
        break;
    case H1_ENC_ALGO: set_algo(iclamp(v, 0, NALGOS(st.inst) - 1)); break;
    case H1_ENC_SELECT:
        if (btn_held & (1u << H1_BTN_EDIT)) {
            btn_fired[H1_BTN_EDIT] = 1;
            if (iclamp(v, 0, 100) != INST_VOL(st.inst))
                set_inst_vol(v);
        } else if (step_cursor_live()) {
            int dlt = iclamp(v, 1, seq.nsteps) - 1 - seq.cursor;
            if (dlt) {
                eng_post(EV_STEP_MOVE, dlt + 128);
                seq.cursor += dlt;               /* (until the engine says so itself) */
            }
        } else if (page == PG_HOME) {
            if (iclamp(v, 40, 240) != st.glo[0])
                set_tempo(v);
        } else
            goto_page(iclamp(v, 1, NPAGE - 1));
        break;
    default:
        if (page == PG_HOME) {
            if (iclamp(v, 0, 100) != KNOBS_NOW(st.inst)[enc - H1_ENC_K1])
                set_knob(enc - H1_ENC_K1, v);
        } else
            set_param(enc - H1_ENC_K1, v);
        break;
    }
}

/* ---- palette ---------------------------------------------------------------------------- */
static APART void build_palette(void)
{
    const uint8_t *ink = st.ink[st.inst];
    f32 bright = (f32)st.glo[3] * 0.01f;
    int i;
    if (pal_key[0] == ink[0] && pal_key[1] == ink[1] && pal_key[2] == ink[2] && pal_key[3] == st.glo[3] && pal_key[4])
        return;
    pal_key[0] = ink[0]; pal_key[1] = ink[1]; pal_key[2] = ink[2]; pal_key[3] = st.glo[3]; pal_key[4] = 1;
    for (i = 0; i < 256; i++) {
        f32 x = (f32)i * (1.0f / 255.0f);
        f32 v = lerpf(x, sqrtfx(x), 0.3f) * bright;          /* a little lift so thin strokes hold up */
        uint32_t r = (uint32_t)((f32)ink[0] * v * (31.0f / 255.0f) + 0.5f);
        uint32_t g = (uint32_t)((f32)ink[1] * v * (63.0f / 255.0f) + 0.5f);
        uint32_t b = (uint32_t)((f32)ink[2] * v * (31.0f / 255.0f) + 0.5f);
        pal[i] = (uint16_t)((r << 11) | (g << 5) | b);
    }
    force_full = 1;
}

/* ---- drawing ---------------------------------------------------------------------------- */
static APART void draw_name(void)
{
    const char *name = insts[st.inst]->name;
    f32 t = name_t, size, w, a, y, scale;
    if (t > 1.05f)
        return;
    size = 78.0f;
    w = g_text_w(name, size);
    if (w > 204.0f)
        size *= 204.0f / w;
    scale = 1.0f + 0.14f * (1.0f - ease_out(t / 0.14f));
    a = t < 0.6f ? 1.0f : 1.0f - smooth01((t - 0.6f) / 0.45f);
    size *= scale;
    y = 120.0f - size * 0.5f;
    if (t < 0.075f) {                            /* the flash: a white band, the name cut out of it */
        g_mode(G_PAINT);
        g_rect(0.0f, y - 16.0f, 240.0f, size + 32.0f, 1.0f);
        g_text(120.0f, y, name, size, maxf(3.0f, size * 0.13f), 0.0f, 1);
        g_mode(G_MAX);
        return;
    }
    g_mode(G_PAINT);                             /* a soft black halo keeps the letters clear of the art */
    g_text(120.0f, y, name, size, maxf(3.0f, size * 0.13f) + 7.0f, 0.0f, 1);
    g_mode(G_MAX);
    g_dim(a);
    g_text(120.0f, y, name, size, maxf(3.0f, size * 0.13f), 1.0f, 1);
    g_dim(1.0f);
}

static APART void draw_still(void)
{
    fit_text(120.0f, 92.0f, insts[st.inst]->name, 40.0f, 200.0f, 0.9f);
    fit_text(120.0f, 146.0f, insts[st.inst]->algo[st.ip[st.inst].algo], 14.0f, 200.0f, 0.5f);
}

static APART void draw_hud(void)
{
    const InstDef *in = insts[st.inst];
    int i;
    for (i = 0; i < 4; i++) {
        f32 a = st.hud ? 0.9f : 1.0f - smooth01((knob_touch[i] - 0.9f) / 0.5f);
        f32 x0 = 8.0f + (f32)i * 58.0f, w = 50.0f, v = vk[i];
        if (a <= 0.01f)
            continue;
        g_mode(G_PAINT);
        g_rect(x0 - 4.0f, 0.0f, w + 8.0f, 30.0f, 0.0f);
        g_mode(G_MAX);
        g_dim(a);
        g_line(x0, 7.0f, x0 + w, 7.0f, 2.0f, 0.3f);
        g_line(x0, 7.0f, x0 + w * v, 7.0f, 4.0f, 1.0f);
        fit_text(x0 + w * 0.5f, 15.0f, in->knob[i], 10.0f, w, 1.0f);
        g_dim(1.0f);
    }
}

static APART void draw_toast(void)
{
    f32 a = 1.0f - smooth01((toast_t - 0.9f) / 0.35f), y = 206.0f, w;
    int i;
    if (a <= 0.01f || !toast_s[0])
        return;
    w = g_text_w(toast_s, 15.0f);
    g_mode(G_PAINT);
    g_rect(120.0f - w * 0.5f - 10.0f, y - 8.0f, w + 20.0f, toast_pips >= 0 ? 42.0f : 31.0f, 0.0f);
    g_mode(G_MAX);
    g_dim(a);
    g_text(120.0f, y, toast_s, 15.0f, 2.0f, 1.0f, 1);
    if (toast_pips >= 0)
        for (i = 0; i < NALGOS(st.inst); i++) {
            f32 x = 120.0f + ((f32)i - 0.5f * (f32)(NALGOS(st.inst) - 1)) * 11.0f;
            if (i == toast_pips) g_disc(x, y + 26.0f, 3.2f, 1.0f);
            else g_disc(x, y + 26.0f, 1.4f, 0.55f);
        }
    g_dim(1.0f);
}

/* Everything drawn so far inside a disc, taken down to k (0..1): what is in front of it stands clear. */
static void shade_disc(f32 cx, f32 cy, f32 r, f32 k)
{
    int k256 = (int)(sat(k) * 256.0f), y, x, y0 = imax(0, ifloor(cy - r)), y1 = imin(FB_H - 1, ifloor(cy + r) + 1);
    if (k256 >= 256)
        return;
    for (y = y0; y <= y1; y++) {
        f32 dy = (f32)y - cy, hw2 = r * r - dy * dy, hw;
        int xa, xb;
        uint8_t *row = &g_fb[y * FB_W];
        if (hw2 <= 0.0f)
            continue;
        hw = sqrtfx(hw2);
        xa = imax(0, ifloor(cx - hw + 0.5f));
        xb = imin(FB_W - 1, ifloor(cx + hw + 0.5f));
        for (x = xa; x <= xb; x++)
            row[x] = (uint8_t)((row[x] * k256) >> 8);
        if ((y & 15) == 0)
            G_POLL();
    }
}

/* The volume, while it is turned: a ring in the middle of the screen that fills the way a knob turns,
 * from seven o'clock round to five, with the number inside it and whose volume it is under that:
 * GLOBAL (MASTER) or INSTRUMENT (the one in hand: EDIT held, and SELECT or MASTER). It stays up while EDIT is
 * held. The picture behind it sits back while it is up. */
static APART void draw_volume(f32 dt)
{
    const char *label = ring_kind == RING_INST ? "INSTRUMENT" : "GLOBAL";
    const f32 cx = 120.0f, cy = 120.0f, t0 = 0.375f, sweep = 0.75f, nsz = 30.0f, nw = 3.9f;
    f32 a, r, x, te, lsz = 9.0f, lw;
    char num[8];
    if ((btn_held & (1u << H1_BTN_EDIT)) && btn_fired[H1_BTN_EDIT] && name_t >= 1.05f) {
        /* EDIT is held: the instrument's ring stays, and comes back once another volume's ring or a
         * new instrument's name has had its moment */
        if (ring_t >= RING_STAYS + 0.4f)
            ring_show(RING_INST);
        else if (ring_kind == RING_INST)
            ring_t = 0.0f;
    }
    if (!ring_kind || ring_t >= RING_STAYS + 0.4f)
        return;
    spring(&ring_x, &ring_v, ring_value(ring_kind), 40.0f, dt);
    a = 1.0f - smooth01((ring_t - RING_STAYS) / 0.4f);
    if (a <= 0.01f)
        return;
    x = sat(ring_x);
    te = t0 + sweep * x;
    r = 60.0f - 5.0f * (1.0f - ease_out(ring_up / 0.14f));    /* it opens a little as it comes up */
    put_i(num, (int)(ring_value(ring_kind) * 100.0f + 0.5f));
    lw = g_text_w(label, lsz);
    if (lw > 86.0f)
        lsz *= 86.0f / lw;

    shade_disc(cx, cy, r + 17.0f, 1.0f - 0.86f * a * sat(ring_up / 0.09f));
    g_mode(G_MAX);
    g_dim(a);
    g_arc(cx, cy, r, t0, t0 + sweep, 2.0f, 0.3f);
    if (x > 0.002f)
        g_arc(cx, cy, r, t0, te, 8.0f, 1.0f);
    g_disc(cx + r * cos_turns(te), cy + r * sin_turns(te), 6.5f, 1.0f);        /* where the volume stands */
    if (ring_kind == RING_GLOBAL && glob_held >= 0) {                          /* and the knob, until it picks it up */
        f32 tk = t0 + sweep * (f32)vol_seen * 0.001f, rk = r + 11.0f;      /* (outside the ring: seen on either side) */
        g_disc(cx + rk * cos_turns(tk), cy + rk * sin_turns(tk), 3.0f, 0.9f);
    }
    g_text(cx, cy - 20.0f, num, nsz, nw, 1.0f, 1);
    g_text(cx, cy + 19.0f, label, lsz, 1.3f, 0.8f, 1);
    g_dim(1.0f);
}

/* a tiny rounded square: the settings pages' map of the panel's buttons */
static void rsquare(f32 x, f32 y, f32 sz, f32 a)
{
    g_rect(x + 1.0f, y, sz - 2.0f, sz, a);
    g_rect(x, y + 1.0f, sz, sz - 2.0f, a);
}

/* The sequencer's mark, along the very bottom. Loop: a line that fills as the loop goes round.
 * Step: one notch a step, split into bars, a little taller on the beats; a step that holds notes
 * stands up, the one sounding is brightest, and the write cursor blinks under a dot. */
static APART void draw_seq_mark(void)
{
    int s = seq.loop_state, i, n, pb, show;
    f32 gap, pitch, w, x;
    if (seq.mode == SEQ_LOOP) {
        if (s == LOOP_EMPTY || s == LOOP_IDLE)
            return;
        if (s == LOOP_ARMED) {
            if (fractf(tsec * 2.0f) < 0.5f)
                g_disc(228.0f, 228.0f, 4.0f, 1.0f);
            return;
        }
        g_line(0.0f, 238.5f, 240.0f * seq.loop_pos, 238.5f, 2.0f, s == LOOP_PLAY ? 0.6f : 1.0f);
        if (s != LOOP_PLAY)
            g_disc(228.0f, 228.0f, 4.0f, 1.0f);
        if (st.seq[6])                           /* LAYERS: a dot a layer, the one being recorded blinking */
            for (i = 0; i < layers_n && i < NLAYER; i++) {
                int rec = layer_rec_was && i == layers_n - 1;
                g_disc(8.0f + (f32)i * 7.0f, 230.0f, 2.2f, rec ? (fractf(tsec * 2.0f) < 0.5f ? 1.0f : 0.3f) : 0.75f);
            }
        return;
    }
    n = seq.nsteps;
    pb = seq.per_bar;
    show = seq.playing || seq.write || page == PG_SEQ;
    for (i = 0; i < n && !show; i++)
        show = st.steps[i][0] != 0;
    if (!show || n < 1)
        return;
    gap = seq.bars > 1 ? 5.0f : 0.0f;
    pitch = (232.0f - gap * (f32)(seq.bars - 1)) / (f32)n;
    w = clampf((f32)ifloor(pitch - 1.0f), 1.0f, 3.0f);
    for (i = 0; i < n; i++) {
        int acc = seq_accent((i % pb) * seq_grid_ticks()), full = st.steps[i][0] != 0;
        int at = seq.playing && i == seq.at, cur = seq.write && !seq.playing && i == seq.cursor;
        f32 h = full ? 7.0f : (acc == 2 ? 4.0f : (acc ? 3.0f : 2.0f)), a = full ? 0.85f : (acc ? 0.5f : 0.3f);
        x = 4.0f + (f32)i * pitch + gap * (f32)(i / pb) + (pitch - w) * 0.5f;
        x = (f32)ifloor(x + 0.5f);
        if (at) {
            h = full ? 10.0f : 5.0f;
            a = 1.0f;
        }
        if (cur) {
            f32 bl = 0.45f + 0.55f * (fractf(tsec * 2.2f) < 0.6f ? 1.0f : 0.0f);
            h = maxf(h, 4.0f);
            a = maxf(a, bl);
            g_mode(G_PAINT);
            g_disc(x + w * 0.5f - 0.5f, 224.0f, 3.0f, 0.0f);
            g_mode(G_MAX);
            g_disc(x + w * 0.5f - 0.5f, 224.0f, 1.6f, 1.0f);
        }
        g_mode(G_PAINT);                         /* a hair of black round each notch keeps it clear of the art */
        g_rect(x - 1.0f, 238.0f - h, w + 2.0f, h + 2.0f, 0.0f);
        g_mode(G_MAX);
        g_rect(x, 239.0f - h, w, h, a);
    }
}

static APART void draw_page(void)
{
    const PageDef *pd = &pages[page];
    char buf[28];
    int i, j;
    f32 intro = ease_out(page_t / 0.22f);
    g_text(12.0f, 14.0f, pd->title, 22.0f, 3.0f, 1.0f, 0);
    if (page == PG_GLO) {                        /* what holding this page's button does */
        put_s(put_s(buf, "HOLD GLO: "), O_CLICK[st.seq[5] < 3 ? st.seq[5] : 0]);
        g_text(13.0f, 43.0f, buf, 8.0f, 1.0f, 0.5f * intro, 0);
    }
    if (page == PG_SEQ) {
        /* The sequencer's buttons do three things held or paired, more than the line holds: they go by
         * as a marquee, still for a moment when the page opens, then round and round. */
        const char *hint[4];
        f32 wd[4], total = 0.0f, off, x;
        int nh = st.seq[6] ? 4 : 3;
        hint[0] = "HOLD SEQ: LOOP / STEP";
        hint[1] = st.seq[6] ? "SEQ + REC: LAYERS ON" : "SEQ + REC: LAYERS OFF";
        hint[2] = st.seq[6] ? "HOLD REC: CLEAR PART, THEN ALL" : "HOLD REC: CLEAR";
        hint[3] = "NEW INSTRUMENT, REC: A NEW LAYER";
        for (i = 0; i < nh; i++) {
            wd[i] = g_text_w(hint[i], 8.0f) + 22.0f;
            total += wd[i];
        }
        off = maxf(0.0f, page_t - 1.4f) * 26.0f;
        off -= total * (f32)ifloor(off / total);
        x = 13.0f - off;
        for (j = 0; j < 2 * nh; j++) {           /* twice round: the line never shows its end */
            i = j % nh;
            if (x + wd[i] > 0.0f && x < 240.0f) {
                g_text(x, 43.0f, hint[i], 8.0f, 1.0f, 0.5f * intro, 0);
                g_disc(x + wd[i] - 11.0f, 47.0f, 1.2f, 0.35f * intro);
            }
            x += wd[i];
        }
        g_mode(G_PAINT);                         /* a margin each side, as the page's other text keeps */
        g_rect(0.0f, 40.0f, 11.0f, 14.0f, 0.0f);
        g_rect(229.0f, 40.0f, 11.0f, 14.0f, 0.0f);
        g_mode(G_MAX);
    }
    for (i = 1; i < PG_BUTTONS; i++)             /* the panel's two rows of six: a square where a button has a page */
        rsquare(165.0f + (f32)PAGE_AT[i][0] * 11.0f, 12.0f + (f32)PAGE_AT[i][1] * 11.0f, 8.0f, i == page ? 1.0f : 0.3f);
    for (i = 0; i < 4; i++) {
        const ParamDef *p = page_param(page, i);
        f32 cx = 30.0f + (f32)i * 60.0f;
        f32 lift = (1.0f - ease_out((page_t - (f32)i * 0.035f) / 0.24f)) * 26.0f;
        f32 a = (i == active_col ? 1.0f : 0.62f) * intro;
        f32 top = 62.0f + lift, bot = 150.0f + lift, h = bot - top;
        int v = *page_value(page, i);
        f32 tgt = p->max > p->min ? (f32)(v - p->min) / (f32)(p->max - p->min) : 0.0f;
        spring(&col_v[i], &col_vv[i], tgt, 26.0f, vis.dt);
        g_dim(a);
        if (p->kind == PK_ENUM) {                /* one dash per option, the chosen one wide */
            int n = p->max + 1;
            f32 gap = n > 1 ? h / (f32)(n - 1) : 0.0f, sel = bot - col_v[i] * h;
            if (n > 9) gap = h / (f32)(n - 1);
            for (j = 0; j < n; j++) {
                f32 y = bot - (f32)j * gap;
                g_line(cx - 5.0f, y, cx + 5.0f, y, 2.0f, 0.4f);
            }
            g_line(cx - 15.0f, sel, cx + 15.0f, sel, 5.0f, 1.0f);
        } else {                                 /* a column that fills */
            f32 y = bot - col_v[i] * h;
            g_line(cx, top, cx, bot, 2.0f, 0.3f);
            if (p->show_off < 0) {               /* bipolar: grows from the middle */
                f32 mid = (top + bot) * 0.5f;
                g_line(cx - 9.0f, mid, cx + 9.0f, mid, 2.0f, 0.5f);
                g_line(cx, mid, cx, y, 9.0f, 1.0f);
            } else
                g_line(cx, bot, cx, y, 9.0f, 1.0f);
            g_line(cx - 13.0f, y, cx + 13.0f, y, 3.0f, 1.0f);
        }
        value_text(page, i, buf);
        fit_text(cx, 166.0f + lift, buf, 17.0f, 50.0f, 1.0f);
        fit_text(cx, 196.0f + lift, p->name, 10.0f, 48.0f, 0.75f);
        g_line(cx - 3.0f, 222.0f, cx + 3.0f, 222.0f, 2.0f, i == active_col ? 1.0f : 0.35f);
        g_dim(1.0f);
    }
}

void ui_frame(uint32_t now_ms)
{
    f32 dt;
    int i;
    now = now_ms;
    dt = clampf((f32)(now - last_ms) * 0.001f, 0.001f, 0.05f);
    if (!frame_n)
        dt = 0.016f;
    last_ms = now;
    frame_n++;
    tsec += dt;
    holds();

    eng_snapshot(&vis);
    eng_seq_info(&seq);
    if (seq.write != seq_write_was) {
        seq_write_was = seq.write;
        toast(seq.write ? "WRITE" : "WRITE OFF", 0, 0, 0, -1);
    }
    if (st.seq[6] && ((frame_n & 7u) == 0 || layer_rec_was != eng_layer_rec()))
        layers_n = eng_layer_count();
    {
        int lr = st.seq[6] ? eng_layer_rec() : 0;   /* LAYERS: a take has begun on a new layer */
        if (lr && !layer_rec_was)
            toast("LAYER ", layers_n, 1, 0, -1);
        layer_rec_was = lr;
    }
    if (eng_loop_full() != loop_full_was) {          /* the loop has no room for another note: say so, every */
        loop_full_was = eng_loop_full();             /* time one is turned away, rather than lose it quietly */
        toast("LOOP FULL", 0, 0, 0, -1);
    }
    for (i = 0; i < 4; i++) {
        if (vis.fresh) {
            vk[i] = vis.k[i];
            vkv[i] = 0.0f;
        }
        spring(&vk[i], &vkv[i], vis.k[i], 32.0f, dt);
        vis.k[i] = sat(vk[i]);
        knob_touch[i] += dt;
    }
    vis.t = tsec;
    vis.dt = dt;
    vis.rng = &vrng;
    vis.algo = st.ip[st.inst].algo;
    vis.algo_t = sat(vis.algo_t + dt / 0.4f);
    name_t += dt;
    page_t += dt;
    toast_t += dt;
    vol_t += dt;
    ring_t += dt;
    ring_up += dt;

    build_palette();
    g_clear();
    g_mode(G_MAX);
    g_dim(1.0f);
    g_origin(0.0f, 0.0f);
    if (page == PG_HOME) {
        /* the art sits back while the name is up, then comes forward */
        g_dim(name_t < 0.55f ? 0.3f : 0.3f + 0.7f * smooth01((name_t - 0.55f) / 0.4f));
        if (opt[OPT_ANIM])                       /* SETUP ANIM OFF: no picture, the name and the algorithm still */
            draw_still();
        else
            insts[st.inst]->draw(&vis);
        g_dim(1.0f);
        g_mode(G_MAX);
        g_origin(0.0f, 0.0f);
        draw_hud();
        draw_seq_mark();
        draw_toast();
        draw_name();
    } else {
        draw_page();
        draw_seq_mark();
        draw_toast();
    }
    if (layview_on())
        draw_layview();
    draw_volume(dt);
    vis.fresh = 0;
    vis.enter += dt;
}

SMALL void ui_init(void)
{
    vis.fresh = 1;
    vis.algo = vis.algo_prev = st.ip[st.inst].algo;
    vis.algo_t = 1.0f;
    name_t = 0.0f;
    force_full = 1;
    rq_n = rq_i = 0;                             /* (nothing queued for the panel) */
    pal_key[4] = 0;
    dirty = 0;                                   /* what is in RAM is what was loaded: nothing to save */
    btn_held = 0;                                /* (and no button is down) */
    ring_kind = 0;
    lv_play = lv_defer = 0;
    lv_key = -1;
    clear_all = 0;
    glob_held = -1;
    master_raw = -1;
    ring_t = 9.0f;
}

/* ---- presenting: only what changed goes to the panel -----------------------------------------
 * A row is 30 tiles of 8 pixels. A tile is dirty when a pixel in it would show another colour than
 * the panel has now (two ink levels that come out as the same colour are the same). Dirty rows are
 * taken up to 20 at a time, and inside such a band every run of dirty tiles goes out as its own
 * rectangle, cut down to the rows that touch it: a ring, or two things far apart, do not drag the
 * unchanged pixels between them along. */
#define TILES (FB_W / 8)
#define PRESENT_RECT 200                 /* what a rectangle costs besides its pixels, in bytes' worth of time */
#define PRESENT_GAP 256                  /* clean pixels' bytes it is cheaper to send than to start another rectangle */
static uint32_t row_tiles(int y)
{
    const uint32_t *wa = &g_fb_w[y * (FB_W / 4)], *wb = &prev_w[y * (FB_W / 4)];
    uint32_t m = 0;
    int t, i;
    if (force_full)
        return (1u << TILES) - 1u;
    for (t = 0; t < TILES; t++, wa += 2, wb += 2) {
        const uint8_t *a, *b;
        if (wa[0] == wb[0] && wa[1] == wb[1])
            continue;
        a = (const uint8_t *)wa;
        b = (const uint8_t *)wb;
        for (i = 0; i < 8; i++)
            if (a[i] != b[i] && pal[a[i]] != pal[b[i]]) {
                m |= 1u << t;
                break;
            }
    }
    return m;
}

/* What changed is found in one pass and queued as rectangles, and the changed pixels are copied to
 * the panel's copy as they are found. From then on a rectangle is read from that copy, not from the
 * framebuffer: the queue can go out to the panel while the next frame is being drawn. */

void h1_present_begin(void)
{
    int y = 0;
    rq_n = rq_i = 0;
    rq_bytes = 0;
    while (y < FB_H) {
        uint32_t rm[20], all = row_tiles(y);
        int n = 0, y0 = y, t = 0;
        if (!all) {
            y++;
            continue;
        }
        rm[n++] = all;
        for (y++; y < FB_H && n < 20; y++) {
            uint32_t m = row_tiles(y);
            if (!m)
                break;
            rm[n++] = m;
            all |= m;
        }
        while (t < TILES) {
            uint32_t run;
            int t0, t1, r0 = 0, r1 = n - 1, yy, x0, x1;
            if (!(all >> t & 1u)) {
                t++;
                continue;
            }
            t0 = t1 = t;
            for (;;) {                           /* on through the dirty tiles, and over a gap too small to be worth a rectangle of its own */
                int u = t1 + 1;
                while (u < TILES && !(all >> u & 1u))
                    u++;
                if (u >= TILES)
                    break;
                if ((u - t1 - 1) * 8 * n * 2 > PRESENT_GAP && rq_n < RQ_MAX - FB_H)
                    break;                       /* (a queue that is nearly full takes whole bands: it cannot overflow) */
                t1 = u;
            }
            run = ((t1 - t0 + 1 >= 32 ? 0u : 1u << (t1 - t0 + 1)) - 1u) << t0;
            while (!(rm[r0] & run)) r0++;
            while (!(rm[r1] & run)) r1--;
            x0 = t0 * 8;
            x1 = t1 * 8 + 7;
            for (yy = y0 + r0; yy <= y0 + r1; yy++) {
                const uint32_t *src = &g_fb_w[(yy * FB_W + x0) / 4];
                uint32_t *dst = &prev_w[(yy * FB_W + x0) / 4];
                int k, nw = (x1 - x0 + 1) / 4;
                for (k = 0; k < nw; k++)
                    dst[k] = src[k];
            }
            rq[rq_n][0] = (uint8_t)x0;
            rq[rq_n][1] = (uint8_t)(y0 + r0);
            rq[rq_n][2] = (uint8_t)(x1 - x0 + 1);
            rq[rq_n][3] = (uint8_t)(r1 - r0 + 1);
            rq_n++;
            rq_bytes += (uint32_t)((x1 - x0 + 1) * (r1 - r0 + 1) * 2 + PRESENT_RECT);
            t = t1 + 1;
        }
    }
    force_full = 0;
}

int h1_present_next(int *x, int *y, int *w, int *h)
{
    if (rq_i >= rq_n)
        return 0;
    *x = rq[rq_i][0];
    *y = rq[rq_i][1];
    *w = rq[rq_i][2];
    *h = rq[rq_i][3];
    rq_i++;
    return 1;
}

int h1_present_left(void) { return rq_n - rq_i; }
uint32_t h1_present_bytes(void) { return rq_bytes; }
int h1_present_rects(void) { return rq_n; }
const uint8_t *h1_panel(void) { return prev_fb; }
void h1_while_drawing(void (*fn)(void)) { g_poll_fn = fn; }

/* all of it at once, for a port that has nothing better to do while it waits */
uint32_t h1_present(h1_blit_fn blit)
{
    int x, y, w, h;
    h1_present_begin();
    while (h1_present_next(&x, &y, &w, &h))
        if (blit)
            blit(x, y, w, h);
    return rq_bytes;
}

const uint8_t *h1_framebuffer(void) { return g_fb; }
const uint16_t *h1_palette(void) { return pal; }

/* ---- lights ----------------------------------------------------------------------------- */
uint32_t h1_key_leds(void)
{
    uint32_t m;
    if (writing())                               /* WRITE: the vowels lit, and the space */
        return 1u << 0 | 1u << 4 | 1u << 8 | 1u << 14 | 1u << 20 | 1u << 26;
    /* The sequencer has the keys on the SEQ page and while steps are written; elsewhere they show what
     * sounds, as they do while playing alone (#49). SETUP's LEDS SEQ: the sequencer has them while it runs. */
    if (layview_on())
        return layview_leds();
    if ((page == PG_SEQ || seq.write || opt[OPT_LEDS]) && eng_seq_leds(&m, page == PG_SEQ, fractf(tsec * 2.2f) < 0.6f))
        return m;
    return eng_sounding_lo();
}

uint32_t h1_button_leds(void)
{
    static const int8_t PAGE_BTN[PG_BUTTONS] = {H1_BTN_HOME, H1_BTN_FX, H1_BTN_SEL, H1_BTN_ENV, H1_BTN_LFO, H1_BTN_GLO, H1_BTN_ARP, H1_BTN_SEQ};
    uint32_t m = 0;
    int s = seq.mode == SEQ_LOOP ? seq.loop_state : LOOP_EMPTY, blink = fractf(tsec * 2.5f) < 0.5f;
    if (page != PG_HOME && page < PG_BUTTONS)
        m |= 1u << PAGE_BTN[page];
    if (st.ip[st.inst].arp[0] && (page == PG_ARP || vis.beat < 0.25f))
        m |= 1u << H1_BTN_ARP;
    if (st.hud || (writing() && blink)) m |= 1u << H1_BTN_EDIT;
    if (st.keys[3]) m |= 1u << H1_BTN_SEL;
    if (s == LOOP_PLAY || s == LOOP_DUB || s == LOOP_REC) m |= 1u << H1_BTN_PLAY;
    if (s == LOOP_REC || s == LOOP_DUB || (s == LOOP_ARMED && blink)) m |= 1u << H1_BTN_REC;
    if (seq.mode == SEQ_STEP) {
        if (seq.playing) m |= 1u << H1_BTN_PLAY;
        if (seq.write) m |= 1u << H1_BTN_REC;
    }
    if (st.octave < 0) m |= 1u << H1_BTN_OCTDN;
    if (st.octave > 0) m |= 1u << H1_BTN_OCTUP;
    return m;
}

/* ---- saved state ------------------------------------------------------------------------ */
int h1_state_dirty(void) { return dirty && now - dirty_ms > 1500u; }
int h1_state_asked(void) { return save_asked; }
void h1_state_saved(void) { dirty = save_asked = 0; }
