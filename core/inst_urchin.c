/* h1 - URCHIN: the FM instrument. Two modulators crossfading under one carrier, eight ways to
 * wire them, and a spiny round thing whose outline is itself an FM waveform wrapped in a circle.
 *
 *   K1 RATIO   which overtones the modulator adds      the wiggles riding on his spines
 *   K2 DEPTH   how hard it modulates                   how long and how bent the spines are
 *   K3 FUZZ    modulator feedback, towards noise       his outline bristles
 *   K4 TAIL    how long the brightness (and the note) rings on       rings nested inside him, and
 *                                                      how far the echoes of each note travel
 * Every note pushes a spine out at its place on the clock of twelve pitches, so a chord is a shape.
 */
#include "internal.h"
#include "dsp.h"

/* ---- sound ------------------------------------------------------------------------------ */
typedef struct UAlgo {
    f32 crat;                    /* carrier ratio */
    f32 rat[6];                  /* RATIO walks through these */
    f32 idx;                     /* modulation at full DEPTH, in turns */
    f32 adec;                    /* how fast the note itself dies, 0 = it sustains */
    f32 matt;                    /* seconds for the modulation to arrive (brass) */
    uint8_t mode;                /* 0 plain, 1 two carriers, 2 organ stack, 3 ring, 4 carrier feedback */
} UAlgo;

static const UAlgo UA[NALGO] = {
    /* KEYS  */ {1.0f, {1.0f, 2.0f, 3.0f, 5.0f, 7.0f, 14.0f}, 0.50f, 0.9f, 0.0f, 0},
    /* BELL  */ {1.0f, {1.41f, 2.0f, 2.76f, 3.5f, 5.4f, 7.1f}, 0.60f, 0.7f, 0.0f, 1},
    /* BASS  */ {0.5f, {0.5f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f}, 0.90f, 0.25f, 0.0f, 0},
    /* BRASS */ {1.0f, {0.5f, 1.0f, 1.0f, 2.0f, 3.0f, 4.0f}, 0.70f, 0.0f, 0.07f, 1},
    /* ORGAN */ {1.0f, {1.0f, 2.0f, 3.0f, 4.0f, 6.0f, 8.0f}, 0.35f, 0.0f, 0.0f, 2},
    /* GLASS */ {1.0f, {4.0f, 5.0f, 6.0f, 7.0f, 9.0f, 11.0f}, 0.38f, 0.5f, 0.0f, 1},
    /* METAL */ {1.0f, {1.5f, 2.33f, 3.14f, 4.71f, 6.3f, 8.2f}, 0.70f, 0.8f, 0.0f, 3},
    /* CHAOS */ {1.0f, {0.5f, 1.01f, 1.99f, 3.03f, 5.1f, 7.77f}, 1.00f, 0.0f, 0.0f, 4},
};

static struct {
    int algo;
    f32 ra, rb, wb;              /* the two modulator slots and the weight of the second */
    f32 index, fb, mrate, floor, along;
} B;

static void urchin_block(const Blk *b)
{
    const UAlgo *a = &UA[b->algo];
    f32 x = b->k[0] * 4.999f, fr, t = b->k[3];
    int i = (int)x;
    fr = x - (f32)i;
    B.algo = b->algo;
    /* even ratios live in slot A, odd ones in slot B: a slot only changes ratio while it is silent */
    B.ra = a->rat[2 * ((i + 1) / 2)];
    B.rb = a->rat[2 * (i / 2) + 1];
    B.wb = (i & 1) ? 1.0f - fr : fr;
    B.index = b->k[1] * sqrtfx(b->k[1]) * a->idx;
    B.fb = b->k[2] * b->k[2] * 0.3f;
    B.mrate = 1.0f / (0.04f * exp2fx(t * 7.0f));          /* 40 ms .. 5 s */
    B.floor = t > 0.96f ? 1.0f : 0.06f + 0.5f * t * t;
    B.along = exp2fx(2.0f - t * 4.0f);                    /* the note's own decay: 4x .. 1/4x */
}

