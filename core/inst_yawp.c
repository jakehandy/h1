/* H1 - YAWP: the choir. A buzz or a breath through three formant filters, so every voice has a
 * mouth, and every voice on the screen is a head with one: play one note and one big head sings
 * it, play a chord and the screen crowds with a head per note.
 *
 *   K1 VOWEL   A - E - I - O - U                       the shape of every mouth
 *   K2 WOBBLE  vibrato                                 the heads quiver and sway
 *   K3 BREATH  voice .. whisper                        puffs of breath leave the mouths
 *   K4 SIZE    small throat .. giant                   small heads .. heads too big for the screen
 */
#include "internal.h"
#include "dsp.h"

/* ---- sound ------------------------------------------------------------------------------ */
static const f32 VOW[5][3] = {{730, 1090, 2440}, {530, 1840, 2480}, {270, 2290, 3010}, {570, 840, 2410}, {300, 870, 2240}};
static const f32 VGA[5][3] = {{1.0f, 0.5f, 0.3f}, {1.0f, 0.4f, 0.3f}, {1.0f, 0.3f, 0.25f}, {1.0f, 0.45f, 0.15f}, {1.0f, 0.3f, 0.1f}};

typedef struct YAlgo {
    uint8_t src;                 /* 0 two saws, 1 narrow pulse, 2 square, 3 breath only */
    f32 transpose, fscale, q, vibmin, vibrate;
} YAlgo;

static const YAlgo YA[NALGO] = {
    /* CHOIR   */ {0, 0.0f, 1.0f, 8.0f, 0.06f, 5.2f},
    /* MONK    */ {1, -12.0f, 0.84f, 11.0f, 0.03f, 4.4f},
    /* ROBOT   */ {2, 0.0f, 1.0f, 14.0f, 0.0f, 0.0f},
    /* BABY    */ {0, 12.0f, 1.38f, 7.0f, 0.1f, 7.0f},
    /* OPERA   */ {0, 0.0f, 0.94f, 9.0f, 0.3f, 5.6f},
    /* WHISPER */ {3, 0.0f, 1.0f, 13.0f, 0.0f, 5.0f},
    /* ALIEN   */ {1, 0.0f, 1.15f, 12.0f, 0.05f, 9.0f},
    /* YAYA    */ {0, 0.0f, 1.06f, 8.0f, 0.05f, 5.5f},
};

static struct {
    int algo;
    f32 pos, size, vib, breath, beat;
} Y;

static void yawp_block(const Blk *b)
{
    const YAlgo *a = &YA[b->algo];
    Y.algo = b->algo;
    Y.pos = b->k[0] * 4.0f;
    if (b->algo == 2)
        Y.pos = (f32)(int)(Y.pos + 0.5f);        /* ROBOT snaps to the five vowels */
    Y.size = exp2fx(0.55f - 1.1f * b->k[3]) * a->fscale;
    Y.vib = maxf(a->vibmin, b->k[1] * b->k[1] * (b->algo == 4 ? 1.2f : 0.7f));
    if (b->algo == 2) Y.vib = 0.0f;
    Y.breath = b->algo == 5 ? 1.0f : b->k[2];
    Y.beat = b->bpm * (1.0f / 60.0f);
}

static void yawp_note(Voice *v, const Blk *b)
{
    int i;
    (void)b;
    if (!v->u[0]) v->u[0] = 0xB5297A4Du + v->idx * 977u;
    if (v->env < 0.001f) {
        for (i = 0; i < 7; i++)
            v->s[i] = 0.0f;
        v->ph[2] = v->r1;
    }
}

