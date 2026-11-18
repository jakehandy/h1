/* H1 - the panel and the screen around the instruments: what every knob and button does, the
 * home overlays (the name flash, the knob strip, toasts) and the settings pages, which are one
 * layout for every page and every instrument: four columns, one per knob, left to right.
 *
 *   PRESETS    the instrument               ALGORITHM   its algorithm (8 each)
 *   KNOB 1-4   home: the instrument's four live modulations. A page: its four columns.
 *   SELECT     home: tempo. A page: the next or previous page.
 *   ARP LFO ENV FX SEL GLO SEQ   open that page; again, or HOME, goes back
 *   EDIT       pins the knob strip on the home screen
 *   SAVE       saves now; held, resets this instrument
 *   PLAY REC   the loop recorder; REC held clears it
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
static const char *const O_QUANT[] = {"OFF", "1/16", "1/8"};

const PageDef pages[NPAGE] = {
    {"HOME", {{0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0}}},
    {"ARP", {{"MODE", PK_ENUM, 6, 0, 0, O_ARPMODE}, {"RATE", PK_ENUM, 5, 0, 0, O_RATE},
             {"RANGE", PK_INT, 4, 1, 0, 0}, {"GATE", PK_PCT, 100, 5, 0, 0}}},
    {"LFO", {{"SHAPE", PK_ENUM, 5, 0, 0, O_SHAPE}, {"RATE", PK_PCT, 100, 0, 0, 0},
             {"DEPTH", PK_PCT, 100, 0, 0, 0}, {"TO", PK_ENUM, 6, 0, 0, O_TARGET}}},
    {"ENV", {{"ATTACK", PK_PCT, 100, 0, 0, 0}, {"DECAY", PK_PCT, 100, 0, 0, 0},
             {"SUSTAIN", PK_PCT, 100, 0, 0, 0}, {"RELEASE", PK_PCT, 100, 0, 0, 0}}},
    {"FX", {{"ECHO", PK_PCT, 100, 0, 0, 0}, {"TIME", PK_ENUM, 7, 0, 0, O_TIME},
            {"SPACE", PK_PCT, 100, 0, 0, 0}, {"TONE", PK_PCT, 100, 0, -50, 0}}},
    {"KEYS", {{"ROOT", PK_ENUM, 11, 0, 0, O_ROOT}, {"SCALE", PK_ENUM, 7, 0, 0, O_SCALE},
              {"GLIDE", PK_PCT, 100, 0, 0, 0}, {"HOLD", PK_ENUM, 1, 0, 0, O_ONOFF}}},
    {"GLOBAL", {{"TEMPO", PK_INT, 240, 40, 0, 0}, {"SWING", PK_PCT, 100, 0, 0, 0},
                {"TUNE", PK_PCT, 100, 0, -50, 0}, {"BRIGHT", PK_PCT, 100, 20, 0, 0}}},
    {"LOOP", {{"BARS", PK_ENUM, 3, 0, 0, O_BARS}, {"SNAP", PK_ENUM, 2, 0, 0, O_QUANT},
              {"CLICK", PK_ENUM, 1, 0, 0, O_ONOFF}, {"DUB", PK_ENUM, 1, 0, 0, O_ONOFF}}},
};

uint8_t *page_value(int page, int i)
{
    InstParams *ip = &st.ip[st.inst];
    switch (page) {
    case PG_ARP: return &ip->arp[i];
    case PG_LFO: return &ip->lfo[i];
    case PG_ENV: return &ip->env[i];
    case PG_FX: return &ip->fx[i];
    case PG_KEYS: return &st.keys[i];
    case PG_GLO: return &st.glo[i];
    case PG_LOOP: return &st.loop[i];
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
    const ParamDef *p = &pages[pg].p[i];
    int v = *page_value(pg, i);
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
    if (ad >= 3 || gap < 28u) m = 4;
    else if (ad == 2 || gap < 70u) m = 2;
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
            const ParamDef *p = &pages[pg].p[i];
            col_v[i] = p->max > p->min ? (f32)(*page_value(pg, i) - p->min) / (f32)(p->max - p->min) : 0.0f;
            col_vv[i] = 0.0f;
        }
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
    case H1_BTN_SEQ: open_page(PG_LOOP); break;
    case H1_BTN_EDIT:
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
        toast("LOOP CLEARED", 0, 0, 0, -1);
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

void h1_encoder(int enc, int d)
{
    InstParams *ip = &st.ip[st.inst];
    if (!d || enc < 0 || enc >= H1_NENC)
        return;
    switch (enc) {
    case H1_ENC_PRESET:
        set_instrument(((st.inst + d) % NINST + NINST) % NINST);
        break;
    case H1_ENC_ALGO:
        vis.algo_prev = ip->algo;
        ip->algo = (uint8_t)(((ip->algo + d) % NALGO + NALGO) % NALGO);
        vis.algo = ip->algo;
        vis.algo_t = 0.0f;
        toast(insts[st.inst]->algo[ip->algo], 0, 0, 0, ip->algo);
        mark_dirty();
        break;
    case H1_ENC_SELECT:
        if (page == PG_HOME) {
            st.glo[0] = (uint8_t)iclamp(st.glo[0] + accel(enc, d), 40, 240);
            toast(0, st.glo[0], 1, " BPM", -1);
            mark_dirty();
        } else {
            int pg = page - 1 + (d > 0 ? 1 : -1);
            pg = (pg % (NPAGE - 1) + (NPAGE - 1)) % (NPAGE - 1) + 1;
            page = PG_HOME;                      /* (so open_page does not read it as a toggle) */
            open_page(pg);
        }
        break;
    default: {
        int i = enc - H1_ENC_K1;
        if (page == PG_HOME) {
            ip->knob[i] = (uint8_t)iclamp(ip->knob[i] + accel(enc, d), 0, 100);
            knob_touch[i] = 0.0f;
        } else {
            const ParamDef *p = &pages[page].p[i];
            uint8_t *v = page_value(page, i);
            int step = p->kind == PK_ENUM || (p->kind == PK_INT && p->max - p->min < 16) ? d : accel(enc, d);
            *v = (uint8_t)iclamp(*v + step, p->min, p->max);
            active_col = i;
            if (page == PG_KEYS && i == 3 && !*v)
                eng_post(EV_UNLATCH, 0);
        }
        mark_dirty();
        break;
    }
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

