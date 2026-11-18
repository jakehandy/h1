/* H1 - the panel and the screen around the instruments: what every knob and button does, the
 * home overlays (the name flash, the knob strip, toasts) and the settings pages, which are one
 * layout for every page and every instrument: four columns, one per knob, left to right.
 *
 *   PRESETS    the instrument               ALGORITHM   its algorithm (8 each)
 *   KNOB 1-4   home: the instrument's four live modulations. A page: its four columns.
 *   SELECT     home: tempo (the step cursor while writing steps). A page: the next or previous page.
 *   FX SEL ENV LFO GLO ARP SEQ   open that page; again, or HOME, goes back
 *   SEQ held   swaps the sequencer between LOOP and STEP
 *   EDIT       pins the knob strip on the home screen (writing steps: a rest)
 *   SAVE       saves now; held, resets this instrument
 *   PLAY REC   the sequencer. Loop: REC arms a take, or overdubs while it plays. Step: REC is
 *              step-write. REC held clears.
 *   OCT- OCT+  octave; both, back to the middle
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
static const char *const O_BARS[] = {"1", "2", "4", "8"};
static const char *const O_QUANT[] = {"FREE", "1/16", "1/8"};
static const char *const O_SEQMODE[] = {"LOOP", "STEP"};
static const char *const O_METER[] = {"4/4", "3/4", "5/4", "6/8", "7/8"};
static const char *const O_GRID[] = {"1/8", "1/16", "1/32"};

/* in the order of the buttons that open them, reading the panel's two rows left to right */
const PageDef pages[NPAGE] = {
    {"HOME", {{0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0}}},
    {"FX", {{"ECHO", PK_PCT, 100, 0, 0, 0}, {"TIME", PK_ENUM, 7, 0, 0, O_TIME},
            {"SPACE", PK_PCT, 100, 0, 0, 0}, {"TONE", PK_PCT, 100, 0, -50, 0}}},
    {"KEYS", {{"ROOT", PK_ENUM, 11, 0, 0, O_ROOT}, {"SCALE", PK_ENUM, 7, 0, 0, O_SCALE},
              {"GLIDE", PK_PCT, 100, 0, 0, 0}, {"HOLD", PK_ENUM, 1, 0, 0, O_ONOFF}}},
    {"ENV", {{"ATTACK", PK_PCT, 100, 0, 0, 0}, {"DECAY", PK_PCT, 100, 0, 0, 0},
             {"SUSTAIN", PK_PCT, 100, 0, 0, 0}, {"RELEASE", PK_PCT, 100, 0, 0, 0}}},
    {"LFO", {{"SHAPE", PK_ENUM, 5, 0, 0, O_SHAPE}, {"RATE", PK_PCT, 100, 0, 0, 0},
             {"DEPTH", PK_PCT, 100, 0, 0, 0}, {"TO", PK_ENUM, 6, 0, 0, O_TARGET}}},
    {"GLOBAL", {{"TEMPO", PK_INT, 240, 40, 0, 0}, {"SWING", PK_PCT, 100, 0, 0, 0},
                {"TUNE", PK_PCT, 100, 0, -50, 0}, {"BRIGHT", PK_PCT, 100, 20, 0, 0}}},
    {"ARP", {{"MODE", PK_ENUM, 6, 0, 0, O_ARPMODE}, {"RATE", PK_ENUM, 5, 0, 0, O_RATE},
             {"RANGE", PK_INT, 4, 1, 0, 0}, {"GATE", PK_PCT, 100, 5, 0, 0}}},
    {"SEQ", {{"MODE", PK_ENUM, 1, 0, 0, O_SEQMODE}, {"BARS", PK_ENUM, 3, 0, 0, O_BARS},
             {"METER", PK_ENUM, 4, 0, 0, O_METER}, {"SNAP", PK_ENUM, 2, 0, 0, O_QUANT}}},
};
static const ParamDef P_GRID = {"GRID", PK_ENUM, 2, 0, 0, O_GRID};

/* where each page's button sits on the panel: column 0..5, row 0..1 */
static const uint8_t PAGE_AT[NPAGE][2] = {{0, 1}, {0, 0}, {1, 0}, {2, 0}, {3, 0}, {5, 0}, {2, 1}, {3, 1}};

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
    default: return &ip->knob[i];
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
static char toast_s[20];
static f32 toast_t = 9.0f;
static int toast_pips = -1;

