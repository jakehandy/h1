/* h1 - KOI: struck metal and wood over still water. A handpan, a tongue drum, a kalimba, a marimba,
 * drops, a gamelan bar, a singing bowl, rain. Every one is a handful of partials that ring and die.
 * The picture is a pond from above: stones, lily pads, a lotus, and koi under them. Every note you
 * strike lands in the water at its own place, and the fish come to it as if it were food.
 *
 *   K1 MALLET  soft felt to hard wood                    the lotus opens, the splash throws more drops
 *   K2 RIPPLE  two bars a hair apart: beating            the water moves, rings spread from every pad
 *   K3 LINGER  how long it rings                         the rings last, the fish trail a longer wake
 *   K4 SINK    lowered into the water: it bends, dulls   the fish go deep, bubbles come up
 */
#include "internal.h"
#include "dsp.h"

/* ---- sound ------------------------------------------------------------------------------ */
typedef struct KAlgo {
    f32 r[4];                    /* where the partials sit */
    f32 a[4];                    /* how loud */
    f32 d[4];                    /* seconds each takes to die, at middle C with LINGER half way */
    f32 strike;                  /* the knock of the mallet itself */
    f32 beat;                    /* Hz of beating it has even with RIPPLE at nothing */
} KAlgo;
static const KAlgo KA[NALGO] = {
    /* PAN     */ {{1.0f, 2.0f, 3.0f, 5.04f}, {1.0f, 0.5f, 0.32f, 0.07f}, {1.5f, 1.0f, 0.7f, 0.12f}, 0.5f, 0.0f},
    /* TONGUE  */ {{1.0f, 2.0f, 3.01f, 6.3f}, {1.0f, 0.22f, 0.1f, 0.05f}, {3.2f, 1.8f, 0.9f, 0.25f}, 0.25f, 0.4f},
    /* KALIMBA */ {{1.0f, 2.0f, 5.8f, 12.3f}, {1.0f, 0.12f, 0.3f, 0.1f}, {1.1f, 0.5f, 0.09f, 0.03f}, 0.6f, 0.0f},
    /* MARIMBA */ {{1.0f, 3.98f, 9.9f, 2.0f}, {1.0f, 0.32f, 0.1f, 0.04f}, {0.55f, 0.13f, 0.045f, 0.3f}, 0.7f, 0.0f},
    /* DROPS   */ {{1.0f, 2.0f, 3.2f, 4.7f}, {1.0f, 0.15f, 0.06f, 0.0f}, {0.3f, 0.12f, 0.06f, 0.05f}, 0.08f, 0.0f},
    /* GAMELAN */ {{1.0f, 2.76f, 5.4f, 8.93f}, {1.0f, 0.5f, 0.28f, 0.14f}, {2.4f, 1.5f, 0.7f, 0.3f}, 0.6f, 2.2f},
    /* BOWL    */ {{1.0f, 2.71f, 5.15f, 8.4f}, {1.0f, 0.55f, 0.26f, 0.1f}, {7.0f, 5.0f, 2.6f, 1.2f}, 0.15f, 0.7f},
    /* RAIN    */ {{1.0f, 3.0f, 6.2f, 10.1f}, {1.0f, 0.3f, 0.14f, 0.05f}, {0.35f, 0.16f, 0.07f, 0.03f}, 0.4f, 0.0f},
};

static struct {
    int algo;
    f32 mallet, ripple, linger, sink, lmul;
} K;
static f32 sink_l[2], sink_r[2];

static void koi_block(const Blk *b)
{
    K.algo = b->algo;
    K.mallet = b->k[0];
    K.ripple = b->k[1];
    K.linger = b->k[2];
    K.sink = b->k[3];
    K.lmul = exp2fx((K.linger - 0.5f) * 4.0f);
}

static void koi_note(Voice *v, const Blk *b)
{
    int i;
    (void)b;
    if (!v->u[0]) v->u[0] = 0x0C01F157u + v->idx * 7919u;
    if (v->env < 0.001f)
        for (i = 0; i < 6; i++)
            v->ph[i] = 0.0f;
    for (i = 0; i < 4; i++)
        v->s[i] = 1.0f;                          /* every partial struck at once */
    v->s[4] = 1.0f;                              /* the knock */
    v->s[5] = 1.0f;                              /* DROPS: how far below the note it still is */
    v->s[6] = 0.1f + 0.3f * rng_f(&v->u[0]);     /* RAIN: until the next drop */
    v->s[7] = 0.0f;
    v->s[8] = 1.0f;
    v->s[9] = 0.0f;                              /* the soft mallet's way in */
}

