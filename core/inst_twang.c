/* H1 - TWANG: plucked strings. Each voice is a real delay-line string, loaded at the note with the
 * shape a plectrum leaves in it, and the screen is those eight strings, moving the way a plucked
 * string moves: a kink running up and down between the ends.
 *
 *   K1 DAMP    how soon the string dies, and how dull     a felt mute creeps along every string
 *   K2 PICK    where it is plucked: middle .. the bridge  the plectrum walks along, the kink follows
 *   K3 TONE    soft thumb .. hard nail                    fat strings .. hair-thin ones
 *   K4 BUZZ    the string rattles on its bridge           the bridge end turns to a zigzag
 */
#include "internal.h"
#include "dsp.h"

#define KS_MASK (KS_LEN - 1)

/* ---- sound ------------------------------------------------------------------------------ */
typedef struct TAlgo {
    f32 ring;                    /* decay time, relative */
    f32 bright;                  /* loop filter, relative */
    f32 transpose;
    f32 body[2];                 /* two body resonances, Hz */
    f32 bodymix;
    uint8_t second;              /* 0 none, 1 octave string, 2 sympathetic drone */
} TAlgo;

static const TAlgo TA[NALGO] = {
    /* HARP   */ {1.3f, 1.0f, 0.0f, {180.0f, 420.0f}, 0.25f, 0},
    /* NYLON  */ {0.6f, 0.72f, 0.0f, {110.0f, 230.0f}, 0.55f, 0},
    /* TWELVE */ {1.0f, 1.0f, 0.0f, {140.0f, 300.0f}, 0.3f, 1},
    /* SITAR  */ {1.1f, 1.0f, 0.0f, {200.0f, 620.0f}, 0.4f, 2},
    /* BANJO  */ {0.28f, 1.0f, 0.0f, {380.0f, 900.0f}, 0.7f, 0},
    /* KOTO   */ {0.8f, 0.9f, 0.0f, {240.0f, 560.0f}, 0.35f, 0},
    /* BASS   */ {1.2f, 0.6f, -12.0f, {70.0f, 150.0f}, 0.45f, 0},
    /* BOW    */ {1.0f, 0.8f, 0.0f, {280.0f, 480.0f}, 0.5f, 0},
};

static struct {
    int algo;
    f32 t60, g, pick, tone, buzz, thr, drone;
} T;
static Svf body_f[2];
static f32 body_s[2][2], dc_y;

static void twang_block(const Blk *b)
{
    const TAlgo *a = &TA[b->algo];
    f32 d = b->k[0];
    T.algo = b->algo;
    T.t60 = 7.0f * exp2fx(-d * 5.5f) * a->ring;
    T.g = clampf(lerpf(0.97f, 0.3f, d * sqrtfx(d)) * a->bright, 0.08f, 0.98f);
    T.pick = b->algo == 4 ? lerpf(0.25f, 0.03f, b->k[1]) : lerpf(0.5f, 0.04f, b->k[1]);
    T.tone = b->k[2];
    T.buzz = b->algo == 3 ? 0.5f + 0.5f * b->k[3] : b->k[3];
    T.thr = lerpf(1.3f, 0.04f, sqrtfx(T.buzz));
    T.drone = mtof(48.0f + (f32)st.keys[0] + b->bend);
    svf_set(&body_f[0], a->body[0], 3.0f, b->isr);
    svf_set(&body_f[1], a->body[1], 4.0f, b->isr);
}

/* leave in the line the shape a pluck at `pick` gives one period of the string */
static void load(f32 *line, uint32_t w, f32 L, f32 amp, uint32_t *rng)
{
    int n = (int)L + 1, i;
    f32 p = T.pick, t1 = sat(T.tone * 2.0f), t2 = sat(T.tone * 2.0f - 1.0f) * 0.6f, iL = 1.0f / L;
    for (i = 0; i < n; i++) {
        f32 u = (f32)i * iL, tri, pul, x;
        if (u > 1.0f) u = 1.0f;
        tri = (u < p ? u / p : (1.0f - u) / (1.0f - p)) * 2.0f - 1.0f;
        pul = u < p ? 1.0f - p : -p;
        x = lerpf(tri, pul * 1.3f, t1) + rng_s(rng) * t2;
        line[(w - (uint32_t)n + (uint32_t)i) & KS_MASK] = x * amp;
    }
}

static f32 string_len(f32 pitch, const Blk *b)
{
    f32 L = b->sr / mtof(pitch) - (1.0f - T.g) / T.g;
    while (L > (f32)(KS_LEN - 4))
        L *= 0.5f;
    return maxf(L, 2.0f);
}