static SeqInfo seq;                              /* the sequencer, as of this frame */
static int seq_write_was;
static uint32_t enc_ms[H1_NENC];
static uint8_t key_note[H1_NKEYS];
static uint8_t key_down[H1_NKEYS];
static uint32_t btn_held;
static uint32_t btn_ms[H1_NBTN];
static uint8_t btn_fired[H1_NBTN];

static uint16_t pal[256];
static uint8_t pal_key[5];
static uint8_t prev_fb[FB_W * FB_H] H1_POOL;
static int force_full = 1;
static int dirty;
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

static void value_text(int pg, int i, char *out)
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

static void set_instrument(int i)
{
    st.inst = (uint8_t)i;
    eng_post(EV_INST, i);
    name_t = 0.0f;
    page = PG_HOME;
    vis.fresh = 1;
    vis.enter = 0.0f;
    vis.algo = vis.algo_prev = st.ip[i].algo;
    vis.algo_t = 1.0f;
    toast_t = 9.0f;
    mark_dirty();
}

static void open_page(int pg)
{
    int i;
    if (page == pg)
        pg = PG_HOME;
    page = pg;
    page_t = 0.0f;
    if (pg != PG_HOME)
        for (i = 0; i < 4; i++) {
            const ParamDef *p = page_param(pg, i);
            col_v[i] = p->max > p->min ? (f32)(*page_value(pg, i) - p->min) / (f32)(p->max - p->min) : 0.0f;
            col_vv[i] = 0.0f;
        }
}

static void set_seq_mode(int m)
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

void h1_key(int key, int down)
{
    if (key < 0 || key >= H1_NKEYS)
        return;
    if (down) {
        int note = scale_snap(53 + key + 12 * st.octave);
        if (key_down[key])
            return;
        note = iclamp(note, 0, 127);
        key_down[key] = 1;
        key_note[key] = (uint8_t)note;
        eng_post(EV_NOTE_ON, note);
    } else if (key_down[key]) {
        key_down[key] = 0;
        eng_post(EV_NOTE_OFF, key_note[key]);
    }
}

void h1_midi(uint8_t status, uint8_t d1, uint8_t d2)
{
    uint8_t type = status & 0xF0u;
    if (type == 0x90u && d2)
        eng_post(EV_NOTE_ON, d1 & 127);
    else if (type == 0x80u || type == 0x90u)
        eng_post(EV_NOTE_OFF, d1 & 127);
    else if (type == 0xB0u && d1 == 123)
        eng_post(EV_PANIC, 0);
    else if (type == 0xB0u && d1 >= 20 && d1 <= 23) {      /* CC 20-23: the four knobs */
        st.ip[st.inst].knob[d1 - 20] = (uint8_t)((d2 * 100 + 63) / 127);
        knob_touch[d1 - 20] = 0.0f;
    }
}

static void octave(int d)
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

void h1_button(int btn, int down)
{
    if (btn < 0 || btn >= H1_NBTN)
        return;
    if (!down) {
        btn_held &= ~(1u << btn);
        if (btn == H1_BTN_REC && !btn_fired[btn])
            eng_post(EV_REC, 0);
        if (btn == H1_BTN_SAVE && !btn_fired[btn]) {
            dirty = 1;
            dirty_ms = now - 5000u;
            toast("SAVED", 0, 0, 0, -1);
        }
        return;
    }
    btn_held |= 1u << btn;
    btn_ms[btn] = now;
    btn_fired[btn] = 0;
    switch (btn) {
    case H1_BTN_FX: open_page(PG_FX); break;
    case H1_BTN_SEL: open_page(PG_KEYS); break;
    case H1_BTN_ENV: open_page(PG_ENV); break;
    case H1_BTN_LFO: open_page(PG_LFO); break;
    case H1_BTN_GLO: open_page(PG_GLO); break;
    case H1_BTN_ARP: open_page(PG_ARP); break;
    case H1_BTN_SEQ: open_page(PG_SEQ); break;
    case H1_BTN_EDIT:
        if (seq.mode == SEQ_STEP && seq.write && page == PG_HOME) {
            eng_post(EV_STEP_REST, 0);           /* writing steps: a rest */
            break;
        }
        st.hud ^= 1;
        mark_dirty();
        break;
    case H1_BTN_HOME:
        if (page == PG_HOME)
            name_t = 0.0f;
        page = PG_HOME;
        break;
    case H1_BTN_PLAY: eng_post(EV_PLAY, 0); break;
    case H1_BTN_OCTDN:
        octave((btn_held & (1u << H1_BTN_OCTUP)) ? 0 : -1);
        break;
    case H1_BTN_OCTUP:
        octave((btn_held & (1u << H1_BTN_OCTDN)) ? 0 : 1);
        break;
    default: break;
    }
}

