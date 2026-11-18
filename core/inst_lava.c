/* H1 - LAVA: the bass. One oscillator pushed through a wavefolder (drive it harder and the wave
 * doubles back on itself, again and again) and then a low-pass filter. The picture is a lava lamp
 * drawn as a map: every note is a blob that rises out of the pool at the bottom, and the screen
 * shows the contour lines of the lot of them, merging and splitting as they pass.
 *
 *   K1 FOLD    how many times the wave folds           how many contour lines
 *   K2 HEAT    the filter opens                        it boils: the lines squirm, the blobs move faster
 *   K3 LEAN    the fold goes lopsided                  the whole lamp leans
 *   K4 SUB     a sine an octave down                   a big slow blob in the deep
 */
#include "internal.h"
#include "dsp.h"

/* ---- sound ------------------------------------------------------------------------------ */
static struct {
    int algo;
    f32 fold, cut, lean, sub, wob;
} V;
static f32 wob_ph;

static void lava_block(const Blk *b)
{
    V.algo = b->algo;
    V.fold = 0.8f + b->k[0] * b->k[0] * 9.0f;
    V.cut = 90.0f * exp2fx(b->k[1] * 6.6f);
    V.lean = b->k[2];
    V.sub = b->k[3];
    wob_ph = fractf(wob_ph + b->bpm * (1.0f / 60.0f) * (f32)CTRL * b->isr);      /* QUAKE wobbles in time */
    V.wob = 0.5f - 0.5f * tsin(wob_ph + 0.25f);
}

static void lava_note(Voice *v, const Blk *b)
{
    (void)b;
    if (!v->u[0]) v->u[0] = 0x1A7A1A7Au + v->idx * 2221u;
    if (v->env < 0.001f) {
        v->s[0] = v->s[1] = v->s[2] = v->s[3] = 0.0f;
        v->ph[0] = 0.0f;
        v->ph[1] = 0.25f;
        v->ph[2] = v->r1;
    }
}

static inline f32 fold_sin(f32 x) { return tsin(x * 0.25f); }
static inline f32 fold_tri(f32 x) { return 4.0f * absf(fractf(x * 0.25f + 0.25f) - 0.5f) - 1.0f; }

static void lava_render(Voice *v, const Blk *b, f32 *out, int n)
{
    Svf f;
    int algo = V.algo, i;
    f32 pitch = v->pitch + b->bend - 12.0f - (algo == 7 ? 12.0f : 0.0f);
    f32 d = minf(mtof(pitch) * b->isr, 0.3f), d2 = d * 1.0065f, dsub = d * 0.5f, g = V.fold, bias = V.lean * 0.9f;
    f32 q = 0.8f, oct = 2.6f * exp2fx(-v->age * 6.0f) + (pitch - 36.0f) * (1.0f / 24.0f), sub = V.sub * 0.9f, pa = v->ph[0], pb = v->ph[1], pc = v->ph[2];
    f32 gdyn = g * (0.55f + 0.45f * exp2fx(-v->age * 2.5f)), hp = 0.0f, norm;

    switch (algo) {
    case 1: q = 5.5f; oct = 3.6f * exp2fx(-v->age * 9.0f); gdyn *= 0.6f; break;          /* ACID */
    case 3: q = 2.2f; oct += 0.9f * tsin(v->age * 2.7f + v->r1); break;                  /* GROWL */
    case 4: q = 1.4f; break;                                                             /* REED */
    case 6: hp = 0.5f; gdyn *= 1.5f; oct += 1.0f; break;                                 /* SHRED */
    case 7: q = 2.6f; oct = -1.0f + 3.4f * V.wob; break;                                 /* QUAKE */
    default: break;
    }
    svf_set(&f, V.cut * exp2fx(oct), q, b->isr);
    norm = 0.75f / (1.0f + 0.25f * absf(bias));

    for (i = 0; i < n; i++) {
        f32 x, lo, bp, s = tsin(pc);
        switch (algo) {
        default: x = fold_sin(tsin(pa) * gdyn + bias); break;                            /* MOLTEN: a sine, folded */
        case 1: x = fold_sin(saw_bl(pa, d) * gdyn + bias); break;                        /* ACID: a saw, squelched */
        case 2: x = fold_tri(pulse_bl(pa, d, 0.5f - 0.38f * V.lean) * 0.6f * gdyn + tsin(pa) * gdyn * 0.5f); break;   /* SQUARE */
        case 3: x = fold_sin((saw_bl(pa, d) + saw_bl(pb, d2)) * 0.5f * gdyn + bias); break;                           /* GROWL */
        case 4: x = fold_tri((4.0f * absf(pa - 0.5f) - 1.0f) * gdyn + bias); break;      /* REED: a triangle, folded square */
        case 5: x = fold_sin(tsin(pa + 0.35f * V.fold * 0.2f * s) * gdyn * 0.8f + bias); break;                       /* HOLLOW */
        case 6: x = fold_tri(fold_sin(saw_bl(pa, d) * gdyn + bias) * 2.2f); break;       /* SHRED: folded twice */
        case 7: x = fold_sin(tsin(pa) * gdyn + bias) + 0.5f * s; break;                  /* QUAKE */
        }
        SVF_TICK(f, &v->s[0], x, lo, bp);
        x = lo - hp * (lo - (x - lo - bp * f.k)) * 0.5f;
        v->s[2] += (x - v->s[2]) * 0.0006f;                                              /* the lean leaves a DC offset: take it out */
        out[i] = (x - v->s[2]) * norm + sub * s;
        pa += d; if (pa >= 1.0f) pa -= 1.0f;
        pb += d2; if (pb >= 1.0f) pb -= 1.0f;
        pc += dsub; if (pc >= 1.0f) pc -= 1.0f;
    }
    v->ph[0] = pa; v->ph[1] = pb; v->ph[2] = pc;
}