static void koi_render(Voice *v, const Blk *b, f32 *out, int n)
{
    const KAlgo *a = &KA[K.algo];
    f32 dt = (f32)n * b->isr, pitch = v->pitch + b->bend, m = K.mallet, ks, f, d0, d[4], g[4], d4, rd, inv, hit, hk, nlp = v->s[10], nk, att = v->s[9], da, atk, amp;
    f32 p0 = v->ph[0], p1 = v->ph[1], p2 = v->ph[2], p3 = v->ph[3], p4 = v->ph[4], strike;
    int algo = K.algo, i, k;
    f32 trim = algo == 4 ? 0.8f : (algo == 7 || algo == 3 ? 0.68f : 0.55f);

    if (algo == 7) {                             /* RAIN: while you hold it, it keeps landing, here and octaves up */
        if (v->gate) {
            v->s[6] -= dt;
            if (v->s[6] <= 0.0f) {
                static const f32 UP[6] = {0.0f, 12.0f, 12.0f, 24.0f, 19.0f, 0.0f};
                v->s[7] = UP[(rng_next(&v->u[0]) >> 8) % 6u];
                v->s[8] = 0.3f + 0.7f * rng_f(&v->u[0]);
                for (k = 0; k < 4; k++) v->s[k] = 1.0f;
                v->s[4] = 1.0f;
                v->s[6] = 0.05f + rng_f(&v->u[0]) * (0.55f - 0.42f * K.ripple);
            }
        }
    } else {
        v->s[7] = 0.0f;
        v->s[8] = 1.0f;
    }
    amp = v->s[8];
    hit = v->s[4];
    pitch += v->s[7];
    pitch -= K.sink * K.sink * 5.0f * (1.0f - exp2fx(-v->age * 4.0f));          /* SINK: the note bends as it goes under */
    if (algo == 4) {                             /* DROPS: it comes up to the note from below, fast */
        pitch -= 15.0f * v->s[5];
        v->s[5] *= exp2fx(-dt * 1.4427f / 0.028f);
    }
    ks = clampf(exp2fx((60.0f - pitch) * (1.0f / 30.0f)), 0.35f, 2.2f);       /* low bars ring longer */
    f = mtof(pitch);
    d0 = minf(f * b->isr, 0.45f);
    g[0] = a->a[0];
    g[1] = a->a[1] * (0.35f + 1.3f * m);
    g[2] = a->a[2] * (0.12f + 1.8f * m);
    g[3] = a->a[3] * (0.05f + 2.2f * m);
    for (k = 0; k < 4; k++) {
        d[k] = d0 * a->r[k];
        if (d[k] > 0.42f) { d[k] = 0.0f; g[k] = 0.0f; }                          /* too high to be a pitch any more */
        g[k] *= v->s[k];
    }
    /* RIPPLE: a second bar a few cycles a second away from the first */
    d4 = d0 + (a->beat + K.ripple * (0.5f + 6.0f * K.ripple)) * b->isr;
    rd = minf(0.85f, a->beat > 0.0f ? 0.3f + K.ripple : K.ripple * 1.5f);
    inv = 1.0f / (1.0f + rd * 0.6f);
    strike = a->strike * (0.12f + 1.5f * m * m);
    nk = 0.06f + 0.7f * m;
    hk = 1.0f - b->isr / (0.003f + 0.012f * (1.0f - m));
    atk = 0.007f * (1.0f - m) * (1.0f - m) + (algo == 4 ? 0.004f : 0.0f) + (algo == 6 ? 0.012f : 0.0f);
    da = atk > 0.0f ? b->isr / atk : 1.0f;

    for (i = 0; i < n; i++) {
        f32 x;
        nlp += (noise_s(&v->u[0]) - nlp) * nk;
        x = g[0] * (tsin(p0) + rd * tsin(p4)) * inv + g[1] * tsin(p1) + g[2] * tsin(p2) + g[3] * tsin(p3) + nlp * hit * strike;
        out[i] = x * trim * att * amp;
        p0 += d0; if (p0 >= 1.0f) p0 -= 1.0f;
        p1 += d[1]; if (p1 >= 1.0f) p1 -= 1.0f;
        p2 += d[2]; if (p2 >= 1.0f) p2 -= 1.0f;
        p3 += d[3]; if (p3 >= 1.0f) p3 -= 1.0f;
        p4 += d4; if (p4 >= 1.0f) p4 -= 1.0f;
        hit *= hk;
        att += da; if (att > 1.0f) att = 1.0f;
    }
    v->ph[0] = p0; v->ph[1] = p1; v->ph[2] = p2; v->ph[3] = p3; v->ph[4] = p4;
    for (k = 0; k < 4; k++)
        v->s[k] *= exp2fx(-dt * 1.4427f / (a->d[k] * K.lmul * ks));
    v->s[4] = hit;
    v->s[9] = att;
    v->s[10] = nlp;
}

static void koi_post(const Blk *b, f32 *l, f32 *r, int n)
{
    f32 cut = 16000.0f * exp2fx(-K.sink * 4.6f), k = onepole_k(1.0f / (H1_TAU * cut), b->sr);
    int i;
    for (i = 0; i < n; i++) {                    /* SINK: heard through the water */
        sink_l[0] += (l[i] - sink_l[0]) * k; sink_l[1] += (sink_l[0] - sink_l[1]) * k;
        sink_r[0] += (r[i] - sink_r[0]) * k; sink_r[1] += (sink_r[0] - sink_r[1]) * k;
        l[i] = sink_l[1];
        r[i] = sink_r[1];
    }
}

/* ---- picture ---------------------------------------------------------------------------- */
#define NFISH 10
#define NJ 9
#define NRIP 10
#define NTR 6
#define NDROP 12

typedef struct Fish {
    f32 j[NJ][2];                /* the spine, head first: each joint is towed by the one ahead */
    f32 hx, hy;                  /* which way it is facing */
    f32 sp, tx, ty, ph, ex, wait, fa;
    f32 tr[NTR][2];              /* where its tail has been */
    f32 tr_t;
    uint8_t tr_i, chase;
} Fish;
typedef struct Rip { f32 x, y, age, hard; } Rip;

static Fish fish[NFISH];
static Rip rip[NRIP];
static struct { f32 x, y, age; } drop[NDROP];
static uint32_t vser[NVOICE];
static f32 kR, bloom, drop_acc, pad_kick[5];
static int rip_i, drop_i;

static const struct { uint8_t n; f32 R; } FK[NALGO] = {
    {3, 8.5f}, {5, 6.0f}, {10, 3.0f}, {4, 7.0f}, {2, 9.5f}, {6, 5.5f}, {1, 14.0f}, {4, 7.0f},
};
static const f32 PAD[5][4] = {                   /* x, y, radius, where the notch points (turns) */
    {196.0f, 40.0f, 22.0f, 0.36f}, {223.0f, 80.0f, 13.0f, 0.55f}, {26.0f, 122.0f, 15.0f, 0.93f},
    {42.0f, 206.0f, 24.0f, 0.8f}, {88.0f, 224.0f, 12.0f, 0.62f},
};

static f32 hsh(uint32_t n) { n = (n ^ 61u) ^ (n >> 16); n *= 9u; n ^= n >> 4; n *= 0x27d4eb2du; n ^= n >> 15; return (f32)(n & 0xFFFFu) * (1.0f / 65536.0f); }

