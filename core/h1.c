/* H1 - the public calls that are not the UI's or the engine's own: start-up and saved state. */
#include "internal.h"

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
    for (i = 0; i < NINST; i++) {
        st.ip[i] = insts[i]->def;
        st.ink[i][0] = insts[i]->ink[0];
        st.ink[i][1] = insts[i]->ink[1];
        st.ink[i][2] = insts[i]->ink[2];
    }
}

void h1_init(uint32_t sample_rate)
{
    defaults();
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
 * look like this version's state is refused, and every value is clamped to its range. */
int h1_state_load(const void *blob, uint32_t size)
{
    const H1State *s = (const H1State *)blob;
    int i, j, pg;
    if (!blob || size != sizeof st || s->magic != H1_MAGIC || s->version != H1_VERSION)
        return -1;
    st = *s;
    if (st.inst >= NINST) st.inst = 0;
    st.octave = (int8_t)iclamp(st.octave, -2, 2);
    st.hud &= 1;
    for (i = 0; i < NINST; i++) {
        uint8_t keep = st.inst;
        st.inst = (uint8_t)i;
        for (j = 0; j < 4; j++)
            if (st.ip[i].knob[j] > 100) st.ip[i].knob[j] = 100;
        if (st.ip[i].algo >= NALGO) st.ip[i].algo = 0;
        for (pg = 1; pg < NPAGE; pg++)
            for (j = 0; j < 4; j++) {
                uint8_t *v = page_value(pg, j);
                *v = (uint8_t)iclamp(*v, page_param(pg, j)->min, page_param(pg, j)->max);
            }
        st.inst = keep;
    }
    st.seq[0] &= 1;
    if (st.seq[3] > 2) st.seq[3] = 1;
    if (st.seq[4] > 2) st.seq[4] = 1;
    for (i = 0; i < NSTEP; i++)
        for (j = 0; j < 4; j++)
            if (st.steps[i][j] > 128) st.steps[i][j] = 0;
    eng_init(0);
    ui_init();
    return 0;
}

int h1_instrument(void) { return st.inst; }
const char *h1_instrument_name(int i) { return i >= 0 && i < NINST ? insts[i]->name : ""; }
int h1_algorithm(void) { return st.ip[st.inst].algo; }
int h1_page(void) { return ui_page(); }
int h1_instrument_count(void) { return NINST; }
const char *h1_knob_name(int inst, int k) { return inst >= 0 && inst < NINST && k >= 0 && k < 4 ? insts[inst]->knob[k] : ""; }
const char *h1_algo_name(int inst, int a) { return inst >= 0 && inst < NINST && a >= 0 && a < NALGO ? insts[inst]->algo[a] : ""; }
uint32_t h1_voices_active(void) { return eng_voices(); }

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