/* ---- picture ---------------------------------------------------------------------------- */
#define CELL 6
#define GN (240 / CELL + 1)
#define NRES 4
#define NBLOB (NVOICE + NRES + 1)
static f32 field[GN * GN] H1_POOL;
static struct { f32 x, y, vy, r, s, pop, popv; } bl[NBLOB];
static uint32_t seen_s[NVOICE];
static f32 boil, lev_s, lean_s;

static f32 key_x(f32 pitch) { return 26.0f + clampf((pitch - 41.0f) / 50.0f, 0.0f, 1.0f) * 188.0f; }

/* how near a point is to a blob, by the algorithm's idea of distance; 1 at one radius out */
static inline f32 nearness(int algo, f32 dx, f32 dy, f32 ir)
{
    f32 d2;
    dx *= ir; dy *= ir;
    switch (algo) {
    default: d2 = dx * dx + dy * dy; break;
    case 1: if (dy > 0.0f) dy *= 0.5f; d2 = dx * dx * 1.3f + dy * dy; break;                 /* ACID: they drip */
    case 2: dx *= dx; dy *= dy; d2 = sqrtfx(dx * dx + dy * dy); break;                       /* SQUARE */
    case 3: d2 = absf(dx) + absf(dy); d2 *= d2 * 0.62f; break;                               /* GROWL: diamonds */
    case 4: d2 = dx * dx * 0.35f + dy * dy * 3.2f; break;                                    /* REED: flat, like strata */
    case 5: d2 = sqrtfx(dx * dx + dy * dy) - 0.85f; d2 = d2 * d2 * 7.0f + 0.25f; break;      /* HOLLOW: rings */
    case 6: d2 = sqrtfx(absf(dx)) + sqrtfx(absf(dy)); d2 = d2 * d2; d2 = d2 * d2 * 0.14f; break;   /* SHRED: four-pointed */
    }
    return 1.0f / (d2 + 0.15f);
}