static void yawp_render(Voice *v, const Blk *b, f32 *out, int n)
{
    const YAlgo *a = &YA[Y.algo];
    Svf f[3];
    f32 pitch = v->pitch + b->bend + a->transpose, pos = Y.pos, fs = Y.size, amp = 1.0f, ga[3], d, d2, fr;
    f32 ph = v->ph[0], ph2 = v->ph[1], tilt = v->s[6], br = Y.breath, voiced, q = a->q, ringd = 0.0f, rph = v->ph[3];
    int i, j, vi;

    v->ph[2] = fractf(v->ph[2] + a->vibrate * (0.9f + 0.2f * v->r2) * (f32)n * b->isr);
    pitch += Y.vib * tsin(v->ph[2]) * sat((v->age - 0.1f) * 3.0f + (Y.algo == 4 ? 0.5f : 0.0f));
    switch (Y.algo) {
    case 2: pitch = (f32)(int)(v->target + 0.5f) + b->bend; break;        /* ROBOT does not slide */
    case 3: pos = lerpf(4.0f, pos, smooth01(v->age * 7.0f)); break;        /* BABY: every note starts "oo" and opens: wa */
    case 5: fs *= exp2fx((pitch - 60.0f) * (1.0f / 30.0f)); break;        /* WHISPER: the key moves the mouth, there is no pitch */
    case 6: fs *= 1.0f + 0.22f * tsin(v->age * (9.0f + 9.0f * v->r1)); ringd = 1.0f; break;
    case 7: {                                    /* YAYA: ee-ah, ee-ah, twice a beat */
        f32 cyc = fractf(v->age * Y.beat * 2.0f);
        pos = lerpf(2.0f, pos, smooth01(cyc * 3.5f));
        amp = 0.35f + 0.65f * sat(cyc * 8.0f) * sat((1.0f - cyc) * 6.0f);
        break;
    }
    default: break;
    }
    pos = clampf(pos, 0.0f, 3.999f);
    vi = (int)pos;
    fr = pos - (f32)vi;
    for (j = 0; j < 3; j++) {
        svf_set(&f[j], lerpf(VOW[vi][j], VOW[vi + 1][j], fr) * fs, q, b->isr);
        ga[j] = lerpf(VGA[vi][j], VGA[vi + 1][j], fr) * (Y.algo == 4 && j == 2 ? 2.6f : 1.0f);
    }
    d = minf(mtof(pitch) * b->isr, 0.45f);
    d2 = d * 1.0028f;
    if (a->src == 0) d *= 0.9972f;
    ringd *= d * 1.5f;
    voiced = a->src == 3 ? 0.0f : 1.0f - br * 0.9f;
    br = br * br * 1.6f + (a->src == 3 ? 0.4f : 0.02f);

    for (i = 0; i < n; i++) {
        f32 x, lo, b0, b1, b2;
        switch (a->src) {
        default: x = 0.5f * (saw_bl(ph, d) + saw_bl(ph2, d2)); break;
        case 1: x = pulse_bl(ph, d, 0.18f); break;
        case 2: x = pulse_bl(ph, d, 0.5f) * 0.8f; break;
        case 3: x = 0.0f; break;
        }
        if (ringd > 0.0f) {
            x *= tsin(rph);
            rph += ringd; if (rph >= 1.0f) rph -= 1.0f;
        }
        x = x * voiced + noise_s(&v->u[0]) * br;
        tilt += 0.3f * (x - tilt);
        SVF_TICK(f[0], &v->s[0], tilt, lo, b0);
        SVF_TICK(f[1], &v->s[2], tilt, lo, b1);
        SVF_TICK(f[2], &v->s[4], tilt, lo, b2);
        (void)lo;
        out[i] = ((b0 * ga[0] + b1 * ga[1] + b2 * ga[2]) * 0.42f + tilt * 0.1f) * amp;
        ph += d; if (ph >= 1.0f) ph -= 1.0f;
        ph2 += d2; if (ph2 >= 1.0f) ph2 -= 1.0f;
    }
    v->ph[0] = ph; v->ph[1] = ph2; v->ph[3] = rph;
    v->s[6] = tilt;
}

/* ---- picture ---------------------------------------------------------------------------- */
#define NPUFF 20
typedef struct Face {
    f32 x, y, vx, vy, r, rv, open, pitch, seed, blink;
    uint32_t serial;
    uint8_t live;
} Face;
static Face F[NVOICE + 1];                       /* one per voice, and the one who waits when nobody plays */
static struct { f32 x, y, vx, vy, life, r; } puff[NPUFF];
static f32 puff_acc;

static const f32 SLOT9[9][2] = {{42, 42}, {198, 42}, {42, 198}, {198, 198}, {120, 42}, {42, 120}, {198, 120}, {120, 198}, {120, 120}};
static const f32 MOUTH[5][2] = {{0.42f, 0.40f}, {0.50f, 0.22f}, {0.56f, 0.11f}, {0.26f, 0.32f}, {0.15f, 0.17f}};