/* half the body's width along its length: a blunt head, widest at the shoulder, down to the wrist of the tail */
static f32 fish_w(f32 u)
{
    f32 t;
    if (u < 0.2f)
        return 0.62f + 0.38f * smooth01(u * 5.0f);
    t = (u - 0.2f) * 1.25f;
    return 1.0f - 0.82f * t * (0.55f + 0.45f * t);
}

static void fish_place(Fish *f, int i, f32 seg)
{
    int j;
    f32 x = 30.0f + 180.0f * hsh((uint32_t)i + 11u), y = 30.0f + 180.0f * hsh((uint32_t)i + 31u), s = (i & 1) ? -1.0f : 1.0f;
    for (j = 0; j < NJ; j++) {
        f->j[j][0] = x - s * seg * (f32)j;
        f->j[j][1] = y;
    }
    for (j = 0; j < NTR; j++) {
        f->tr[j][0] = f->j[NJ - 1][0];
        f->tr[j][1] = f->j[NJ - 1][1];
    }
    f->hx = s; f->hy = 0.0f;
    f->sp = 12.0f;
    f->tx = x; f->ty = y;
    f->ph = hsh((uint32_t)i + 51u);
    f->ex = 0.0f; f->wait = 0.0f; f->fa = 0.0f;
    f->tr_t = 0.0f; f->tr_i = 0; f->chase = 0;
}

static void fish_move(Fish *f, int i, int algo, f32 R, f32 lev, f32 dt, uint32_t *rng)
{
    f32 seg = R * 0.8f, hx = f->j[0][0], hy = f->j[0][1], dx, dy, d2, inv, cr, dot, amt, ang, want, base = R < 4.0f ? 30.0f : 15.0f + lev * 10.0f;
    int j;

    dx = f->tx - hx; dy = f->ty - hy;
    d2 = dx * dx + dy * dy;
    if (f->chase && (d2 < (R * 2.0f + 9.0f) * (R * 2.0f + 9.0f) || f->ex <= 0.0f)) {       /* got there: nothing to eat after all */
        f->chase = 0;
        f->wait = 0.0f;
    }
    if (!f->chase) {
        f->wait -= dt;
        if (algo == 1) {                         /* TONGUE: they go round, nose to tail */
            f32 rx = hx - 120.0f, ry = hy - 120.0f, ri = rsqrtf(rx * rx + ry * ry + 0.01f);
            rx *= ri; ry *= ri;
            f->tx = 120.0f + rx * 64.0f - ry * 46.0f;
            f->ty = 120.0f + ry * 64.0f + rx * 46.0f;
        } else if (algo == 2 && i > 0) {         /* KALIMBA: a school, and the first of them leads it */
            f->tx = fish[0].j[3][0] + (hsh((uint32_t)i + 71u) - 0.5f) * 76.0f;
            f->ty = fish[0].j[3][1] + (hsh((uint32_t)i + 91u) - 0.5f) * 76.0f;
        } else if (f->wait <= 0.0f || d2 < 400.0f) {
            f->tx = 26.0f + 188.0f * rng_f(rng);
            f->ty = 26.0f + 188.0f * rng_f(rng);
            f->wait = 3.0f + 5.0f * rng_f(rng);
        }
        dx = f->tx - hx; dy = f->ty - hy;
        d2 = dx * dx + dy * dy;
    }
    f->ex = maxf(0.0f, f->ex - dt * 0.45f);
    inv = rsqrtf(d2 + 0.01f);
    dx *= inv; dy *= inv;
    cr = f->hx * dy - f->hy * dx;                /* turn towards it, as fast as a fish can and no faster */
    dot = f->hx * dx + f->hy * dy;
    amt = dot < 0.0f ? (cr >= 0.0f ? 1.0f : -1.0f) : cr;
    want = base * (1.0f + 4.0f * f->ex);
    ang = amt * want / (R * 3.6f) * dt;         /* a long fish cannot turn in its own length */
    hx = f->hx - f->hy * ang; hy = f->hy + f->hx * ang;
    inv = rsqrtf(hx * hx + hy * hy + 1e-6f);
    f->hx = hx * inv; f->hy = hy * inv;
    want *= 0.3f + 0.7f * maxf(0.0f, dot);       /* it slows to turn */
    f->sp += (want - f->sp) * minf(1.0f, dt * 3.0f);
    f->ph = fractf(f->ph + dt * (0.55f + f->sp * 0.045f));
    f->j[0][0] += f->hx * f->sp * dt;
    f->j[0][1] += f->hy * f->sp * dt;
    for (j = 1; j < NJ; j++) {
        f32 ex = f->j[j][0] - f->j[j - 1][0], ey = f->j[j][1] - f->j[j - 1][1], k = seg * rsqrtf(ex * ex + ey * ey + 1e-4f);
        f->j[j][0] = f->j[j - 1][0] + ex * k;
        f->j[j][1] = f->j[j - 1][1] + ey * k;
        if (j >= 2) {                            /* a spine, not a rope: it wants to be straight */
            f32 stiff = minf(0.5f, dt * 3.0f);
            f->j[j][0] += (2.0f * f->j[j - 1][0] - f->j[j - 2][0] - f->j[j][0]) * stiff;
            f->j[j][1] += (2.0f * f->j[j - 1][1] - f->j[j - 2][1] - f->j[j][1]) * stiff;
        }
    }
    f->tr_t += f->sp * dt / maxf(8.0f, R * 1.7f);
    if (f->tr_t >= 1.0f) {
        f->tr_t = 0.0f;
        f->tr_i = (uint8_t)((f->tr_i + 1) % NTR);
        f->tr[f->tr_i][0] = f->j[NJ - 1][0];
        f->tr[f->tr_i][1] = f->j[NJ - 1][1];
    }
}