static void twang_note(Voice *v, const Blk *b)
{
    const TAlgo *a = &TA[T.algo];
    f32 L = string_len(v->target + b->bend + a->transpose, b);
    if (!v->u[2]) v->u[2] = 0x9E3779B9u + v->idx * 7919u;
    v->s[0] = 0.0f;
    v->s[1] = L;
    load(ks_line[v->idx][0], v->u[0], L, T.algo == 7 ? 0.3f : 0.85f, &v->u[2]);
    if (a->second == 1) {
        f32 keep = T.pick;
        T.pick = keep * 0.7f;
        v->s[2] = 0.0f;
        load(ks_line[v->idx][1], v->u[1], L * 0.5f * 0.998f, 0.5f, &v->u[2]);
        T.pick = keep;
    }
}

static void twang_render(Voice *v, const Blk *b, f32 *out, int n)
{
    const TAlgo *a = &TA[T.algo];
    f32 *line = ks_line[v->idx][0], *line2 = ks_line[v->idx][1];
    f32 pitch = v->pitch + b->bend + a->transpose, L, L2 = 4.0f, rho, rho2 = 0.0f, lp = v->s[0], lp2 = v->s[2];
    f32 g = T.g, thr = T.thr, bow = 0.0f, drive = 1.0f + T.buzz * 2.5f, idrive = 1.0f / (1.0f + T.buzz * 0.8f);
    uint32_t w = v->u[0] & KS_MASK, w2 = v->u[1] & KS_MASK;
    int i, second = a->second;

    if (T.algo == 5)                             /* KOTO: the note is pressed sharp and settles */
        pitch += 0.7f * exp2fx(-v->age * 14.0f) + 0.12f * tsin(v->age * 5.5f) * sat(v->age * 2.0f - 0.6f);
    L = string_len(pitch, b);
    rho = exp2fx(-9.966f * L * b->isr / T.t60);
    if (T.algo == 7 && v->gate) {                /* BOW: the loop feeds itself while the key is down */
        rho = 1.06f;
        bow = 0.02f + 0.1f * T.tone;
    }
    if (second == 1) {
        L2 = maxf(L * 0.5f * 0.998f, 2.0f);
        rho2 = exp2fx(-9.966f * L2 * b->isr / (T.t60 * 0.8f));
    } else if (second == 2) {
        L2 = clampf(b->sr / T.drone - 0.4f, 2.0f, (f32)(KS_LEN - 4));
        rho2 = 0.9985f;
    }

    for (i = 0; i < n; i++) {
        f32 pos = (f32)(w + KS_LEN) - L, fr, y, o;
        int ip = (int)pos;
        fr = pos - (f32)ip;
        y = line[ip & KS_MASK];
        y += (line[(ip + 1) & KS_MASK] - y) * fr;
        lp += g * (y - lp);
        o = lp * rho;
        if (o > thr) o = thr + (o - thr) * 0.2f;                 /* the bridge: one-sided, so it rattles */
        if (bow > 0.0f) o = softclip(o) + noise_s(&v->u[2]) * bow;
        line[w] = o;
        w = (w + 1u) & KS_MASK;
        if (second) {
            f32 y2;
            pos = (f32)(w2 + KS_LEN) - L2;
            ip = (int)pos;
            fr = pos - (f32)ip;
            y2 = line2[ip & KS_MASK];
            y2 += (line2[(ip + 1) & KS_MASK] - y2) * fr;
            lp2 += 0.8f * (y2 - lp2);
            line2[w2] = lp2 * rho2 + (second == 2 ? y * 0.035f : 0.0f);
            w2 = (w2 + 1u) & KS_MASK;
            y += y2 * (second == 2 ? 1.1f : 0.8f);
        }
        out[i] = softclip(y * drive) * idrive;
    }
    v->u[0] = w;
    v->u[1] = w2;
    v->s[0] = lp;
    v->s[2] = lp2;
}

static void twang_post(const Blk *b, f32 *l, f32 *r, int n)
{
    f32 mix = TA[T.algo].bodymix * 1.6f;
    int i;
    (void)b;
    for (i = 0; i < n; i++) {
        f32 m = (l[i] + r[i]) * 0.5f, lo, b0, b1, hp;
        SVF_TICK(body_f[0], body_s[0], m, lo, b0);
        SVF_TICK(body_f[1], body_s[1], m, lo, b1);
        (void)lo;
        m = (b0 * body_f[0].k + b1 * body_f[1].k * 0.7f) * mix;
        /* the one-sided bridge leaves an offset behind: take it out */
        hp = (l[i] + r[i]) * 0.5f;
        dc_y += 0.0025f * (hp - dc_y);
        l[i] += m - dc_y;
        r[i] += m - dc_y;
    }
}