static void contour(f32 level, f32 w, f32 a)
{
    int i, j;
    for (j = 0; j < GN - 1; j++) {
        const f32 *r0 = &field[j * GN], *r1 = r0 + GN;
        for (i = 0; i < GN - 1; i++) {
            f32 fa = r0[i] - level, fb = r0[i + 1] - level, fc = r1[i + 1] - level, fd = r1[i] - level;
            int m = (fa > 0.0f) | ((fb > 0.0f) << 1) | ((fc > 0.0f) << 2) | ((fd > 0.0f) << 3);
            f32 x0 = (f32)(i * CELL), y0 = (f32)(j * CELL), ex[4], ey[4];
            if (m == 0 || m == 15)
                continue;
            /* where the line crosses each edge: top, right, bottom, left */
            ex[0] = x0 + (f32)CELL * fa / (fa - fb); ey[0] = y0;
            ex[1] = x0 + (f32)CELL; ey[1] = y0 + (f32)CELL * fb / (fb - fc);
            ex[2] = x0 + (f32)CELL * fd / (fd - fc); ey[2] = y0 + (f32)CELL;
            ex[3] = x0; ey[3] = y0 + (f32)CELL * fa / (fa - fd);
#define SEG(p, q) g_line(ex[p], ey[p], ex[q], ey[q], w, a)
            switch (m) {
            case 1: case 14: SEG(3, 0); break;
            case 2: case 13: SEG(0, 1); break;
            case 3: case 12: SEG(3, 1); break;
            case 4: case 11: SEG(1, 2); break;
            case 6: case 9: SEG(0, 2); break;
            case 7: case 8: SEG(3, 2); break;
            case 5: SEG(3, 0); SEG(1, 2); break;
            default: SEG(0, 1); SEG(3, 2); break;
            }
#undef SEG
        }
    }
}