static void slot(int n, int k, f32 *x, f32 *y, f32 *r)
{
    switch (n) {
    case 1: *x = 120; *y = 122; *r = 86; break;
    case 2: *x = k ? 177.0f : 63.0f; *y = 120; *r = 54; break;
    case 3: *x = k == 0 ? 120.0f : (k == 1 ? 62.0f : 178.0f); *y = k == 0 ? 66.0f : 174.0f; *r = 50; break;
    case 4: *x = (k & 1) ? 177.0f : 63.0f; *y = (k & 2) ? 177.0f : 63.0f; *r = 52; break;
    case 5:
        if (k == 4) { *x = 120; *y = 120; }
        else { *x = (k & 1) ? 192.0f : 48.0f; *y = (k & 2) ? 192.0f : 48.0f; }
        *r = 44; break;
    case 6: *x = 42.0f + 78.0f * (f32)(k % 3); *y = k < 3 ? 76.0f : 164.0f; *r = 36; break;
    default: *x = SLOT9[k][0]; *y = SLOT9[k][1]; *r = 35; break;
    }
}

static void face(Face *f, const Vis *vis, int algo, f32 pos, f32 wob, f32 size, f32 fade)
{
    f32 R = f->r * size, tall = clampf((f->pitch - 65.0f) / 14.0f, -1.0f, 1.0f);
    f32 q = wob * 0.07f * sin_turns(vis->t * 5.2f + f->seed), sx = 1.0f - 0.07f * tall - q, sy = 1.0f + 0.1f * tall + q;
    f32 x = f->x + wob * 7.0f * sin_turns(vis->t * 0.9f + f->seed * 3.0f), y = f->y + wob * 3.0f * sin_turns(vis->t * 1.3f + f->seed);
    f32 mw, mh, my, ex, ey, er, shut = f->blink < 0.0f ? 0.15f : 1.0f, fr, ink = 0.0f;
    int vi, hollow = algo == 5, e, ne = algo == 6 ? 3 : 2;

    if (R < 2.0f)
        return;
    pos = clampf(pos, 0.0f, 3.999f);
    vi = (int)pos;
    fr = pos - (f32)vi;
    mw = lerpf(MOUTH[vi][0], MOUTH[vi + 1][0], fr) * R * sx;
    mh = lerpf(MOUTH[vi][1], MOUTH[vi + 1][1], fr) * R * sy * (0.1f + 0.9f * f->open);
    my = y + 0.36f * R * sy;
    ex = 0.36f * R * sx;
    ey = y - 0.2f * R * sy;
    er = 0.13f * R;

    /* the head */
    g_mode(G_MAX);
    if (algo == 2) {                             /* ROBOT: a box with an aerial */
        g_rect(x - R * sx * 0.9f, y - R * sy * 0.85f, R * sx * 1.8f, R * sy * 1.7f, fade);
        g_line(x, y - R * sy * 0.85f, x, y - R * sy * 1.12f, maxf(2.0f, R * 0.06f), fade);
        g_disc(x, y - R * sy * 1.15f, maxf(2.5f, R * 0.09f), fade);
    } else if (hollow) {                         /* WHISPER: nobody there, only an outline */
        g_ellipse(x, y, R * sx, R * sy, maxf(2.0f, R * 0.05f), 0.75f * fade);
        ink = 0.9f * fade;
    } else if (algo == 1) {                      /* MONK: a long head */
        g_ellipse(x, y, R * sx * 0.86f, R * sy * 1.04f, 0.0f, fade);
    } else if (algo == 6) {                      /* ALIEN: wide at the top */
        g_ellipse(x, y - R * 0.12f, R * sx * 1.02f, R * sy * 0.8f, 0.0f, fade);
        g_ellipse(x, y + R * 0.3f, R * sx * 0.62f, R * sy * 0.7f, 0.0f, fade);
    } else
        g_ellipse(x, y, R * sx, R * sy, 0.0f, fade);
    if (algo == 3) {                             /* BABY: one curl */
        g_arc(x + R * 0.1f, y - R * sy * 1.08f, R * 0.16f, 0.25f, 0.95f, maxf(2.0f, R * 0.06f), fade);
    }
    if (!hollow)
        g_mode(G_PAINT);

    /* eyes */
    for (e = 0; e < ne; e++) {
        f32 cx = e == 2 ? x : x + (e ? ex : -ex), cy = e == 2 ? ey - 0.34f * R : ey;
        switch (algo) {
        case 0:                                  /* CHOIR: shut tight and happy while they sing */
            if (f != &F[NVOICE] && f->open > 0.3f) g_arc(cx, cy + er * 0.5f, er * 1.1f, 0.55f, 0.95f, maxf(2.0f, R * 0.07f), ink);
            else g_ellipse(cx, cy, er, er * shut, 0.0f, ink);
            break;
        case 1:                                  /* MONK: closed */
            g_line(cx - er * 1.3f, cy, cx + er * 1.3f, cy, maxf(2.0f, R * 0.07f), ink);
            break;
        case 2:
            g_rect(cx - er, cy - er * shut, er * 2.0f, er * 2.0f * shut, ink);
            break;
        case 3:                                  /* BABY: enormous */
            g_ellipse(cx * 1.0f + (e ? er * 0.3f : -er * 0.3f), cy + er, er * 1.5f, er * 1.5f * shut, 0.0f, ink);
            if (shut > 0.5f) {
                g_mode(G_MAX);
                g_disc(cx + (e ? er * 0.3f : -er * 0.3f) - er * 0.4f, cy + er * 0.5f, er * 0.45f, fade);
                g_mode(G_PAINT);
            }
            break;
        case 4:                                  /* OPERA: eyes shut downward, brows up */
            g_arc(cx, cy - er * 0.4f, er * 1.2f, 0.05f, 0.45f, maxf(2.0f, R * 0.07f), ink);
            g_line(cx - er * 1.4f, cy - er * (e ? 1.9f : 2.9f) - f->open * er, cx + er * 1.4f, cy - er * (e ? 2.9f : 1.9f) - f->open * er, maxf(1.5f, R * 0.05f), ink);
            break;
        case 6:                                  /* ALIEN: three, tall */
            g_ellipse(cx, cy, er * 0.8f, er * 1.7f * shut, 0.0f, ink);
            break;
        case 7:                                  /* YAYA: wide awake */
            g_ellipse(cx, cy, er * 1.35f, er * 1.35f * shut, 0.0f, ink);
            if (shut > 0.5f) {
                g_mode(G_MAX);
                g_disc(cx + er * 0.5f * sin_turns(vis->t * 1.7f + f->seed), cy + er * 0.4f * sin_turns(vis->t * 2.3f), er * 0.5f, fade);
                g_mode(G_PAINT);
            }
            break;
        default:
            g_ellipse(cx, cy, er, er * shut, 0.0f, ink);
            break;
        }
    }

    /* the mouth */
    if (algo == 2) {
        int t;
        g_rect(x - mw, my - mh, mw * 2.0f, mh * 2.0f, ink);
        if (mh > 4.0f) {
            g_mode(G_MAX);
            for (t = -1; t <= 1; t++)
                g_line(x + (f32)t * mw * 0.5f, my - mh, x + (f32)t * mw * 0.5f, my + mh, maxf(1.5f, R * 0.035f), fade);
            g_mode(G_PAINT);
        }
    } else if (hollow)
        g_ellipse(x, my, mw, maxf(mh, 1.0f), maxf(2.0f, R * 0.05f), ink);
    else {
        g_ellipse(x, my, mw, maxf(mh, R * 0.025f), 0.0f, ink);
        if (algo == 4 && mh > R * 0.16f) {       /* OPERA: you can see the uvula */
            g_mode(G_MAX);
            g_disc(x, my - mh + R * 0.05f, R * 0.07f, fade);
        }
    }
    g_mode(G_MAX);
}