static void fish_draw(const Fish *f, int i, f32 R, f32 dim, f32 linger)
{
    f32 D[NJ][2], Q[NJ][2], a = f->fa * dim, tri[6], sw, bx, by, px, py, fl, live = sat(f->sp * (1.0f / 40.0f));
    int j, s, style = i & 3, small = R < 4.0f, ntr;

    if (a < 0.02f)
        return;
    /* the wake: the rings its tail left behind. LINGER keeps more of them. */
    ntr = small ? 2 : 1 + (int)(linger * (f32)(NTR - 1) + 0.5f);
    for (j = 0; j < ntr; j++) {
        int idx = ((int)f->tr_i - j + NTR) % NTR;
        f32 u = ((f32)j + f->tr_t) / (f32)ntr;
        if (u < 1.0f)
            g_ring(f->tr[idx][0], f->tr[idx][1], R * 0.3f + 1.0f + u * (f32)ntr * (1.6f + R * 0.2f), 1.0f, a * 0.4f * (1.0f - u));
    }
    for (j = 0; j < NJ; j++) {                   /* the swimming: a wave that grows towards the tail */
        f32 u = (f32)j / (f32)(NJ - 1), ex, ey, k, off;
        if (j == 0) { ex = f->hx; ey = f->hy; }
        else {
            ex = f->j[j - 1][0] - f->j[j][0]; ey = f->j[j - 1][1] - f->j[j][1];
            k = rsqrtf(ex * ex + ey * ey + 1e-4f);
            ex *= k; ey *= k;
        }
        Q[j][0] = ex; Q[j][1] = ey;
        off = R * 0.55f * u * u * sin_turns(f->ph - u * 0.75f) * (0.45f + 0.55f * live);
        D[j][0] = f->j[j][0] - ey * off;
        D[j][1] = f->j[j][1] + ex * off;
    }
    /* the tail fin: two lobes that follow the wave a moment late */
    bx = -Q[NJ - 1][0]; by = -Q[NJ - 1][1]; px = -by; py = bx;
    sw = sin_turns(f->ph - 0.95f) * (0.4f + 0.6f * live);
    fl = style == 2 && !small ? 1.45f : 1.0f;    /* one of them has the long fins */
    for (s = -1; s <= 1; s += 2) {
        f32 sd = (f32)s * R * 1.15f * fl + sw * R * 0.9f;
        tri[0] = D[NJ - 1][0] - bx * R * 0.3f; tri[1] = D[NJ - 1][1] - by * R * 0.3f;
        tri[2] = D[NJ - 1][0] + bx * R * 2.1f * fl + px * sd; tri[3] = D[NJ - 1][1] + by * R * 2.1f * fl + py * sd;
        tri[4] = D[NJ - 1][0] + bx * R * 1.15f * fl + px * sw * R * 0.5f; tri[5] = D[NJ - 1][1] + by * R * 1.15f * fl + py * sw * R * 0.5f;
        g_fill(tri, 3, a * 0.7f);
    }
    if (!small) {                                /* the two fins it steers with */
        f32 fp = 0.5f + 0.5f * sin_turns(f->ph * 2.0f + 0.2f);
        px = -Q[2][1]; py = Q[2][0];
        for (s = -1; s <= 1; s += 2) {
            f32 sd = (f32)s;
            tri[0] = D[1][0] + px * sd * R * 0.7f; tri[1] = D[1][1] + py * sd * R * 0.7f;
            tri[2] = D[2][0] + px * sd * R * (1.5f + 0.35f * fp) * fl - Q[2][0] * R * (1.25f - 0.45f * fp); tri[3] = D[2][1] + py * sd * R * (1.5f + 0.35f * fp) * fl - Q[2][1] * R * (1.25f - 0.45f * fp);
            tri[4] = D[3][0] + px * sd * R * 0.75f; tri[5] = D[3][1] + py * sd * R * 0.75f;
            g_fill(tri, 3, a * 0.6f);
        }
    }
    /* the body */
    if (style == 1 && !small) {                  /* a dark one: an outline, with the water showing through */
        for (s = 0; s < 2 * NJ - 1; s++) {
            f32 u = (f32)s / (f32)(2 * NJ - 2), x = s & 1 ? (D[s / 2][0] + D[s / 2 + 1][0]) * 0.5f : D[s / 2][0], y = s & 1 ? (D[s / 2][1] + D[s / 2 + 1][1]) * 0.5f : D[s / 2][1];
            g_disc(x, y, R * fish_w(u) + 1.1f, a * 0.9f);
        }
        g_mode(G_PAINT);
        for (s = 0; s < 2 * NJ - 1; s++) {
            f32 u = (f32)s / (f32)(2 * NJ - 2), x = s & 1 ? (D[s / 2][0] + D[s / 2 + 1][0]) * 0.5f : D[s / 2][0], y = s & 1 ? (D[s / 2][1] + D[s / 2 + 1][1]) * 0.5f : D[s / 2][1];
            g_disc(x, y, maxf(0.6f, R * fish_w(u) - 0.3f), a * 0.2f);
        }
        g_mode(G_MAX);
        for (j = 0; j < 3; j++) {                /* and its markings are the bright part */
            int jj = 1 + (int)(hsh((uint32_t)(i * 13 + j) + 300u) * 2.99f) + j * 2;
            f32 u = (f32)jj / (f32)(NJ - 1), w = R * fish_w(u), o = (hsh((uint32_t)(i * 13 + j) + 320u) - 0.5f) * 0.7f * w;
            if (jj < NJ - 1)
                g_disc(D[jj][0] - Q[jj][1] * o, D[jj][1] + Q[jj][0] * o, w * (0.45f + 0.2f * hsh((uint32_t)(i * 13 + j) + 340u)), a);
        }
    } else {
        int step = small ? 2 : 1;
        for (s = 0; s < 2 * NJ - 1; s += step) {
            f32 u = (f32)s / (f32)(2 * NJ - 2), x = s & 1 ? (D[s / 2][0] + D[s / 2 + 1][0]) * 0.5f : D[s / 2][0], y = s & 1 ? (D[s / 2][1] + D[s / 2 + 1][1]) * 0.5f : D[s / 2][1];
            g_disc(x, y, R * fish_w(u), a * 0.95f);
        }
        if (!small) {
            g_mode(G_PAINT);
            if (style == 0) {                    /* three patches, where they fell */
                for (j = 0; j < 3; j++) {
                    int jj = 1 + j * 2 + (int)(hsh((uint32_t)(i * 13 + j) + 300u) * 1.99f);
                    f32 u = (f32)jj / (f32)(NJ - 1), w = R * fish_w(u), o = (hsh((uint32_t)(i * 13 + j) + 320u) - 0.5f) * 0.8f * w;
                    g_disc(D[jj][0] - Q[jj][1] * o, D[jj][1] + Q[jj][0] * o, w * (0.5f + 0.22f * hsh((uint32_t)(i * 13 + j) + 340u)), a * 0.24f);
                }
            } else if (style == 2) {             /* one round mark on the head, and nothing else */
                g_disc(D[1][0] + Q[1][0] * R * 0.1f, D[1][1] + Q[1][1] * R * 0.1f, R * 0.5f, a * 0.24f);
            } else {                             /* freckled all the way down */
                for (j = 0; j < 9; j++) {
                    int jj = 1 + j % 6;
                    f32 u = (f32)jj / (f32)(NJ - 1), w = R * fish_w(u), o = (hsh((uint32_t)(i * 13 + j) + 320u) - 0.5f) * 1.3f * w;
                    f32 al = (hsh((uint32_t)(i * 13 + j) + 360u) - 0.5f) * R * 0.7f;
                    g_disc(D[jj][0] - Q[jj][1] * o + Q[jj][0] * al, D[jj][1] + Q[jj][0] * o + Q[jj][1] * al, maxf(1.0f, R * 0.16f), a * 0.24f);
                }
            }
            {                                    /* the ridge of its back */
                f32 xy[8];
                for (j = 0; j < 4; j++) { xy[2 * j] = D[j + 2][0]; xy[2 * j + 1] = D[j + 2][1]; }
                g_poly(xy, 4, 0, 1.0f, a * 0.66f);
            }
            g_mode(G_MAX);
        }
    }
    if (!small) {                                /* eyes, and the two whiskers a koi has */
        f32 er = maxf(1.0f, R * 0.14f);
        px = -Q[0][1]; py = Q[0][0];
        if (style != 1) g_mode(G_PAINT);
        for (s = -1; s <= 1; s += 2)
            g_disc(D[0][0] + Q[0][0] * R * 0.12f + px * (f32)s * R * 0.4f, D[0][1] + Q[0][1] * R * 0.12f + py * (f32)s * R * 0.4f, er, style == 1 ? a : 0.0f);
        g_mode(G_MAX);
        if (R >= 5.5f)
            for (s = -1; s <= 1; s += 2) {
                f32 x0 = D[0][0] + Q[0][0] * R * 0.55f + px * (f32)s * R * 0.22f, y0 = D[0][1] + Q[0][1] * R * 0.55f + py * (f32)s * R * 0.22f;
                g_line(x0, y0, x0 + Q[0][0] * R * 0.45f + px * (f32)s * R * 0.4f, y0 + Q[0][1] * R * 0.45f + py * (f32)s * R * 0.4f, 1.0f, a * 0.7f);
            }
    }
}