/* ---- picture ---------------------------------------------------------------------------- */
#define NSTR 8
#define NSEG 22
static struct { f32 amp, ph, hz, e[4], ev[4]; uint32_t serial; } S[NSTR];
static f32 pk_x, pk_y, pk_vx, pk_vy, pk_side = 1.0f, pk_off, pk_offv, pk_ang[2], hit;
static int pk_str;
static uint32_t seen;

static void layout(int algo, int i, f32 *e)
{
    f32 fi = (f32)i;
    switch (algo) {
    default: {                                   /* HARP: long strings to short, hung from a curved neck */
        f32 u = fi / 7.0f;
        e[0] = 22.0f + 26.0f * fi; e[1] = 254.0f;
        e[2] = 34.0f + 27.5f * fi; e[3] = 52.0f + 74.0f * u - 20.0f * sin_turns(u * 0.5f);
        break;
    }
    case 1:                                      /* NYLON: level */
        e[0] = -14.0f; e[1] = 29.0f + 26.0f * fi; e[2] = 254.0f; e[3] = e[1]; break;
    case 2:                                      /* TWELVE: courses of two */
        e[0] = -14.0f; e[1] = 36.0f + 56.0f * (f32)(i / 2) + 10.0f * (f32)(i & 1); e[2] = 254.0f; e[3] = e[1]; break;
    case 3:                                      /* SITAR: a fan from somewhere off to the left */
        e[0] = -90.0f; e[1] = 120.0f + (fi - 3.5f) * 4.0f; e[2] = 254.0f; e[3] = 120.0f + (fi - 3.5f) * 34.0f; break;
    case 4: {                                    /* BANJO: across a drum */
        f32 an = fi / 16.0f + 0.03f;
        e[0] = 120.0f - 170.0f * cos_turns(an); e[1] = 120.0f - 170.0f * sin_turns(an);
        e[2] = 120.0f + 170.0f * cos_turns(an); e[3] = 120.0f + 170.0f * sin_turns(an); break;
    }
    case 5:                                      /* KOTO: a slope */
        e[0] = -14.0f; e[1] = 96.0f + 25.0f * fi; e[2] = 254.0f; e[3] = e[1] - 104.0f; break;
    case 6:                                      /* BASS: four, each shared by two voices */
        e[0] = -14.0f; e[1] = 42.0f + 52.0f * (f32)(i & 3) + 6.0f * (f32)(i >> 2); e[2] = 254.0f; e[3] = e[1]; break;
    case 7:                                      /* BOW: a cat's cradle */
        e[0] = -14.0f; e[1] = 20.0f + 28.5f * fi; e[2] = 254.0f; e[3] = 220.0f - 28.5f * fi; break;
    }
}