static void urchin_note(Voice *v, const Blk *b)
{
    (void)b;
    if (v->env < 0.001f) {
        v->ph[0] = v->ph[1] = v->ph[2] = v->ph[3] = 0.0f;
        v->s[0] = v->s[1] = v->s[2] = v->s[3] = 0.0f;
        v->s[4] = 1.0f;
    }
}

static void urchin_render(Voice *v, const Blk *b, f32 *out, int n)
{
    const UAlgo *a = &UA[B.algo];
    f32 pitch = v->pitch + b->bend, f, dc, da, db, dd;
    f32 ks = clampf(exp2fx((60.0f - pitch) * (1.0f / 24.0f)), 0.3f, 1.6f);
    f32 menv, I = v->s[0], I1, dI, amp = v->s[4], amp1, damp;
    f32 pc = v->ph[0], pa = v->ph[1], pb = v->ph[2], pd = v->ph[3];
    f32 za = v->s[1], zb = v->s[2], zo = v->s[3], wa = 1.0f - B.wb, wb = B.wb, fb = B.fb;
    int i, mode = a->mode;

    if (mode == 4)                               /* CHAOS never plays it straight */
        pitch += 0.35f * tsin(v->age * (3.0f + 5.0f * v->r1)) * v->r2;
    f = mtof(pitch);
    dc = minf(f * a->crat * b->isr, 0.45f);
    da = minf(f * B.ra * b->isr, 0.45f);
    db = minf(f * B.rb * b->isr, 0.45f);
    dd = minf(dc * (mode == 3 ? 3.53f : 1.0035f), 0.45f);

    menv = B.floor + (1.0f - B.floor) * exp2fx(-v->age * B.mrate * 1.4427f);
    if (a->matt > 0.0f)
        menv *= 0.25f + 0.75f * sat(v->age / a->matt);
    I1 = B.index * ks * menv;
    dI = (I1 - I) / (f32)n;
    amp1 = a->adec > 0.0f ? exp2fx(-v->age * a->adec * B.along * (0.5f + 0.5f / ks)) : 1.0f;
    damp = (amp1 - amp) / (f32)n;

    for (i = 0; i < n; i++) {
        f32 ma = tsin(pa + fb * za), mb = tsin(pb + fb * zb), mod, o;
        za = 0.5f * (za + ma);
        zb = 0.5f * (zb + mb);
        mod = (wa * ma + wb * mb) * I;
        switch (mode) {
        default: o = tsin(pc + mod); break;
        case 1: o = 0.5f * (tsin(pc + mod) + tsin(pd + mod)); break;
        case 2: o = 0.5f * tsin(pc + 0.3f * mod) + 0.32f * tsin(2.0f * pc + mod) + 0.2f * tsin(4.0f * pc + mod); break;
        case 3: o = 1.4f * tsin(pc + mod) * (0.35f + 0.65f * tsin(pd)); break;
        case 4:
            o = tsin(pc + mod + 0.22f * zo * (0.3f + I));
            zo = 0.5f * (zo + o);
            break;
        }
        out[i] = o * amp;
        I += dI;
        amp += damp;
        pc += dc; if (pc >= 1.0f) pc -= 1.0f;
        pa += da; if (pa >= 1.0f) pa -= 1.0f;
        pb += db; if (pb >= 1.0f) pb -= 1.0f;
        pd += dd; if (pd >= 1.0f) pd -= 1.0f;
    }
    v->ph[0] = pc; v->ph[1] = pa; v->ph[2] = pb; v->ph[3] = pd;
    v->s[0] = I; v->s[1] = za; v->s[2] = zb; v->s[3] = zo; v->s[4] = amp;
}

/* ---- picture ---------------------------------------------------------------------------- */
#define UN 120
#define NECHO 6
static const uint8_t LOBES[NALGO] = {7, 5, 4, 6, 12, 9, 11, 5};
static const uint8_t EYES[NALGO] = {1, 1, 1, 2, 3, 1, 2, 5};
static const f32 EYE_POS[6][5][2] = {
    {{0, 0}},
    {{0, 0}},
    {{-17, 0}, {17, 0}},
    {{0, -15}, {-15, 11}, {15, 11}},
    {{-14, -14}, {14, -14}, {-14, 14}, {14, 14}},
    {{0, 0}, {-24, -8}, {24, -8}, {-13, 20}, {13, 20}},
};
static const f32 EYE_R[6] = {0, 19, 13, 11, 10, 9};