static void strike_at(f32 pitch, f32 hard, int algo, int nfish)
{
    int pc = ((int)(pitch + 0.5f) % 12 + 12) % 12, i, best = -1, second = -1;
    f32 x = 30.0f + sat((pitch - 53.0f) * (1.0f / 26.0f)) * 180.0f, y = 36.0f + (f32)((pc * 7) % 12) * (168.0f / 11.0f), bd = 1e9f, sd = 1e9f;
    Rip *r = &rip[rip_i];
    rip_i = (rip_i + 1) % NRIP;
    r->x = x; r->y = y; r->age = 0.0f; r->hard = hard;
    for (i = 0; i < 5; i++) {                    /* the pads near it rock */
        f32 dx = PAD[i][0] - x, dy = PAD[i][1] - y;
        pad_kick[i] = maxf(pad_kick[i], sat(1.3f - sqrtfx(dx * dx + dy * dy) * (1.0f / 110.0f)));
    }
    for (i = 0; i < nfish; i++) {                /* and whoever is nearest comes to see */
        f32 dx = fish[i].j[0][0] - x, dy = fish[i].j[0][1] - y, d = dx * dx + dy * dy + (fish[i].chase ? 4000.0f : 0.0f);
        if (d < bd) { sd = bd; second = best; bd = d; best = i; }
        else if (d < sd) { sd = d; second = i; }
    }
    for (i = 0; i < nfish; i++)
        if (i == best || (i == second && nfish >= 4) || algo == 2) {
            fish[i].tx = x + (algo == 2 ? (hsh((uint32_t)i + 71u) - 0.5f) * 44.0f : 0.0f);
            fish[i].ty = y + (algo == 2 ? (hsh((uint32_t)i + 91u) - 0.5f) * 44.0f : 0.0f);
            fish[i].chase = 1;
            fish[i].ex = 1.0f;
        }
}