static void twang_draw(const Vis *vis)
{
    f32 dt = vis->dt, damp = vis->k[0], pick = lerpf(0.5f, 0.13f, vis->k[1]), tone = vis->k[2], buzz = vis->k[3];
    f32 mute = damp * 0.34f, wline = lerpf(4.2f, 1.5f, tone), c[5], norm = 0.0f, mt = smooth01(vis->algo_t);
    f32 want[NSTR] = {0}, t60 = 7.0f * exp2fx(-damp * 5.5f);
    int i, j, n;

    for (n = 0; n < 5; n++) {                    /* the pluck's harmonics: sin(n pi p) / n^2 */
        c[n] = sin_turns((f32)(n + 1) * pick * 0.5f) / (f32)((n + 1) * (n + 1));
        norm += absf(c[n]);
    }
    for (n = 0; n < 5; n++)
        c[n] /= norm;

    if (vis->fresh) {
        seen = vis->notes;
        for (i = 0; i < NSTR; i++) {
            layout(vis->algo, i, S[i].e);
            S[i].amp = 0.0f;
            S[i].ev[0] = S[i].ev[1] = S[i].ev[2] = S[i].ev[3] = 0.0f;
        }
        pk_str = 3;
        pk_x = 120.0f;
        pk_y = -30.0f;
    }

    for (i = 0; i < NVOICE; i++)
        if (vis->v[i].on) {
            int s = ((int)(vis->v[i].pitch + 0.5f) % NSTR + NSTR) % NSTR;
            f32 a = vis->v[i].env * exp2fx(-4.0f * vis->v[i].age / t60);
            if (vis->algo == 7 && vis->v[i].gate) a = vis->v[i].env * 0.8f;
            if (a > want[s]) want[s] = a;
            if (vis->v[i].serial != S[s].serial && vis->v[i].age < 0.2f) {
                S[s].serial = vis->v[i].serial;
                S[s].ph = 0.0f;
                S[s].hz = 2.2f + (vis->v[i].pitch - 53.0f) * 0.07f;
            }
        }
    if (vis->notes != seen) {
        seen = vis->notes;
        pk_str = ((int)(vis->last_pitch + 0.5f) % NSTR + NSTR) % NSTR;
        pk_side = -pk_side;
        hit = 1.0f;
    }
    hit = maxf(0.0f, hit - dt * 5.0f);

    for (i = 0; i < NSTR; i++) {
        f32 e[4], x0, y0, dx, dy, len, nx, ny, xy[2 * (NSEG + 1)], amp, live, A, zz = buzz * 4.5f;
        layout(vis->algo, i, e);
        if (mt < 1.0f) {
            f32 p[4];
            layout(vis->algo_prev, i, p);
            for (j = 0; j < 4; j++)
                e[j] = lerpf(p[j], e[j], mt);
        }
        for (j = 0; j < 4; j++)
            S[i].e[j] = e[j];
        S[i].amp += (want[i] - S[i].amp) * minf(1.0f, dt * (want[i] > S[i].amp ? 60.0f : 14.0f));
        S[i].ph = fractf(S[i].ph + dt * S[i].hz);
        amp = S[i].amp;
        x0 = e[0]; y0 = e[1];
        dx = e[2] - e[0]; dy = e[3] - e[1];
        len = sqrtfx(dx * dx + dy * dy);
        nx = -dy / len; ny = dx / len;
        live = 1.0f - mute;
        A = (vis->algo == 6 ? 17.0f : 12.5f) * amp;

        if (amp > 0.01f) {                       /* the blur of its swing: where the kink can reach */
            f32 px = x0 + dx * pick * live, py = y0 + dy * pick * live, ex = x0 + dx * live, ey = y0 + dy * live;
            f32 ga = 0.3f * sat(amp * 3.0f);
            g_line(x0, y0, px + nx * A, py + ny * A, 1.2f, ga);
            g_line(px + nx * A, py + ny * A, ex, ey, 1.2f, ga);
            g_line(x0, y0, px - nx * A, py - ny * A, 1.2f, ga);
            g_line(px - nx * A, py - ny * A, ex, ey, 1.2f, ga);
        }
        for (j = 0; j <= NSEG; j++) {
            f32 u = (f32)j / (f32)NSEG, d = 0.0f;
            if (amp > 0.01f)
                for (n = 0; n < 5; n++)
                    d += c[n] * sin_turns((f32)(n + 1) * u * 0.5f) * cos_turns((f32)(n + 1) * S[i].ph);
            d *= A;
            if (u < 0.2f && zz > 0.05f)          /* BUZZ: the bridge end rattles */
                d += ((j & 1) ? zz : -zz) * (1.0f - u * 5.0f) * (0.6f + 2.0f * amp + 0.4f * sin_turns(vis->t * 9.0f + (f32)i * 0.3f));
            xy[2 * j] = x0 + dx * u * live + nx * d;
            xy[2 * j + 1] = y0 + dy * u * live + ny * d;
        }
        g_poly(xy, NSEG + 1, 0, (vis->algo == 6 ? wline * 1.5f : wline), 0.5f + 0.5f * sat(amp * 4.0f));
        if (mute > 0.01f)                        /* DAMP: the felt */
            g_line(x0 + dx * live, y0 + dy * live, e[2], e[3], 6.0f, 0.75f);
    }

    if (vis->algo == 0 || (mt < 1.0f && vis->algo_prev == 0)) {      /* the harp's neck */
        f32 a = vis->algo == 0 ? mt : 1.0f - mt, xy[2 * 12];
        for (i = 0; i < 12; i++) {
            f32 u = ((f32)i - 1.5f) / 7.0f;
            xy[2 * i] = 34.0f + 27.5f * 7.0f * u;
            xy[2 * i + 1] = 52.0f + 74.0f * u - 20.0f * sin_turns(clampf(u, -0.2f, 1.2f) * 0.5f) - 4.0f;
        }
        g_poly(xy, 12, 0, 5.0f, 0.9f * a);
    }
    if (vis->algo == 4 || (mt < 1.0f && vis->algo_prev == 4)) {      /* the banjo's hoop */
        f32 a = vis->algo == 4 ? mt : 1.0f - mt;
        g_ring(120.0f, 120.0f, 104.0f, 2.0f, 0.55f * a);
    }
    if (vis->algo == 5 || (mt < 1.0f && vis->algo_prev == 5)) {      /* the koto's bridges, one under each string */
        f32 a = vis->algo == 5 ? mt : 1.0f - mt;
        for (i = 0; i < NSTR; i++) {
            f32 u = 0.86f - 0.07f * (f32)i - 0.3f * mute, bx = S[i].e[0] + (S[i].e[2] - S[i].e[0]) * u, by = S[i].e[1] + (S[i].e[3] - S[i].e[1]) * u;
            g_line(bx, by + 2.0f, bx - 5.0f, by + 13.0f, 2.0f, 0.8f * a);
            g_line(bx, by + 2.0f, bx + 5.0f, by + 13.0f, 2.0f, 0.8f * a);
        }
    }

    /* the plectrum: he lives beside the string he last plucked, at PICK, and steps through it to pluck */
    {
        const f32 *e = S[pk_str].e;
        f32 dx = e[2] - e[0], dy = e[3] - e[1], len = sqrtfx(dx * dx + dy * dy), nx = -dy / len, ny = dx / len;
        f32 u = pick * (1.0f - mute), tx, ty, hx, hy, bx, by, px, py, sz = 17.0f, sq;
        spring_z(&pk_off, &pk_offv, pk_side * 17.0f, 26.0f, 0.5f, dt);
        tx = e[0] + dx * u + nx * pk_off;
        ty = e[1] + dy * u + ny * pk_off;
        spring(&pk_x, &pk_vx, tx, 22.0f, dt);
        spring(&pk_y, &pk_vy, ty, 22.0f, dt);
        /* he points at the string */
        hx = -nx * (pk_off > 0.0f ? 1.0f : -1.0f);
        hy = -ny * (pk_off > 0.0f ? 1.0f : -1.0f);
        pk_ang[0] += (hx - pk_ang[0]) * minf(1.0f, dt * 18.0f);
        pk_ang[1] += (hy - pk_ang[1]) * minf(1.0f, dt * 18.0f);
        sq = sqrtfx(pk_ang[0] * pk_ang[0] + pk_ang[1] * pk_ang[1]);
        if (sq < 0.05f) { hx = 0.0f; hy = 1.0f; } else { hx = pk_ang[0] / sq; hy = pk_ang[1] / sq; }
        px = -hy; py = hx;
        sz *= 1.0f + 0.25f * hit;
        bx = pk_x - hx * sz * 0.5f;
        by = pk_y - hy * sz * 0.5f;
        g_mode(G_PAINT);
        g_disc(pk_x - hx * 1.0f, pk_y - hy * 1.0f, sz * 0.9f, 0.0f);
        g_mode(G_MAX);
        for (j = -3; j <= 3; j++)                /* a fat rounded triangle, as a fan of strokes from his tip */
            g_line(pk_x + hx * sz * 0.6f, pk_y + hy * sz * 0.6f, bx + px * sz * 0.2f * (f32)j, by + py * sz * 0.2f * (f32)j, 5.0f, 1.0f);
        g_line(bx + px * sz * 0.6f, by + py * sz * 0.6f, bx - px * sz * 0.6f, by - py * sz * 0.6f, 5.0f, 1.0f);
        g_mode(G_PAINT);
        g_disc(bx + px * sz * 0.26f + hx * sz * 0.22f, by + py * sz * 0.26f + hy * sz * 0.22f, 2.4f, 0.0f);
        g_disc(bx - px * sz * 0.26f + hx * sz * 0.22f, by - py * sz * 0.26f + hy * sz * 0.22f, 2.4f, 0.0f);
        g_mode(G_MAX);
    }
}

const InstDef inst_twang = {
    "TWANG",
    {255, 196, 60},
    {"DAMP", "PICK", "TONE", "BUZZ"},
    {"HARP", "NYLON", "TWELVE", "SITAR", "BANJO", "KOTO", "BASS", "BOW"},
    {{25, 35, 45, 0}, 0, {0, 50, 100, 38}, {0, 55, 0, 0}, {14, 3, 22, 50}, {0, 3, 1, 60}, {0, 0, 0}},
    0.3f,
    twang_block, twang_note, twang_render, twang_post, twang_draw,
};