static void yawp_draw(const Vis *vis)
{
    f32 dt = vis->dt, pos = vis->k[0] * 4.0f, wob = vis->k[1], breath = vis->k[2], size = lerpf(0.72f, 1.3f, vis->k[3]);
    int i, n = 0, k = 0, order[NVOICE];
    Face *res = &F[NVOICE];

    if (vis->algo == 2) pos = (f32)(int)(pos + 0.5f);
    if (vis->algo == 5) breath = 1.0f;
    if (vis->fresh) {
        for (i = 0; i <= NVOICE; i++) {
            F[i].r = F[i].rv = 0.0f;
            F[i].live = 0;
            F[i].seed = (f32)i * 0.37f;
            F[i].blink = 1.0f + (f32)i * 0.4f;
        }
        res->x = 120; res->y = 122; res->pitch = 65.0f;
        for (i = 0; i < NPUFF; i++)
            puff[i].life = 0.0f;
    }
    for (i = 0; i < NVOICE; i++)
        if (vis->v[i].on && (vis->v[i].gate || vis->v[i].env > 0.05f))
            order[n++] = i;

    for (i = 0; i < NVOICE; i++) {
        Face *f = &F[i];
        const VVoice *v = &vis->v[i];
        f32 tx = f->x, ty = f->y, tr = 0.0f, topen = 0.0f;
        if (k < n && order[k] == i) {
            slot(n, k, &tx, &ty, &tr);
            k++;
            if (!f->live) {                      /* a new head: it pops up where it belongs */
                f->x = tx; f->y = ty; f->vx = f->vy = 0.0f;
                f->r = 0.0f; f->rv = 260.0f;
                f->live = 1;
            }
            if (v->serial != f->serial) {
                f->serial = v->serial;
                f->rv += 140.0f;
            }
            f->pitch += (v->pitch - f->pitch) * minf(1.0f, dt * 20.0f);
            topen = v->gate ? 0.35f + 0.65f * v->env : v->env * 0.8f;
        }
        spring(&f->x, &f->vx, tx, 14.0f, dt);
        spring(&f->y, &f->vy, ty, 14.0f, dt);
        spring_z(&f->r, &f->rv, tr, tr > 0.0f ? 20.0f : 26.0f, tr > 0.0f ? 0.5f : 1.0f, dt);
        f->open += (topen - f->open) * minf(1.0f, dt * 22.0f);
        if (tr <= 0.0f && f->r < 1.5f) { f->live = 0; f->r = 0.0f; }
    }
    /* the one who waits: mouths the vowel silently so the knobs show before a note */
    spring_z(&res->r, &res->rv, n ? 0.0f : 86.0f, 16.0f, n ? 1.0f : 0.55f, dt);
    res->open += ((0.3f + 0.06f * sin_turns(vis->t * 0.25f)) - res->open) * minf(1.0f, dt * 8.0f);
    if (res->r < 0.0f) res->r = 0.0f;

    for (i = 0; i <= NVOICE; i++) {
        Face *f = &F[i];
        f->blink -= dt;
        if (f->blink < -0.12f)
            f->blink = 1.2f + 3.5f * rng_f(vis->rng);
        if (f->r > 2.0f)
            face(f, vis, vis->algo, pos, wob, size * (vis->algo_t < 1.0f ? 0.8f + 0.2f * ease_out(vis->algo_t) : 1.0f), 1.0f);
    }

    /* breath */
    puff_acc += dt * breath * breath * (n ? 26.0f : 5.0f);
    while (puff_acc >= 1.0f) {
        Face *f = n ? &F[order[rng_next(vis->rng) % (uint32_t)n]] : res;
        puff_acc -= 1.0f;
        for (i = 0; i < NPUFF; i++)
            if (puff[i].life <= 0.0f) {
                puff[i].x = f->x + rng_s(vis->rng) * f->r * 0.2f;
                puff[i].y = f->y + 0.36f * f->r * size;
                puff[i].vx = rng_s(vis->rng) * 40.0f;
                puff[i].vy = -(20.0f + 50.0f * rng_f(vis->rng));
                puff[i].life = 1.0f;
                puff[i].r = 2.0f;
                break;
            }
    }
    for (i = 0; i < NPUFF; i++) {
        if (puff[i].life <= 0.0f)
            continue;
        puff[i].x += puff[i].vx * dt;
        puff[i].y += puff[i].vy * dt;
        puff[i].vy -= 40.0f * dt;
        puff[i].vx += 30.0f * sin_turns(vis->t * 0.7f + (f32)i * 0.3f) * dt;
        puff[i].r += dt * 9.0f;
        puff[i].life -= dt * 0.75f;
        g_mode(G_PAINT);
        g_ring(puff[i].x, puff[i].y, puff[i].r, 1.6f, 0.0f);
        g_mode(G_MAX);
        g_ring(puff[i].x, puff[i].y, puff[i].r, 1.6f, sat(puff[i].life * 2.0f) * 0.8f);
    }
}

const InstDef inst_yawp = {
    "YAWP",
    {255, 255, 255},
    {"VOWEL", "WOBBLE", "BREATH", "SIZE"},
    {"CHOIR", "MONK", "ROBOT", "BABY", "OPERA", "WHISPER", "ALIEN", "YAYA"},
    {{0, 30, 12, 50}, 0, {22, 40, 85, 36}, {0, 55, 0, 0}, {0, 3, 28, 50}, {0, 3, 1, 60}, {0, 0, 0}},
    0.3f,
    yawp_block, yawp_note, yawp_render, 0, yawp_draw,
};