static void pad_draw(int i, f32 t, f32 ripple)
{
    f32 x = PAD[i][0], y = PAD[i][1], r = PAD[i][2] * (1.0f + 0.05f * pad_kick[i] * sin_turns(t * 2.2f + (f32)i)), no = PAD[i][3] + 0.015f * sin_turns(t * 0.05f + (f32)i * 0.3f), tri[6];
    int j;
    x += (0.6f + 2.0f * ripple) * sin_turns(t * 0.11f + (f32)i * 0.37f);
    y += (0.6f + 2.0f * ripple) * sin_turns(t * 0.09f + (f32)i * 0.21f);
    g_mode(G_PAINT);
    g_disc(x, y, r, 0.15f);
    tri[0] = x; tri[1] = y;
    tri[2] = x + cos_turns(no - 0.045f) * (r + 3.0f); tri[3] = y + sin_turns(no - 0.045f) * (r + 3.0f);
    tri[4] = x + cos_turns(no + 0.045f) * (r + 3.0f); tri[5] = y + sin_turns(no + 0.045f) * (r + 3.0f);
    g_fill(tri, 3, 0.0f);
    g_mode(G_MAX);
    g_arc(x, y, r, no + 0.045f, no + 0.955f, 1.5f, 0.9f);
    g_line(x, y, x + cos_turns(no - 0.045f) * r, y + sin_turns(no - 0.045f) * r, 1.2f, 0.9f);
    g_line(x, y, x + cos_turns(no + 0.045f) * r, y + sin_turns(no + 0.045f) * r, 1.2f, 0.9f);
    for (j = 1; j < 8; j++) {                    /* veins */
        f32 an = no + (f32)j / 8.0f, L = r * (j & 1 ? 0.82f : 0.62f);
        g_line(x + cos_turns(an) * 2.5f, y + sin_turns(an) * 2.5f, x + cos_turns(an) * L, y + sin_turns(an) * L, 1.0f, 0.42f);
    }
}

static void petal(f32 cx, f32 cy, f32 an, f32 r0, f32 L, f32 w, f32 fill, f32 edge)
{
    f32 c = cos_turns(an), s = sin_turns(an), q[8];
    q[0] = cx + c * r0; q[1] = cy + s * r0;
    q[2] = cx + c * (r0 + L * 0.5f) - s * w; q[3] = cy + s * (r0 + L * 0.5f) + c * w;
    q[4] = cx + c * (r0 + L); q[5] = cy + s * (r0 + L);
    q[6] = cx + c * (r0 + L * 0.5f) + s * w; q[7] = cy + s * (r0 + L * 0.5f) - c * w;
    g_mode(G_PAINT);
    g_fill(q, 4, fill);
    g_mode(G_MAX);
    g_poly(q, 4, 1, 1.0f, edge);
}

static void stone(const f32 *xy, int n, f32 cx, f32 cy)
{
    static const f32 LV[6] = {0.07f, 0.15f, 0.1f, 0.2f, 0.08f, 0.13f};
    int i;
    g_mode(G_PAINT);
    for (i = 0; i < n; i++) {                    /* cut in facets, each catching a different light */
        int k = (i + 1) % n;
        f32 tri[6];
        tri[0] = cx; tri[1] = cy; tri[2] = xy[2 * i]; tri[3] = xy[2 * i + 1]; tri[4] = xy[2 * k]; tri[5] = xy[2 * k + 1];
        g_fill(tri, 3, LV[i % 6]);
    }
    g_mode(G_MAX);
    for (i = 0; i < n; i++)
        g_line(cx, cy, xy[2 * i], xy[2 * i + 1], 1.0f, 0.3f);
    g_poly(xy, n, 1, 1.5f, 0.8f);
}

/* (hidden) five notes down at once is a stone in the pond. The frog that was under a pad all along has had
 * enough: it comes up, says so, goes over to a quieter pad, and after a while slips back in. */
static f32 frog;
static int frog_done;

static APART void frog_run(const Vis *vis)
{
    f32 t = vis->t, u, j, fly, s, x, y, hx, hy, puff;
    int i;
    {
        int held = 0;
        for (i = 0; i < NVOICE; i++)
            held += vis->v[i].on && vis->v[i].gate && !vis->v[i].seq;
        if (held < 2)
            frog_done = 0;
        if (held >= 5 && !frog_done && frog <= 0.0f && EGGS_ON) {
            frog_done = 1;
            frog = 1.0f;
            eng_cue(CUE_KOI);
        }
    }
    if (frog <= 0.0f)
        return;
    frog -= vis->dt * 0.14f;
    if (frog <= 0.0f) {                          /* gone: the ring it leaves */
        rip[rip_i].x = PAD[3][0]; rip[rip_i].y = PAD[3][1] - 4.0f; rip[rip_i].age = 0.0f; rip[rip_i].hard = 0.0f;
        rip_i = (rip_i + 1) % NRIP;
        return;
    }
    u = 1.0f - frog; j = sat((u - 0.36f) * 7.0f); fly = sin_turns(j * 0.5f);
    s = sat(u * 14.0f) * sat((1.0f - u) * 12.0f) * (1.3f + 0.6f * fly);
    x = PAD[2][0] + (PAD[3][0] - PAD[2][0]) * j; y = PAD[2][1] + (PAD[3][1] - PAD[2][1]) * j - 4.0f;
    hx = 0.19f * s; hy = 0.98f * s;
    puff = u < 0.3f ? 0.5f + 0.5f * sin_turns(t * 3.0f) : 0.0f;
    for (i = -1; i <= 1; i += 2) {
        f32 px = -hy * (f32)i, py = hx * (f32)i, ax = x - 3.0f * hx + 4.0f * px, ay = y - 3.0f * hy + 4.0f * py;
        f32 kx = ax + (6.0f * px + 3.0f * hx) * (1.0f - fly) + (1.0f * px - 6.0f * hx) * fly, ky = ay + (6.0f * py + 3.0f * hy) * (1.0f - fly) + (1.0f * py - 6.0f * hy) * fly;
        g_line(ax, ay, kx, ky, 2.0f * s, 0.9f);                                     /* the back legs: folded, or out behind it */
        g_line(kx, ky, kx - 7.0f * hx + 2.0f * px * fly, ky - 7.0f * hy + 2.0f * py * fly, 1.6f * s, 0.9f);
    }
    g_disc(x, y, 6.5f * s, 0.92f);
    g_disc(x + 5.5f * hx, y + 5.5f * hy, (4.6f + 1.6f * puff) * s, 0.92f);
    for (i = -1; i <= 1; i += 2) {               /* its eyes stand out of its head */
        f32 ex = x + 7.0f * hx - 3.6f * hy * (f32)i, ey = y + 7.0f * hy + 3.6f * hx * (f32)i;
        g_disc(ex, ey, 2.3f * s, 1.0f);
        g_mode(G_PAINT);
        g_disc(ex, ey, 1.0f * s, 0.0f);
        g_mode(G_MAX);
    }
}

