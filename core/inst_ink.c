/* h1 - INK: the lead. One voice a note, each of them sliding to its pitch and shaking once it is
 * there, like a bowed string or a whistle. The picture is a scroll going by under a brush: every
 * note is a stroke, as high as its pitch, as wide as it is loud, so a slide is a curve, vibrato is
 * a wave, and a tune is a line of handwriting that runs off to the left.
 *
 *   K1 SLIDE   how long a note takes to arrive        the stroke sweeps in from the last one
 *   K2 SHAKE   vibrato                                the stroke waves
 *   K3 PRESS   harder: brighter and thicker           a fatter stroke, blots where it lands, runs
 *   K4 DRY     breath and scratch                     the brush runs dry: streaks, gaps
 */
#include "internal.h"
#include "dsp.h"

/* ---- sound ------------------------------------------------------------------------------ */
static struct {
    int algo;
    f32 kslide, shake, press, dry;
} I;
static f32 ink_last = 60.0f;
static volatile f32 ink_pitch[NVOICE];

static void ink_block(const Blk *b)
{
    I.algo = b->algo;
    I.kslide = onepole_k(0.004f + 0.45f * b->k[0] * b->k[0] * (b->algo == 5 ? 2.5f : 1.0f), b->sr / (f32)CTRL);
    I.shake = b->k[1];
    I.press = b->k[2];
    I.dry = b->k[3];
}

static void ink_note(Voice *v, const Blk *b)
{
    (void)b;
    if (!v->u[0]) v->u[0] = 0x1B2C3D4Fu + v->idx * 6151u;
    if (v->env < 0.001f) {
        v->s[0] = ink_last;                      /* a new stroke starts where the last note was */
        v->s[1] = v->s[2] = v->s[6] = v->s[7] = 0.0f;
        v->ph[1] = 0.0f;
    }
    v->s[5] = 0.0f;
    ink_last = v->target;
}

static void ink_render(Voice *v, const Blk *b, f32 *out, int n)
{
    Svf f, f2;
    int algo = I.algo, i;
    f32 dt = (f32)n * b->isr, pitch, vib, d, cut, q = 1.2f, drive = 1.0f + 3.0f * I.press * I.press, breath = I.dry * I.dry * 0.5f, thin = I.dry * 0.75f;
    f32 pa = v->ph[0], hp = v->s[8], x1 = v->s[9], outg = I.algo == 6 ? 2.4f : 0.8f;

    v->s[0] += (v->pitch - v->s[0]) * I.kslide;
    v->ph[1] = fractf(v->ph[1] + (5.2f + 1.5f * I.shake + (algo == 3 ? 0.8f : 0.0f)) * dt);
    vib = (I.shake * I.shake * 0.9f + (algo == 3 ? 0.12f : 0.0f)) * sat((v->age - 0.12f) * 4.0f) * tsin(v->ph[1]);
    pitch = v->s[0] + vib + b->bend;
    if (algo == 7) {                             /* GLITCH: the pitch only moves in jumps */
        v->s[5] -= dt;
        if (v->s[5] <= 0.0f) {
            v->s[5] = 0.055f;
            v->s[4] = (f32)ifloor(pitch + 0.5f + (rng_f(&v->u[0]) < 0.12f ? 12.0f : 0.0f));
        }
        pitch = v->s[4];
    }
    ink_pitch[v->idx] = pitch - b->bend;
    if (algo == 4) pitch += 12.0f;
    d = minf(mtof(pitch) * b->isr, 0.45f);
    cut = 500.0f * exp2fx(I.press * 4.6f + (pitch - 60.0f) * (1.0f / 16.0f) + 1.2f * exp2fx(-v->age * 7.0f));
    if (algo == 1) q = 2.6f;
    if (algo == 6) drive *= 6.0f;
    if (algo == 6) thin *= 0.3f;
    svf_set(&f, cut, q, b->isr);
    svf_set(&f2, 1250.0f, 4.0f, b->isr);         /* ERHU: the snakeskin */

    for (i = 0; i < n; i++) {
        f32 x, lo, bp, nz = noise_s(&v->u[0]);
        if (algo == 2 || algo == 4) {            /* breath is not hiss: its top comes off */
            v->s[3] += 0.28f * (nz - v->s[3]);
            nz = v->s[3] * 2.2f;
        }
        switch (algo) {
        default: x = saw_bl(pa, d); break;                                           /* BRUSH */
        case 1: x = pulse_bl(pa, d, 0.5f); break;                                    /* REED */
        case 2: x = tsin(pa) + 0.2f * tsin(pa * 2.0f) + 0.25f * nz; break;           /* FLUTE */
        case 3: x = saw_bl(pa, d); break;                                            /* ERHU */
        case 4: x = tsin(pa) + 0.06f * nz; break;                                    /* WHISTLE */
        case 5: x = tsin(pa + 0.06f * x1); break;                                    /* THEREMIN */
        case 6: x = saw_bl(pa, d); break;                                            /* FUZZ */
        case 7: x = pa < 0.5f ? 0.8f : -0.8f; break;                                 /* GLITCH */
        }
        x1 = x;
        x = softclip((x + breath * nz * 1.5f) * drive) * (1.0f / (0.6f + 0.4f * drive));
        if (algo == 2 || algo == 4 || algo == 5)
            lo = x;
        else
            SVF_TICK(f, &v->s[1], x, lo, bp);
        if (algo == 3) {
            f32 l2;
            SVF_TICK(f2, &v->s[6], lo, l2, bp);
            lo = lo * 0.5f + bp * 0.9f;
            (void)l2;
        }
        hp += (lo - hp) * 0.02f;                 /* DRY: the body goes out of it */
        out[i] = (lo - hp * thin) * outg;
        pa += d; if (pa >= 1.0f) pa -= 1.0f;
    }
    v->ph[0] = pa;
    v->s[8] = hp;
    v->s[9] = x1;
}