static void holds(void)
{
    if ((btn_held & (1u << H1_BTN_REC)) && !btn_fired[H1_BTN_REC] && now - btn_ms[H1_BTN_REC] > 700u) {
        btn_fired[H1_BTN_REC] = 1;
        eng_post(EV_CLEAR, 0);
        toast(st.seq[0] == SEQ_STEP ? "STEPS CLEARED" : "LOOP CLEARED", 0, 0, 0, -1);
    }
    if ((btn_held & (1u << H1_BTN_SEQ)) && !btn_fired[H1_BTN_SEQ] && now - btn_ms[H1_BTN_SEQ] > 550u) {
        btn_fired[H1_BTN_SEQ] = 1;
        set_seq_mode(st.seq[0] == SEQ_STEP ? SEQ_LOOP : SEQ_STEP);
        toast(O_SEQMODE[st.seq[0]], 0, 0, 0, -1);
    }
    if ((btn_held & (1u << H1_BTN_SAVE)) && !btn_fired[H1_BTN_SAVE] && now - btn_ms[H1_BTN_SAVE] > 900u) {
        int alg = st.ip[st.inst].algo;
        btn_fired[H1_BTN_SAVE] = 1;
        st.ip[st.inst] = insts[st.inst]->def;
        vis.algo_prev = alg;
        vis.algo = st.ip[st.inst].algo;
        vis.algo_t = 0.0f;
        toast("RESET", 0, 0, 0, -1);
        mark_dirty();
    }
    if ((btn_held & (1u << H1_BTN_HOME)) && !btn_fired[H1_BTN_HOME] && now - btn_ms[H1_BTN_HOME] > 700u) {
        btn_fired[H1_BTN_HOME] = 1;
        eng_post(EV_PANIC, 0);
        toast("HUSH", 0, 0, 0, -1);
    }
}

static void set_algo(int a)
{
    InstParams *ip = &st.ip[st.inst];
    a = (a % NALGO + NALGO) % NALGO;
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
    st.ip[st.inst].knob[i] = (uint8_t)iclamp(v, 0, 100);
    knob_touch[i] = 0.0f;
    mark_dirty();
}

static void set_param(int i, int v)
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
    *q = (uint8_t)v;
    if (page == PG_KEYS && i == 3 && !v)
        eng_post(EV_UNLATCH, 0);
    mark_dirty();
}

static int step_cursor_live(void) { return page == PG_HOME && seq.mode == SEQ_STEP && seq.write; }