static void koi_draw(const Vis *vis)
{
    f32 dt = vis->dt, t = vis->t, mallet = vis->k[0], ripple = vis->k[1], linger = vis->k[2], sink = vis->k[3], mt = smooth01(vis->algo_t);
    f32 lev = sat(vis->level * 3.0f), life = 1.2f + 3.2f * linger, dim = 1.0f - 0.58f * sink, Rv;
    int algo = vis->algo, nfish = FK[algo].n, i, j;

    if (vis->fresh) {
        kR = FK[algo].R;
        for (i = 0; i < NFISH; i++) fish_place(&fish[i], i, kR * 0.8f);
        for (i = 0; i < NRIP; i++) rip[i].age = 1e3f;
        for (i = 0; i < NDROP; i++) drop[i].age = 1.0f;
        for (i = 0; i < NVOICE; i++) vser[i] = vis->v[i].serial;
        for (i = 0; i < 5; i++) pad_kick[i] = 0.0f;
        bloom = mallet;
        frog = 0.0f;
    }
    kR += (FK[algo].R - kR) * minf(1.0f, dt * 2.5f);
    for (i = 0; i < NVOICE; i++)
        if (vis->v[i].serial != vser[i]) {
            vser[i] = vis->v[i].serial;
            if (vis->v[i].on)
                strike_at(vis->v[i].pitch, mallet, algo, nfish);
        }
    for (i = 0; i < NFISH; i++) {
        Fish *f = &fish[i];
        f->fa += ((i < nfish ? 1.0f : 0.0f) - f->fa) * minf(1.0f, dt * 2.0f);
        if (i >= nfish) f->chase = 0;
        if (f->fa > 0.01f)
            fish_move(f, i, algo, kR, lev, dt, vis->rng);
    }
    for (i = 0; i < NRIP; i++) rip[i].age += dt;
    for (i = 0; i < 5; i++) pad_kick[i] = maxf(0.0f, pad_kick[i] - dt * 0.7f);
    bloom += (sat(mallet * 0.85f + lev * 0.25f) - bloom) * minf(1.0f, dt * 6.0f);

    /* GAMELAN plays at night: the moon is in the pond, and the water will not leave it alone */
    {
        f32 ma = algo == 5 ? mt : (vis->algo_prev == 5 ? 1.0f - mt : 0.0f);
        if (ma > 0.02f) {
            f32 cx = 122.0f, cy = 112.0f, r = 30.0f, wb = (0.6f + 2.4f * ripple + 2.0f * lev) * sin_turns(t * 0.21f);
            g_ellipse(cx, cy, r + wb, r - wb, 0.0f, 0.36f * ma);
            g_ellipse(cx, cy, r + 6.0f - wb * 1.5f, r + 6.0f + wb * 1.5f, 1.0f, 0.2f * ma);
            for (j = 0; j < 4; j++) {            /* and what is left of it further down the water */
                f32 y = cy + r + 9.0f + (f32)j * 7.0f, hw = (17.0f - (f32)j * 3.5f) * (0.8f + 0.2f * sin_turns(t * 0.3f + (f32)j * 0.3f));
                g_line(cx - hw + wb * 2.0f, y, cx + hw + wb * 2.0f, y, 1.6f, (0.3f - 0.05f * (f32)j) * ma);
            }
        }
    }
    /* RIPPLE: the pads are never quite still, and the rings go out from them */
    {
        int nr = 1 + (int)(ripple * 2.99f);
        for (i = 0; i < 5; i++)
            for (j = 0; j < nr; j++) {
                f32 u = fractf(t * 0.16f + (f32)i * 0.23f + (f32)j / (f32)nr);
                g_ring(PAD[i][0], PAD[i][1], PAD[i][2] + 3.0f + u * (16.0f + 22.0f * ripple), 1.0f, (0.1f + 0.5f * ripple) * (1.0f - u) * (0.6f + 0.4f * dim));
            }
    }
    /* where the notes fell */
    for (i = 0; i < NRIP; i++) {
        Rip *r = &rip[i];
        f32 u = r->age / life, big = (algo == 6 ? 1.7f : (algo == 4 ? 1.25f : 1.0f)), rad, fade;
        int nr = 2 + (int)(ripple * 2.99f) + (algo == 6 ? 1 : 0);
        if (u >= 1.0f) continue;
        rad = 5.0f + (34.0f + 52.0f * ripple) * big * ease_out(u);
        fade = (1.0f - u) * (1.0f - u);
        for (j = 0; j < nr; j++) {
            f32 rr = rad - (f32)j * (8.0f + 3.0f * big);
            if (rr > 2.0f)
                g_ring(r->x, r->y, rr, 1.8f - 0.3f * (f32)j, fade * (0.95f - 0.2f * (f32)j));
        }
    }

    /* the fish, the deep ones dimmer and smaller */
    Rv = kR * (1.0f - 0.2f * sink);
    for (i = NFISH - 1; i >= 0; i--)
        fish_draw(&fish[i], i, Rv, dim, linger);

    /* SINK: what comes up */
    {
        int nb = (int)(sink * 12.0f + 0.5f);
        for (i = 0; i < nb; i++) {
            f32 sp = 0.22f + 0.2f * hsh((uint32_t)i + 500u), u = fractf(hsh((uint32_t)i + 510u) + t * sp);
            uint32_t lap = (uint32_t)(int)(hsh((uint32_t)i + 510u) + t * sp) * 97u + (uint32_t)i;
            f32 x = 20.0f + 200.0f * hsh(lap + 520u) + 2.0f * sin_turns(t * 0.7f + (f32)i), y = 20.0f + 200.0f * hsh(lap + 540u);
            g_ring(x, y, 0.8f + 3.6f * u, 1.0f, sat(sink * 3.0f) * 0.75f * (u < 0.85f ? 1.0f : (1.0f - u) * 6.6f));
            if (u > 0.5f)
                g_ring(x + 5.0f, y + 3.0f, 0.6f + 2.0f * (u - 0.5f), 1.0f, sat(sink * 3.0f) * 0.5f * (1.0f - u) * 2.0f);
        }
    }

    /* the stones at the edge */
    {
        static const f32 SA[] = {-2, -2, 48, -2, 55, 12, 44, 31, 17, 39, -2, 31};
        static const f32 SB[] = {58, -2, 84, -2, 86, 9, 72, 15, 60, 9};
        static const f32 SC[] = {242, 242, 186, 242, 181, 225, 196, 205, 224, 198, 242, 206};
        static const f32 SD[] = {242, 196, 226, 190, 220, 172, 230, 160, 242, 160};
        stone(SA, 6, 22.0f, 14.0f);
        stone(SB, 5, 72.0f, 4.0f);
        stone(SC, 6, 216.0f, 224.0f);
        stone(SD, 5, 234.0f, 178.0f);
    }
    /* the pads, over the fish */
    for (i = 0; i < 5; i++)
        pad_draw(i, t, ripple);
    frog_run(vis);
    /* MALLET: the lotus. A bud for felt, wide open for wood. */
    {
        f32 cx = PAD[0][0] - 5.0f + (0.6f + 2.0f * ripple) * sin_turns(t * 0.11f), cy = PAD[0][1] + 4.0f + (0.6f + 2.0f * ripple) * sin_turns(t * 0.09f), o = bloom;
        f32 turn = 0.02f * sin_turns(t * 0.07f);
        for (i = 0; i < 9; i++)
            petal(cx, cy, turn + (f32)i / 9.0f, 1.0f, 6.0f + 11.0f * o, 3.6f - 0.8f * o, 0.42f, 0.95f);
        for (i = 0; i < 6; i++)
            petal(cx, cy, turn + ((f32)i + 0.5f) / 6.0f, 0.5f, 4.0f + 6.0f * o, 2.6f - 0.4f * o, 0.72f, 1.0f);
        g_mode(G_PAINT);
        g_disc(cx, cy, 1.4f + 1.6f * o, 0.2f);
        g_mode(G_MAX);
        g_ring(cx, cy, 1.4f + 1.6f * o, 1.0f, 1.0f);
    }
    /* petals from a tree you cannot see, going where the water takes them */
    for (i = 0; i < 6; i++) {
        f32 x = fractf(hsh((uint32_t)i + 600u) + t * (0.006f + 0.006f * hsh((uint32_t)i + 610u))) * 270.0f - 15.0f;
        f32 y = 14.0f + 212.0f * hsh((uint32_t)i + 620u) + 7.0f * sin_turns(t * 0.04f + (f32)i * 0.4f), an = t * (0.03f + 0.03f * hsh((uint32_t)i + 630u)) + (f32)i * 0.3f;
        f32 c = cos_turns(an), s = sin_turns(an), q[8];
        q[0] = x - c * 3.6f; q[1] = y - s * 3.6f; q[2] = x - s * 1.9f; q[3] = y + c * 1.9f;
        q[4] = x + c * 3.6f; q[5] = y + s * 3.6f; q[6] = x + s * 1.9f; q[7] = y - c * 1.9f;
        g_fill(q, 4, 0.8f);
    }
    /* the splash: MALLET throws the drops */
    for (i = 0; i < NRIP; i++) {
        Rip *r = &rip[i];
        f32 u = r->age * (1.0f / 0.42f), big = algo == 4 ? 1.6f : 1.0f;
        int nd = 4 + (int)(r->hard * 9.0f);
        if (u >= 1.0f) continue;
        for (j = 0; j < nd; j++) {
            f32 an = ((f32)j + hsh((uint32_t)(i * 17 + j) + 700u) * 0.7f) / (f32)nd, far = (10.0f + 40.0f * r->hard * (0.5f + 0.5f * hsh((uint32_t)(i * 17 + j) + 720u))) * big * ease_out(u);
            g_disc(r->x + cos_turns(an) * (4.0f + far), r->y + sin_turns(an) * (4.0f + far), (1.0f + 1.3f * r->hard) * (1.0f - u * 0.6f), (1.0f - u) * (0.5f + 0.5f * r->hard));
        }
        g_disc(r->x, r->y, 3.0f * (1.0f - u) * big, 1.0f - u);
    }
    /* RAIN: it comes down harder the more you play */
    {
        f32 ra = algo == 7 ? mt : (vis->algo_prev == 7 ? 1.0f - mt : 0.0f);
        if (ra > 0.02f) {
            drop_acc += dt * (3.0f + 34.0f * lev);
            while (drop_acc >= 1.0f) {
                drop_acc -= 1.0f;
                drop[drop_i].x = 6.0f + 228.0f * rng_f(vis->rng);
                drop[drop_i].y = 6.0f + 228.0f * rng_f(vis->rng);
                drop[drop_i].age = 0.0f;
                drop_i = (drop_i + 1) % NDROP;
            }
            for (i = 0; i < NDROP; i++)
                if (drop[i].age < 1.0f) {
                    g_ring(drop[i].x, drop[i].y, 1.0f + 11.0f * ease_out(drop[i].age), 1.0f, ra * 0.7f * (1.0f - drop[i].age));
                    if (drop[i].age < 0.25f)
                        g_disc(drop[i].x, drop[i].y, 1.3f, ra * (1.0f - drop[i].age * 4.0f));
                    drop[i].age += dt * 1.5f;
                }
        }
    }
}

const InstDef inst_koi = {
    "KOI",
    {104, 238, 150},
    {"MALLET", "RIPPLE", "LINGER", "SINK"},
    {"PAN", "TONGUE", "KALIMBA", "MARIMBA", "DROPS", "GAMELAN", "BOWL", "RAIN"},
    {{50, 30, 50, 8}, 0, {0, 60, 100, 78}, {0, 55, 0, 0}, {22, 5, 46, 55}, {0, 3, 1, 60}, {0, 0, 0}},
    0.3f,
    koi_block, koi_note, koi_render, koi_post, koi_draw,
};