/* ---- picture ---------------------------------------------------------------------------- */
#define HN 124                                   /* columns of the scroll, 2 pixels apart */
#define PEN 196.0f
typedef struct Col { int16_t y[NVOICE]; uint8_t w[NVOICE]; uint8_t id[NVOICE]; } Col;
static Col hist[HN] H1_POOL;
static int head;                                 /* the newest column */
static uint32_t col_n;                           /* columns laid down so far */
static f32 scroll, pen_y = 120.0f, pen_v, beat_x, splay;
static f32 cur_y[NVOICE], cur_w[NVOICE];
static uint8_t cur_id[NVOICE];
static uint32_t seen_i[NVOICE];

static f32 hs(uint32_t i)
{
    i = i * 747796405u + 2891336453u;
    i = ((i >> ((i >> 28) + 4u)) ^ i) * 277803737u;
    return (f32)((i ^ (i >> 22)) >> 8) * (1.0f / 16777216.0f);
}

static f32 pitch_y(f32 p) { return clampf(204.0f - (p - 41.0f) * 3.3f, 14.0f, 222.0f); }

/* one pixel column of one stroke: centre y, width w, in the algorithm's hand. cn = which column
 * of the scroll (it stays with the ink as it moves), id = which stroke, age in columns. */
static void ink_col(int style, int x, f32 y, f32 w, uint32_t cn, uint32_t id, f32 age, f32 dry, f32 press, f32 a)
{
    int k;
    f32 h = w * 0.5f;
    if (w < 0.3f)
        return;
    switch (style) {
    default: {                                   /* BRUSH: five bristles, each running dry by itself */
        if (dry < 0.5f)
            g_vspan(x, y - h * 1.45f, y + h * 1.45f, a * 0.16f * (1.0f - 2.0f * dry));     /* wet ink spreads a little */
        for (k = 0; k < 5; k++) {
            f32 ya = y - h + w * (f32)k * 0.2f, wet = hs(id * 131u + (uint32_t)k * 17u + (cn >> 3)), wet2 = hs(id * 131u + (uint32_t)k * 17u + (cn >> 3) + 1u);
            f32 m = lerpf(wet, wet2, (f32)(cn & 7u) * 0.125f), al = 1.0f;
            if (dry > 0.02f)
                al = sat((m - dry * 0.62f) * 6.0f + (k == 2 ? 0.25f : 0.0f) + (1.0f - dry));
            if (al > 0.02f)
                g_vspan(x, ya, ya + w * 0.2f + 0.3f, a * al);
        }
        if (press > 0.45f && hs(cn * 7u + id) < (press - 0.45f) * 0.14f)             /* PRESS: it runs */
            g_vspan(x, y + h, y + h + minf(age * 0.5f, 6.0f + 40.0f * hs(cn + id * 3u)), a * 0.8f);
        break;
    }
    case 1:                                      /* REED: a split nib, two edges */
        g_vspan(x, y - h, y - h + 1.4f, a);
        g_vspan(x, y + h - 1.4f, y + h, a);
        break;
    case 2:                                      /* FLUTE: breath, a spray of dots */
        for (k = 0; k < 3; k++) {
            f32 r = hs(cn * 13u + (uint32_t)k + id * 5u);
            if (hs(cn * 29u + (uint32_t)k) < 0.55f - 0.3f * dry)
                g_vspan(x, y + (r - 0.5f) * w * 1.3f, y + (r - 0.5f) * w * 1.3f + 1.4f, a * (0.5f + 0.5f * r));
        }
        break;
    case 3:                                      /* ERHU: two hairs */
        g_vspan(x, y - h * 0.6f - 0.8f, y - h * 0.6f + 0.8f, a);
        g_vspan(x, y + h * 0.6f - 0.5f, y + h * 0.6f + 0.5f, a * 0.6f);
        break;
    case 4:                                      /* WHISTLE: beads */
        if ((cn & 3u) < 2u)
            g_vspan(x, y - minf(h, 2.6f), y + minf(h, 2.6f), a);
        break;
    case 5:                                      /* THEREMIN: a tube of light */
        g_vspan(x, y - h * 1.5f, y + h * 1.5f, a * 0.25f);
        g_vspan(x, y - h * 0.45f, y + h * 0.45f, a);
        break;
    case 6: {                                    /* FUZZ: hairy */
        f32 r1 = hs(cn * 3u + id), r2 = hs(cn * 5u + id + 9u);
        g_vspan(x, y - h * (1.0f + 1.6f * r1 * r1), y + h * (1.0f + 1.6f * r2 * r2), a * 0.55f);
        g_vspan(x, y - h * 0.7f, y + h * 0.7f, a);
        break;
    }
    case 7:                                      /* GLITCH: blocks */
        y = (f32)ifloor(y / 6.0f) * 6.0f;
        if ((cn & 3u) != 3u)
            g_vspan(x, y, y + 5.0f, a);
        break;
    }
}