static void lava_draw(const Vis *vis)
{
    f32 dt = vis->dt, t = vis->t, fold = vis->k[0], heat = vis->k[1], lean = (vis->k[2] - 0.0f), sub = vis->k[3];
    f32 speed = 0.35f + 1.6f * heat, nlev = 2.0f + fold * 6.0f, mt = smooth01(vis->algo_t), pool = 2.6f, strata = 0.0f;
    int i, j, b, algo = mt < 0.5f ? vis->algo_prev : vis->algo, nb = 0, act[NBLOB];
    f32 ax[NBLOB], ay[NBLOB], air[NBLOB], as[NBLOB], dim = 0.2f + 0.8f * absf(2.0f * mt - 1.0f);

    if (vis->fresh) {
        for (i = 0; i < NBLOB; i++) {
            bl[i].x = 30.0f + (f32)((i * 67) % 180);
            bl[i].y = 228.0f;
            bl[i].vy = bl[i].s = bl[i].pop = bl[i].popv = 0.0f;
            bl[i].r = 22.0f;
        }
        for (i = 0; i < NVOICE; i++)
            seen_s[i] = vis->v[i].serial;
        lev_s = 0.0f;
        lean_s = lean;
    }
    lev_s += (minf(1.0f, vis->level * 3.0f) - lev_s) * minf(1.0f, dt * 6.0f);
    lean_s += (lean - lean_s) * minf(1.0f, dt * 6.0f);
    boil += dt * speed;

    /* the notes: each rises to its own height, higher notes higher, and sinks when it is let go */
    for (i = 0; i < NVOICE; i++) {
        const VVoice *v = &vis->v[i];
        f32 want = 0.0f, ty = 232.0f;
        if (v->serial != seen_s[i]) {
            seen_s[i] = v->serial;
            bl[i].popv += 5.0f;
            if (bl[i].s < 0.05f) {
                bl[i].y = 222.0f;
                bl[i].vy = 0.0f;
            }
        }
        if (v->on) {
            want = v->env;
            bl[i].x += (key_x(v->pitch) - bl[i].x) * minf(1.0f, dt * 7.0f);
            if (v->gate)
                ty = clampf(176.0f - (v->pitch - 36.0f) * 2.3f, 44.0f, 176.0f) + 9.0f * sin_turns(boil * 0.13f + (f32)i * 0.27f);
        }
        spring(&bl[i].y, &bl[i].vy, ty, 1.6f + 2.4f * heat, dt);
        spring_z(&bl[i].pop, &bl[i].popv, 0.0f, 16.0f, 0.3f, dt);
        bl[i].s += (want - bl[i].s) * minf(1.0f, dt * (want > bl[i].s ? 9.0f : 3.0f));
        bl[i].r = 21.0f * (0.75f + 0.25f * bl[i].s) * (1.0f + 0.3f * bl[i].pop);
    }
    /* the lamp's own blobs, always going up and down, so it is never still */
    for (i = 0; i < NRES; i++) {
        b = NVOICE + i;
        bl[b].x = 40.0f + (f32)i * 54.0f + 22.0f * sin_turns(boil * 0.031f + (f32)i * 0.4f);
        bl[b].y = 150.0f + 76.0f * sin_turns(boil * (0.021f + 0.006f * (f32)i) + (f32)i * 0.29f);
        bl[b].s = 0.55f;
        bl[b].r = 13.0f + 4.0f * (f32)(i & 1);
    }
    b = NVOICE + NRES;                           /* SUB: the big one in the deep */
    bl[b].x = 120.0f + 40.0f * sin_turns(boil * 0.017f);
    bl[b].y = 196.0f - 22.0f * sub + 8.0f * sin_turns(boil * 0.05f);
    bl[b].s = sub * (0.6f + 0.6f * lev_s);
    bl[b].r = 30.0f + 34.0f * sub;

    for (i = 0; i < NBLOB; i++)
        if (bl[i].s > 0.02f) {
            act[nb] = i;
            ax[nb] = bl[i].x; ay[nb] = bl[i].y; air[nb] = 1.0f / bl[i].r; as[nb] = bl[i].s;
            nb++;
        }
    if (algo == 7) {                             /* QUAKE: the whole screen is ground, in layers */
        strata = 1.0f;
        pool = 0.0f;
    }

    /* the field */
    for (j = 0; j < GN; j++) {
        f32 y = (f32)(j * CELL), py = pool * smooth01((y - 196.0f) * (1.0f / 40.0f)), shear = (lean_s - 0.0f) * (y - 150.0f) * 0.55f;
        f32 *row = &field[j * GN];
        for (i = 0; i < GN; i++) {
            f32 x = (f32)(i * CELL) - shear, fsum = py;
            for (b = 0; b < nb; b++) {
                f32 dx = x - ax[b], dy = y - ay[b];
                if (absf(dx) < 110.0f && absf(dy) < 110.0f)
                    fsum += as[b] * nearness(algo, dx, dy, air[b]);
            }
            if (strata > 0.0f)
                fsum = fsum * 0.8f + (y + 14.0f * sin_turns(x * 0.004f + boil * 0.02f)) * (1.0f / 40.0f);
            if (heat > 0.02f)                    /* HEAT: it boils */
                fsum *= 1.0f + heat * 0.3f * sin_turns(x * 0.013f + boil * 0.16f) * sin_turns(y * 0.016f - boil * 0.13f);
            row[i] = fsum;
        }
    }

    /* FOLD many contour lines, each higher one brighter */
    for (i = 0; i < 8; i++) {
        f32 a = sat(nlev - (f32)i), level, u = (f32)i / 7.0f;
        if (a <= 0.01f)
            break;
        if (strata > 0.0f)
            level = 0.6f + (f32)i * (6.0f / nlev) * 0.86f;
        else
            level = 0.5f * exp2fx((f32)i * 0.52f);
        contour(level, 1.3f + 0.9f * u, (0.62f + 0.38f * u) * a * dim);
        if (strata > 0.0f && nlev < 8.0f)        /* (the layers fill the screen however few there are) */
            contour(level + 3.0f / nlev, 1.2f, 0.4f * a * dim);
    }
    /* an ember at the heart of every note */
    for (i = 0; i < NVOICE; i++)
        if (bl[i].s > 0.05f)
            g_disc(bl[i].x + lean_s * (bl[i].y - 150.0f) * 0.55f, bl[i].y, 1.5f + 3.0f * bl[i].s + 2.0f * maxf(0.0f, bl[i].pop), sat(bl[i].s * 2.0f) * dim);
    (void)t; (void)act;
}

const InstDef inst_lava = {
    "LAVA",
    {255, 72, 168},
    {"FOLD", "HEAT", "LEAN", "SUB"},
    {"MOLTEN", "ACID", "SQUARE", "GROWL", "REED", "HOLLOW", "SHRED", "QUAKE"},
    {{35, 50, 0, 30}, 0, {0, 55, 80, 30}, {0, 55, 0, 0}, {0, 3, 10, 50}, {0, 3, 1, 60}, {0, 0, 0}},
    0.3f,
    lava_block, lava_note, lava_render, 0, lava_draw,
};