static f32 rad[UN], cs_tab[UN][2];
static struct { f32 s, life, rot; } echo[NECHO];
static f32 kick, kickv, rot, rotv, bump[12], lev, look[2], lookv[2], blink, since, phase2;
static uint32_t seen;
static int cs_ready, spin_dir = 1;

static f32 spine(int algo, f32 th, f32 mlo, f32 mfr, f32 I, f32 t)
{
    /* RATIO: every step up adds two spines, the outline melting from one count to the next */
    f32 L = (f32)LOBES[algo] + 2.0f * (mlo - 1.0f);
    f32 w = L * th;
    f32 m0 = sin_turns(mlo * th + phase2), m1 = sin_turns((mlo + 1.0f) * th - phase2);
    f32 md = I * (m0 + (m1 - m0) * mfr);
    f32 s = sin_turns(w + md);
    if (mfr > 0.001f)
        s += (sin_turns(w + 2.0f * th + md) - s) * mfr;
    switch (algo) {
    default: return s;                                             /* KEYS: soft lobes */
    case 1: return 2.0f * absf(s) - 1.0f;                          /* BELL: scallops */
    case 2: return clampf(s * 2.6f, -1.0f, 1.0f);                  /* BASS: fat blocks */
    case 3: return s > 0.0f ? s * s * 1.3f : s * 0.3f;             /* BRASS: flares */
    case 4: return s * 0.7f;                                       /* ORGAN: fine teeth */
    case 5: s = maxf(s, 0.0f); s *= s; return s * s * 1.5f - 0.15f;   /* GLASS: needles */
    case 6: return 0.7f * (s + 0.5f * sin_turns(2.0f * w + 2.0f * I * m0) + 0.33f * sin_turns(3.0f * w));   /* METAL: a gear */
    case 7: return s * (0.4f + 0.9f * sin_turns(2.0f * th + t * 0.13f)) + 0.4f * sin_turns(3.0f * th - t * 0.21f);   /* CHAOS */
    }
}

static void outline(f32 scale, f32 turn, int stepv, f32 w, f32 a)
{
    f32 xy[2 * UN + 2], cd = cos_turns(turn), sd = sin_turns(turn);
    int i, n = 0;
    for (i = 0; i < UN; i += stepv) {
        f32 r = rad[i] * scale;
        xy[2 * n] = 120.0f + (cs_tab[i][0] * cd - cs_tab[i][1] * sd) * r;
        xy[2 * n + 1] = 120.0f + (cs_tab[i][1] * cd + cs_tab[i][0] * sd) * r;
        n++;
    }
    g_poly(xy, n, 1, w, a);
}

static void eyes(int count, f32 sc, const Vis *vis)
{
    f32 r = EYE_R[count] * sc, shut = blink < 0.0f ? 0.12f : 1.0f;
    int i;
    if (sc <= 0.02f)
        return;
    for (i = 0; i < count; i++) {
        f32 ex = 120.0f + EYE_POS[count][i][0] * sc, ey = 120.0f + EYE_POS[count][i][1] * sc;
        f32 lx = look[0], ly = look[1];
        if (count == 5 && i) {                   /* CHAOS: everybody looks somewhere else */
            lx = 0.7f * sin_turns(vis->t * 0.31f + (f32)i * 0.21f) + 0.3f * look[0];
            ly = 0.7f * sin_turns(vis->t * 0.23f + (f32)i * 0.37f) + 0.3f * look[1];
        }
        g_mode(G_PAINT);
        g_disc(ex, ey, r + 4.0f, 0.0f);
        g_ellipse(ex, ey, r, r * shut, 0.0f, 1.0f);
        if (shut > 0.5f) {
            f32 pr = r * (0.46f + 0.2f * lev);
            g_disc(ex + lx * (r - pr - 1.0f), ey + ly * (r - pr - 1.0f), pr, 0.0f);
            g_disc(ex + lx * (r - pr - 1.0f) - pr * 0.35f, ey + ly * (r - pr - 1.0f) - pr * 0.35f, pr * 0.28f, 1.0f);
        }
        g_mode(G_MAX);
    }
}