static SMALL void ink_draw(const Vis *vis)
{
    f32 dt = vis->dt, t = vis->t, shake = vis->k[1], press = vis->k[2], dry = vis->k[3], mt = smooth01(vis->algo_t);
    f32 speed = 18.0f + 0.26f * (f32)st.glo[0], dim = 0.2f + 0.8f * absf(2.0f * mt - 1.0f), bw = (3.0f + 29.0f * press * (0.4f + 0.6f * press));
    int i, c, x, style = mt < 0.5f ? vis->algo_prev : vis->algo, any = 0;

    if (vis->fresh) {
        for (c = 0; c < HN; c++)
            for (i = 0; i < NVOICE; i++)
                hist[c].w[i] = 0;
        for (i = 0; i < NVOICE; i++) {
            seen_i[i] = vis->v[i].serial;
            cur_w[i] = 0.0f;
            cur_id[i] = 0;
        }
        scroll = 0.0f;
    }

    /* what the brush is doing now */
    for (i = 0; i < NVOICE; i++) {
        const VVoice *v = &vis->v[i];
        f32 w = 0.0f, y;
        if (v->serial != seen_i[i]) {
            seen_i[i] = v->serial;
            if (cur_w[i] < 0.4f)
                cur_id[i] = (uint8_t)(v->serial * 8u + (uint32_t)i);     /* a new stroke, not a legato one */
        }
        if (v->on && v->env > 0.004f) {
            y = pitch_y(ink_pitch[i]);
            w = bw * sqrtfx(v->env) * (0.75f + 0.25f * sat(v->age * 6.0f));
            w /= 1.0f + absf(y - cur_y[i]) * 0.12f;                      /* a fast stroke is a thin one */
            if (cur_w[i] < 0.05f)
                cur_y[i] = y;
            cur_y[i] += (y - cur_y[i]) * minf(1.0f, dt * 40.0f);
            any = 1;
            spring(&pen_y, &pen_v, cur_y[i], 14.0f, dt);
        }
        cur_w[i] += (w - cur_w[i]) * minf(1.0f, dt * 30.0f);
    }

    /* the scroll moves; every two pixels the brush leaves a column behind */
    scroll += speed * dt;
    beat_x = fractf(beat_x + speed * dt / 48.0f);
    while (scroll >= 2.0f) {
        scroll -= 2.0f;
        head = (head + 1) % HN;
        col_n++;
        for (i = 0; i < NVOICE; i++) {
            hist[head].y[i] = (int16_t)(cur_y[i] * 64.0f);
            hist[head].w[i] = (uint8_t)clampf(cur_w[i] * 8.0f, 0.0f, 255.0f);
            hist[head].id[i] = cur_id[i];
        }
    }

    /* the paper: a rule every beat's worth, going by */
    for (i = 0; i < 6; i++) {
        f32 rx = PEN + 44.0f - (beat_x + (f32)i) * 48.0f;
        int j;
        for (j = 0; j < 12; j++)
            g_disc(rx, 20.0f + (f32)j * 18.0f, 0.9f, 0.2f * dim);
    }

    /* the strokes, newest first */
    for (i = 0; i < NVOICE; i++) {
        f32 y1 = cur_y[i], w1 = cur_w[i], x1 = PEN;
        uint8_t id1 = cur_id[i];
        for (c = 0; c < HN; c++) {
            const Col *h = &hist[(head - c + HN) % HN];
            f32 y0 = (f32)h->y[i] * (1.0f / 64.0f), w0 = (f32)h->w[i] * 0.125f, x0 = PEN - scroll - 2.0f * (f32)c, fade;
            uint32_t cn = col_n - (uint32_t)c;
            if (x0 < -3.0f)
                break;
            fade = sat((x0 + 4.0f) * (1.0f / 60.0f)) * dim;              /* the ink fades as it leaves */
            if (w0 > 0.2f && w1 > 0.2f && h->id[i] == id1) {
                int xa = (int)x0 + 1, xb = (int)x1;
                for (x = xa; x <= xb; x++) {
                    f32 u = x1 > x0 ? ((f32)x - x0) / (x1 - x0) : 0.0f, yy = lerpf(y0, y1, u), ww = lerpf(w0, w1, u);
                    f32 lo = minf(y0, y1), hi = maxf(y0, y1);
                    if (hi - lo > ww)            /* a leap: the stroke stands up */
                        g_vspan(x, lo - ww * 0.3f, hi + ww * 0.3f, fade * 0.9f);
                    else
                        ink_col(style, x, yy, ww, cn * 2u + (uint32_t)(x & 1), h->id[i], (f32)c, dry, press, fade);
                }
            } else if (w0 > 0.6f && w1 <= 0.2f && press > 0.3f) {      /* where a stroke began: blots */
                int k;
                for (k = 0; k < 4; k++) {
                    f32 r = hs(cn * 11u + (uint32_t)k);
                    g_disc(x0 + 3.0f + r * 14.0f * press, y0 + (hs(cn * 23u + (uint32_t)k) - 0.5f) * 30.0f * press, 0.8f + 2.0f * r * press, fade * 0.85f);
                }
            }
            x1 = x0; y1 = y0; w1 = w0; id1 = h->id[i];
        }
        if (cur_w[i] > 0.4f)                     /* the wet tip */
            g_disc(PEN, cur_y[i], cur_w[i] * 0.5f + 1.2f, dim);
    }

    /* the brush itself, waiting */
    if (!any)
        spring(&pen_y, &pen_v, pen_y, 6.0f, dt);
    {
        /* PRESS: the brush is pushed into the paper. Its head splays as wide as the stroke it will
         * leave, the handle leans over, and a wet mark under it shows the width before you play. */
        f32 by = pen_y + (any ? 0.0f : 3.0f * sin_turns(t * 0.35f)) + shake * 2.0f * sin_turns(t * 5.5f);
        f32 hw = 0.5f * bw, lean = 0.13f - 0.07f * press + clampf(pen_v * 0.0005f, -0.035f, 0.035f) + (any ? 0.0f : 0.008f * sin_turns(t * 0.21f));
        f32 hx = cos_turns(lean), hy = -sin_turns(lean), px = -hy, py = hx;      /* along the brush, and across it */
        f32 hl = 23.0f + 6.0f * press, a0 = (any ? 1.0f : 0.78f) * dim, tx = PEN + 0.5f, fx = tx + hx * hl, fy = by + hy * hl;
        f32 bl = 0.46f, bwd = 5.0f + 2.2f * press, bx = tx + hx * hl * bl, bby = by + hy * hl * bl, q[10];
        int k;
        if (!any) {
            g_vspan((int)PEN, by - hw, by + hw, 0.22f * dim);
            g_vspan((int)PEN - 1, by - hw * 0.8f, by + hw * 0.8f, 0.12f * dim);
        }
        splay += ((any ? 1.0f : 0.12f) - splay) * minf(1.0f, dt * 14.0f);       /* lifted, the hair comes back to its point */
        hw *= splay;
        /* the handle: bamboo, a little thicker at the ferrule, with its joints and the loop it hangs by */
        g_beam(fx + hx * 3.0f, fy + hy * 3.0f, fx + hx * 70.0f, fy + hy * 70.0f, 6.0f + 1.0f * press, 4.4f, a0 * 0.9f, a0 * 0.9f, 0.0f);
        g_ring(fx + hx * 73.5f, fy + hy * 73.5f, 3.0f, 1.0f, a0 * 0.8f);
        g_mode(G_PAINT);
        for (k = 0; k < 2; k++) {
            f32 d = 20.0f + 19.0f * (f32)k;
            g_line(fx + hx * d - px * 4.0f, fy + hy * d - py * 4.0f, fx + hx * d + px * 4.0f, fy + hy * d + py * 4.0f, 1.2f, 0.0f);
        }
        g_line(fx + hx * 8.0f + px * 0.9f, fy + hy * 8.0f + py * 0.9f, fx + hx * 16.0f + px * 0.8f, fy + hy * 16.0f + py * 0.8f, 1.0f, 0.25f * a0);   /* (a shadow down its length) */
        g_mode(G_MAX);
        /* the head: a belly of hair that comes to a point, and splays on the paper as wide as the stroke it will leave */
        q[0] = tx; q[1] = by;
        q[2] = bx + px * bwd; q[3] = bby + py * bwd;
        q[4] = fx + px * 3.0f; q[5] = fy + py * 3.0f;
        q[6] = fx - px * 3.0f; q[7] = fy - py * 3.0f;
        q[8] = bx - px * bwd; q[9] = bby - py * bwd;
        g_fill(q, 5, a0 * 0.8f);
        if (hw > 1.5f) {
            f32 tri[6];
            tri[0] = bx; tri[1] = bby; tri[2] = PEN + 1.0f; tri[3] = by - hw * 0.92f; tri[4] = PEN + 1.0f; tri[5] = by + hw * 0.92f;
            g_fill(tri, 3, a0 * 0.8f);
            g_line(bx + px * bwd, bby + py * bwd, PEN + 1.0f, by + hw * 0.92f, 1.2f, a0);
            g_line(bx - px * bwd, bby - py * bwd, PEN + 1.0f, by - hw * 0.92f, 1.2f, a0);
        }
        q[8] = q[0]; q[9] = q[1];
        g_poly(q, 5, 0, 1.2f, a0);               /* one side, round the ferrule end, */
        g_line(q[6], q[7], bx - px * bwd, bby - py * bwd, 1.2f, a0);
        g_line(bx - px * bwd, bby - py * bwd, tx, by, 1.2f, a0);
        g_mode(G_PAINT);                         /* and the hair itself */
        for (k = -1; k <= 1; k++) {
            f32 o = (f32)k * 0.42f;
            g_line(fx + px * o * 3.0f - hx * 2.5f, fy + py * o * 3.0f - hy * 2.5f, tx + hx * hl * 0.16f + px * o * bwd * 0.5f, by + hy * hl * 0.16f + py * o * bwd * 0.5f, 1.0f, 0.3f * a0);
        }
        g_mode(G_MAX);
        g_line(fx - px * 3.3f, fy - py * 3.3f, fx + px * 3.3f, fy + py * 3.3f, 3.0f, a0);                /* the binding */
        g_line(fx - px * 3.5f + hx * 4.0f, fy - py * 3.5f + hy * 4.0f, fx + px * 3.5f + hx * 4.0f, fy + py * 3.5f + hy * 4.0f, 1.6f, a0);
    }
}

const InstDef inst_ink = {
    "INK",
    {255, 232, 196},
    {"SLIDE", "SHAKE", "PRESS", "DRY"},
    {"BRUSH", "REED", "FLUTE", "ERHU", "WHISTLE", "THEREMIN", "FUZZ", "GLITCH"},
    {{35, 30, 50, 10}, 0, {12, 50, 85, 40}, {0, 55, 0, 0}, {22, 4, 30, 55}, {0, 3, 1, 60}, {0, 0, 0}},
    0.28f,
    ink_block, ink_note, ink_render, 0, ink_draw,
};