static void draw_loop_mark(void)
{
    int s = eng_loop_state();
    f32 p;
    if (s == LOOP_EMPTY || s == LOOP_IDLE)
        return;
    if (s == LOOP_ARMED) {
        if (fractf(tsec * 2.0f) < 0.5f)
            g_disc(228.0f, 228.0f, 4.0f, 1.0f);
        return;
    }
    p = eng_loop_pos();
    g_line(0.0f, 238.5f, 240.0f * p, 238.5f, 2.0f, s == LOOP_PLAY ? 0.6f : 1.0f);
    if (s != LOOP_PLAY)
        g_disc(228.0f, 228.0f, 4.0f, 1.0f);
}

static void draw_page(void)
{
    const PageDef *pd = &pages[page];
    char buf[20];
    int i, j;
    f32 intro = ease_out(page_t / 0.22f);
    g_text(12.0f, 14.0f, pd->title, 22.0f, 3.0f, 1.0f, 0);
    for (i = 1; i < NPAGE; i++) {                /* which page of the seven */
        f32 x = 228.0f - (f32)(NPAGE - 1 - i) * 9.0f;
        if (i == page) g_disc(x, 25.0f, 3.0f, 1.0f);
        else g_disc(x, 25.0f, 1.3f, 0.5f);
    }
    for (i = 0; i < 4; i++) {
        const ParamDef *p = &pd->p[i];
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
        draw_loop_mark();
        draw_toast();
        draw_name();
    } else {
        draw_page();
        draw_loop_mark();
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
    static const int8_t PAGE_BTN[NPAGE] = {H1_BTN_HOME, H1_BTN_ARP, H1_BTN_LFO, H1_BTN_ENV, H1_BTN_FX, H1_BTN_SEL, H1_BTN_GLO, H1_BTN_SEQ};
    uint32_t m = 0;
    int s = eng_loop_state(), blink = fractf(tsec * 2.5f) < 0.5f;
    if (page != PG_HOME)
        m |= 1u << PAGE_BTN[page];
    if (st.ip[st.inst].arp[0] && (page == PG_ARP || vis.beat < 0.25f))
        m |= 1u << H1_BTN_ARP;
    if (st.hud) m |= 1u << H1_BTN_EDIT;
    if (st.keys[3]) m |= 1u << H1_BTN_SEL;
    if (s == LOOP_PLAY || s == LOOP_DUB || s == LOOP_REC) m |= 1u << H1_BTN_PLAY;
    if (s == LOOP_REC || s == LOOP_DUB || (s == LOOP_ARMED && blink)) m |= 1u << H1_BTN_REC;
    if (st.octave < 0) m |= 1u << H1_BTN_OCTDN;
    if (st.octave > 0) m |= 1u << H1_BTN_OCTUP;
    return m;
}

/* ---- saved state ------------------------------------------------------------------------ */
int h1_state_dirty(void) { return dirty && now - dirty_ms > 1500u; }
void h1_state_saved(void) { dirty = 0; }