static SMALL void urchin_draw(const Vis *vis)          /* (built small: room for BLAB) */
{
    f32 dt = vis->dt, x = vis->k[0] * 4.999f, mlo, mfr, I, A, R0, fz = vis->k[2] * vis->k[2], tail = vis->k[3], mt;
    f32 lvl = 0.0f, tx = 0.0f, ty = 0.0f;
    int i, j, nest;

    if (!cs_ready) {
        for (i = 0; i < UN; i++) {
            cs_tab[i][0] = cos_turns((f32)i / (f32)UN);
            cs_tab[i][1] = sin_turns((f32)i / (f32)UN);
        }
        cs_ready = 1;
    }
    if (vis->fresh) {
        seen = vis->notes;
        kick = kickv = rotv = lev = 0.0f;
        since = 9.0f;
        for (i = 0; i < NECHO; i++)
            echo[i].life = 0.0f;
        for (i = 0; i < 12; i++)
            bump[i] = 0.0f;
    }

    /* the voices: each pushes the spine of its pitch class */
    {
        f32 want[12] = {0};
        for (i = 0; i < NVOICE; i++)
            if (vis->v[i].on) {
                int pc = ((int)(vis->v[i].pitch + 0.5f) % 12 + 12) % 12;
                f32 e = vis->v[i].env * (vis->v[i].gate ? 1.0f : 0.6f);
                if (e > want[pc]) want[pc] = e;
                if (vis->v[i].env > lvl) lvl = vis->v[i].env;
            }
        for (i = 0; i < 12; i++)
            bump[i] += (want[i] - bump[i]) * minf(1.0f, dt * (want[i] > bump[i] ? 40.0f : 9.0f));
    }
    lev += (minf(1.0f, vis->level * 3.0f) - lev) * minf(1.0f, dt * 14.0f);
    since += dt;

    if (vis->notes != seen) {                    /* a new note: pop, spin a little, send an echo out */
        int pc = ((int)(vis->last_pitch + 0.5f) % 12 + 12) % 12, best = 0;
        seen = vis->notes;
        since = 0.0f;
        kickv += 2.6f;
        spin_dir = -spin_dir;
        rotv += (f32)spin_dir * 0.1f;
        for (i = 1; i < NECHO; i++)
            if (echo[i].life < echo[best].life)
                best = i;
        echo[best].s = 1.0f;
        echo[best].life = 1.0f;
        echo[best].rot = 0.0f;
        tx = cos_turns((f32)pc / 12.0f - 0.25f);
        ty = sin_turns((f32)pc / 12.0f - 0.25f);
        lookv[0] += tx * 20.0f;
        lookv[1] += ty * 20.0f;
    }
    if (since < 1.6f) {
        int pc = ((int)(vis->last_pitch + 0.5f) % 12 + 12) % 12;
        tx = 0.9f * cos_turns((f32)pc / 12.0f - 0.25f);
        ty = 0.9f * sin_turns((f32)pc / 12.0f - 0.25f);
    } else {
        tx = 0.45f * sin_turns(vis->t * 0.11f);
        ty = 0.3f * sin_turns(vis->t * 0.17f + 0.2f);
    }
    spring_z(&look[0], &lookv[0], tx, 13.0f, 0.6f, dt);
    spring_z(&look[1], &lookv[1], ty, 13.0f, 0.6f, dt);
    spring_z(&kick, &kickv, 0.0f, 20.0f, 0.3f, dt);
    rotv -= rotv * minf(1.0f, dt * 2.0f);
    rot = fractf(rot + dt * (0.012f + rotv));
    phase2 = fractf(phase2 + dt * (0.05f + 0.25f * lev));
    blink -= dt;
    if (blink < -0.11f)
        blink = 1.4f + 3.0f * rng_f(vis->rng);

    /* the outline */
    mlo = (f32)((int)x + 1);
    mfr = smooth01(x - (f32)(int)x);
    I = 0.07f + vis->k[1] * (0.22f + 0.3f * lev);
    A = 7.0f + 17.0f * vis->k[1] + 20.0f * lev;
    R0 = (58.0f + 3.0f * sin_turns(vis->t * 0.31f)) * (1.0f + 0.16f * kick);
    mt = smooth01(vis->algo_t);
    for (i = 0; i < UN; i++) {
        f32 th = (f32)i / (f32)UN, p = spine(vis->algo, th, mlo, mfr, I, vis->t), r;
        if (mt < 1.0f)
            p = lerpf(spine(vis->algo_prev, th, mlo, mfr, I, vis->t), p, mt);
        r = R0 + A * p;
        if (fz > 0.001f)
            r += fz * (5.0f + 9.0f * lev) * sin_turns(31.0f * th + vis->t * 3.3f) * sin_turns(17.0f * th - vis->t * 2.1f)
                 + fz * 3.0f * sin_turns(53.0f * th + vis->t * 7.0f);
        rad[i] = r;
    }
    /* the pitch spines stay put on the screen while the body turns under them */
    for (j = 0; j < 12; j++)
        if (bump[j] > 0.01f) {
            f32 cf = fractf((f32)j / 12.0f - 0.25f - rot + 2.0f) * (f32)UN;
            int c = (int)cf;
            for (i = -8; i <= 9; i++) {
                f32 u = 1.0f - absf((f32)(c + i) - cf) / 8.0f;
                if (u > 0.0f)
                    rad[((c + i) % UN + UN) % UN] += bump[j] * 34.0f * u * u * u;
            }
        }

    /* echoes travelling out to the bezel and past it */
    for (i = 0; i < NECHO; i++) {
        if (echo[i].life <= 0.0f)
            continue;
        echo[i].s += dt * (0.55f + 0.5f * echo[i].s);
        echo[i].rot += dt * 0.03f;
        echo[i].life -= dt / (0.3f + 2.4f * tail);
        if (echo[i].s > 3.2f)
            echo[i].life = 0.0f;
        if (echo[i].life > 0.0f)
            outline(echo[i].s, rot - echo[i].rot, 1, 2.0f, 0.75f * echo[i].life * sqrtfx(echo[i].life));
    }
    /* TAIL: copies nested inside, each a little behind the last */
    nest = (int)(tail * 4.0f) + 1;
    for (j = 1; j <= nest && j <= 4; j++) {
        f32 a = sat(tail * 4.0f - (f32)(j - 1)) * (0.55f - 0.1f * (f32)j);
        if (a > 0.02f)
            outline(1.0f - 0.14f * (f32)j, rot - 0.012f * (f32)j * (1.0f + 2.0f * sin_turns(vis->t * 0.09f)), 2, 1.6f, a);
    }
    outline(1.0f, rot, 1, 3.0f, 1.0f);

    if (mt < 0.5f)
        eyes(EYES[vis->algo_prev], 1.0f - 2.0f * mt + 0.0f * lvl, vis);
    else
        eyes(EYES[vis->algo], ease_out(2.0f * mt - 1.0f) * (1.0f + 0.1f * kick), vis);
}

const InstDef inst_urchin = {
    "URCHIN",
    {255, 96, 64},
    {"RATIO", "DEPTH", "FUZZ", "TAIL"},
    {"KEYS", "BELL", "BASS", "BRASS", "ORGAN", "GLASS", "METAL", "CHAOS"},
    {{20, 45, 8, 45}, 0, {0, 40, 60, 30}, {0, 55, 0, 0}, {18, 3, 20, 50}, {0, 3, 1, 60}, {0, 0, 0}},
    0.26f,
    urchin_block, urchin_note, urchin_render, 0, urchin_draw,
};