void h1_encoder(int enc, int d)
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
        if (step_cursor_live())
            eng_post(EV_STEP_MOVE, iclamp(d, -100, 100) + 128);
        else if (page == PG_HOME)
            set_tempo(st.glo[0] + accel(enc, d));
        else
            goto_page(page + (d > 0 ? 1 : -1));
        break;
    default: {
        int i = enc - H1_ENC_K1;
        if (page == PG_HOME)
            set_knob(i, st.ip[st.inst].knob[i] + accel(enc, d));
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
int h1_control(int enc, int *lo, int *hi, const char **name, const char **text)
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
        v = st.ip[st.inst].algo; b = NALGO - 1; nm = "ALGORITHM"; tx = insts[st.inst]->algo[v];
        break;
    case H1_ENC_SELECT:
        if (step_cursor_live()) {
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
            v = st.ip[st.inst].knob[i]; b = 100; nm = insts[st.inst]->knob[i];
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

void h1_control_set(int enc, int v)
{
    if (enc < 0 || enc >= H1_NENC)
        return;
    switch (enc) {
    case H1_ENC_PRESET:
        v = iclamp(v, 0, NINST - 1);
        if (v != st.inst)
            set_instrument(v);
        break;
    case H1_ENC_ALGO: set_algo(iclamp(v, 0, NALGO - 1)); break;
    case H1_ENC_SELECT:
        if (step_cursor_live()) {
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
            if (iclamp(v, 0, 100) != st.ip[st.inst].knob[enc - H1_ENC_K1])
                set_knob(enc - H1_ENC_K1, v);
        } else
            set_param(enc - H1_ENC_K1, v);
        break;
    }
}

/* ---- palette ---------------------------------------------------------------------------- */
static void build_palette(void)
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
static void draw_name(void)
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

static void draw_hud(void)
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

static void draw_toast(void)
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
        for (i = 0; i < NALGO; i++) {
            f32 x = 120.0f + ((f32)i - 3.5f) * 11.0f;
            if (i == toast_pips) g_disc(x, y + 26.0f, 3.2f, 1.0f);
            else g_disc(x, y + 26.0f, 1.4f, 0.55f);
        }
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
static void draw_seq_mark(void)
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

static void draw_page(void)
{
    const PageDef *pd = &pages[page];
    char buf[20];
    int i, j;
    f32 intro = ease_out(page_t / 0.22f);
    g_text(12.0f, 14.0f, pd->title, 22.0f, 3.0f, 1.0f, 0);
    for (i = 1; i < NPAGE; i++)                  /* the panel's two rows of six: a square where a button has a page */
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

    build_palette();
    g_clear();
    g_mode(G_MAX);
    g_dim(1.0f);
    g_origin(0.0f, 0.0f);
    if (page == PG_HOME) {
        /* the art sits back while the name is up, then comes forward */
        g_dim(name_t < 0.55f ? 0.3f : 0.3f + 0.7f * smooth01((name_t - 0.55f) / 0.4f));
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
    vis.fresh = 0;
    vis.enter += dt;
}

void ui_init(void)
{
    vis.fresh = 1;
    vis.algo = vis.algo_prev = st.ip[st.inst].algo;
    vis.algo_t = 1.0f;
    name_t = 0.0f;
    force_full = 1;
    pal_key[4] = 0;
}

/* ---- presenting ------------------------------------------------------------------------- */
static int row_range(int y, int *x0, int *x1)
{
    const uint8_t *a = &g_fb[y * FB_W], *b = &prev_fb[y * FB_W];
    int l = 0, r = FB_W - 1;
    if (force_full) {
        *x0 = 0;
        *x1 = FB_W - 1;
        return 1;
    }
    while (l < FB_W && a[l] == b[l])
        l++;
    if (l == FB_W)
        return 0;
    while (a[r] == b[r])
        r--;
    *x0 = l;
    *x1 = r;
    return 1;
}

uint32_t h1_present(h1_blit_fn blit)
{
    uint32_t bytes = 0;
    int y = 0;
    while (y < FB_H) {
        int x0, x1, bx0, bx1, y0, yy, xx;
        if (!row_range(y, &x0, &x1)) {
            y++;
            continue;
        }
        y0 = y;
        bx0 = x0;
        bx1 = x1;
        y++;
        while (y < FB_H && y - y0 < 20 && row_range(y, &x0, &x1)) {
            /* stop the band where it would start to carry a lot of unchanged pixels */
            int nb0 = x0 < bx0 ? x0 : bx0, nb1 = x1 > bx1 ? x1 : bx1;
            if ((nb1 - nb0) > (bx1 - bx0) + 48 && (nb1 - nb0) > 2 * (x1 - x0 + 1))
                break;
            bx0 = nb0;
            bx1 = nb1;
            y++;
        }
        for (yy = y0; yy < y; yy++)
            for (xx = bx0; xx <= bx1; xx++)
                prev_fb[yy * FB_W + xx] = g_fb[yy * FB_W + xx];
        if (blit)
            blit(bx0, y0, bx1 - bx0 + 1, y - y0);
        bytes += (uint32_t)((bx1 - bx0 + 1) * (y - y0) * 2 + 16);
    }
    force_full = 0;
    return bytes;
}

const uint8_t *h1_framebuffer(void) { return g_fb; }
const uint16_t *h1_palette(void) { return pal; }

/* ---- lights ----------------------------------------------------------------------------- */
uint32_t h1_key_leds(void) { return eng_sounding_lo(); }

uint32_t h1_button_leds(void)
{
    static const int8_t PAGE_BTN[NPAGE] = {H1_BTN_HOME, H1_BTN_FX, H1_BTN_SEL, H1_BTN_ENV, H1_BTN_LFO, H1_BTN_GLO, H1_BTN_ARP, H1_BTN_SEQ};
    uint32_t m = 0;
    int s = seq.mode == SEQ_LOOP ? seq.loop_state : LOOP_EMPTY, blink = fractf(tsec * 2.5f) < 0.5f;
    if (page != PG_HOME)
        m |= 1u << PAGE_BTN[page];
    if (st.ip[st.inst].arp[0] && (page == PG_ARP || vis.beat < 0.25f))
        m |= 1u << H1_BTN_ARP;
    if (st.hud) m |= 1u << H1_BTN_EDIT;
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
void h1_state_saved(void) { dirty = 0; }
