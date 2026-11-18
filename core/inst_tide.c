/* H1 - TIDE: the pad. Detuned saws through a slow filter, and a night sea under whatever the
 * algorithm hangs in the sky. The keyboard lies across the water: every note lifts a swell at its
 * own place and lights a star above it.
 *
 *   K1 GLOW    filter cutoff                           the moon climbs and fills
 *   K2 HALO    resonance                               rings around it
 *   K3 DRIFT   detune and wander                       the sea gets choppy
 *   K4 SURGE   slow swells of filter and level         the whole sea heaves
 */
#include "internal.h"
#include "dsp.h"

/* ---- sound ------------------------------------------------------------------------------ */
static struct {
    int algo;
    f32 cut, q, det, drift, surge;
} D;

static void tide_block(const Blk *b)
{
    D.algo = b->algo;
    D.cut = 70.0f * exp2fx(b->k[0] * 7.3f);
    D.q = 0.65f + b->k[1] * b->k[1] * 9.0f;
    D.det = 0.03f + 0.42f * b->k[2] * b->k[2];           /* semitones */
    D.drift = b->k[2];
    D.surge = b->k[3];
}

static void tide_note(Voice *v, const Blk *b)
{
    (void)b;
    if (!v->u[0]) v->u[0] = 0x51ED270Bu + v->idx * 3571u;
    if (v->env < 0.001f) {
        v->s[0] = v->s[1] = 0.0f;
        v->ph[4] = v->r1;
        v->ph[5] = v->r2;
    }
}

static void tide_render(Voice *v, const Blk *b, f32 *out, int n)
{
    Svf f;
    f32 pitch = v->pitch + b->bend, det = D.det, cut, oct = 0.0f, l1, l2, amp, bpmix = 0.0f, nz = 0.0f, sub = 0.0f, hi = 0.0f, duty = 0.5f;
    f32 da, db, dc, dd, pa = v->ph[0], pb = v->ph[1], pc = v->ph[2], pd = v->ph[3], q = D.q, shim = 0.0f;
    int i, algo = D.algo;

    /* each voice wanders a little by itself, and breathes on its own slow clock */
    v->s[3] += (rng_s(&v->u[0]) - v->s[3]) * 0.004f;
    v->s[2] += (v->s[3] - v->s[2]) * 0.01f;
    pitch += v->s[2] * D.drift * 2.2f;
    v->ph[4] = fractf(v->ph[4] + (0.07f + 0.5f * D.surge * D.surge) * (0.8f + 0.4f * v->r2) * (f32)n * b->isr);
    v->ph[5] = fractf(v->ph[5] + 0.31f * (f32)n * b->isr);
    l1 = tsin(v->ph[4]);
    l2 = tsin(v->ph[5]);
    oct = D.surge * 1.8f * l1 - 1.3f * exp2fx(-v->age * 1.6f);       /* the filter blooms open after the note starts */
    amp = 1.0f - 0.4f * D.surge * (0.5f - 0.5f * tsin(v->ph[4] + 0.2f));

    switch (algo) {
    default: sub = 0.5f; break;                                       /* MOON */
    case 1: hi = 0.3f; oct += 1.0f; break;                            /* SUN */
    case 2: bpmix = 0.6f; duty = 0.5f + 0.38f * l2; break;            /* ECLIPSE */
    case 3: shim = sat(v->age * 0.6f) * (0.5f + 0.5f * l2); oct += 0.5f * l2; break;   /* AURORA */
    case 4: nz = 0.3f; det *= 2.5f; q += 1.5f; v->s[5] += (rng_s(&v->u[0]) - v->s[5]) * 0.02f; oct += v->s[5] * 2.0f; break;   /* STORM */
    case 5: nz = 0.9f; bpmix = 0.7f; sub = 0.45f; oct += (pitch - 60.0f) * (1.0f / 14.0f); break;   /* FOG */
    case 6: pitch += 12.0f * exp2fx(-v->age * 8.0f); oct += 1.3f * exp2fx(-v->age * 1.6f) + 3.2f * exp2fx(-v->age * 2.6f); break;   /* COMET */
    case 7: pitch -= 12.0f; sub = 0.9f; oct -= 0.8f; break;           /* ABYSS */
    }
    if (algo == 5) amp *= 2.2f;
    cut = D.cut * exp2fx(oct + (pitch - 60.0f) * (1.0f / 30.0f));
    v->s[4] += (cut - v->s[4]) * 0.25f;
    svf_set(&f, v->s[4], q, b->isr);

    da = minf(mtof(pitch) * b->isr, 0.45f);
    db = minf(da * exp2fx(det * (1.0f / 12.0f)), 0.45f);
    dc = da * exp2fx(-det * 0.8f * (1.0f / 12.0f));
    dd = sub > 0.0f ? da * 0.5f : (shim > 0.0f ? minf(da * 3.0f, 0.45f) : minf(da * 2.0f, 0.45f));

    for (i = 0; i < n; i++) {
        f32 x, lo, bp;
        switch (algo) {
        case 2: x = 0.5f * (pulse_bl(pa, da, duty) + pulse_bl(pb, db, 1.0f - duty * 0.9f)); break;
        case 5: x = 0.12f * saw_bl(pa, da); break;
        default: x = 0.36f * (saw_bl(pa, da) + saw_bl(pb, db) + saw_bl(pc, dc)); break;
        }
        if (sub > 0.0f) x += sub * tsin(pd);
        else if (hi > 0.0f) x += hi * saw_bl(pd, dd);
        if (nz > 0.0f) x += nz * noise_s(&v->u[0]);
        SVF_TICK(f, &v->s[0], x, lo, bp);
        x = lo + (bp - lo) * bpmix;
        if (shim > 0.0f) x += 0.16f * shim * (tsin(pd) + 0.6f * tsin(pd * 2.0f));
        out[i] = x * amp;
        pa += da; if (pa >= 1.0f) pa -= 1.0f;
        pb += db; if (pb >= 1.0f) pb -= 1.0f;
        pc += dc; if (pc >= 1.0f) pc -= 1.0f;
        pd += dd; if (pd >= 1.0f) pd -= 1.0f;
    }
    v->ph[0] = pa; v->ph[1] = pb; v->ph[2] = pc; v->ph[3] = pd;
}

/* ---- picture ---------------------------------------------------------------------------- */
#define NLINE 9
#define NPT 44
#define NBG 12
static f32 swell[NVOICE], swx[NVOICE], lev, sea_ph[3], bolt_t, bolt_x, comet_u, comet_v, eye_x, eye_xv, eye_o, hz_y = 128.0f, hz_v;
static uint32_t seen, bolt_seed;
static f32 bg[NBG][2];
static int bg_ready;

static f32 key_x(f32 pitch) { return 16.0f + clampf((pitch - 41.0f) / 50.0f, 0.0f, 1.0f) * 208.0f; }

static void star(f32 x, f32 y, f32 s, f32 a)
{
    g_line(x - s, y, x + s, y, 1.6f, a);
    g_line(x, y - s, x, y + s, 1.6f, a);
    g_disc(x, y, s * 0.36f, a);
}

static void sky(int algo, f32 a, const Vis *vis, f32 mx, f32 my, f32 H)
{
    f32 glow = vis->k[0], halo = vis->k[1], t = vis->t, r = 29.0f + 4.0f * lev;
    int i, j;
    if (a <= 0.01f)
        return;
    /* HALO, for anything round */
    if (algo != 3 && algo != 4 && algo != 6)
        for (j = 0; j < 3; j++) {
            f32 ha = halo * (1.0f - 0.27f * (f32)j) * (0.45f + 0.55f * sat(lev * 2.0f + 0.3f * sin_turns(t * 0.4f + (f32)j * 0.2f))) * a;
            f32 rr = (algo == 7 ? 15.0f : r) + 9.0f + (f32)j * (9.0f + 5.0f * halo) + 2.0f * sin_turns(t * 0.7f - (f32)j * 0.17f);
            if (ha > 0.02f)
                g_ring(mx, my, rr, 1.6f, ha);
        }
    switch (algo) {
    default:                                     /* MOON: GLOW is its phase too */
        g_disc(mx, my, r, a);
        g_mode(G_PAINT);
        g_disc(mx - r * (0.42f + 1.7f * glow), my - r * 0.18f * (1.0f - glow), r * 1.02f, 0.0f);
        g_mode(G_MAX);
        break;
    case 1: {                                    /* SUN */
        f32 rs = r * 0.86f;
        g_disc(mx, my, rs, a);
        for (i = 0; i < 12; i++) {
            f32 an = (f32)i / 12.0f + t * 0.02f, c = cos_turns(an), s = sin_turns(an);
            f32 l0 = rs + 6.0f, l1 = l0 + 5.0f + 9.0f * glow + 16.0f * lev * (0.6f + 0.4f * sin_turns(t * 0.9f + (f32)i * 0.31f));
            g_line(mx + c * l0, my + s * l0, mx + c * l1, my + s * l1, 2.6f, a);
        }
        break;
    }
    case 2:                                      /* ECLIPSE: a hole with a corona */
        for (i = 0; i < 28; i++) {
            f32 an = (f32)i / 28.0f + t * 0.006f, c = cos_turns(an), s = sin_turns(an);
            f32 l1 = r + 4.0f + (6.0f + 22.0f * glow) * (0.45f + 0.55f * sin_turns(t * 0.23f + (f32)i * 0.618f)) + 14.0f * lev;
            g_line(mx + c * r, my + s * r, mx + c * l1, my + s * l1, 1.4f, 0.8f * a);
        }
        g_mode(G_PAINT);
        g_disc(mx, my, r, 0.0f);
        g_mode(G_MAX);
        g_ring(mx, my, r, 2.2f, a);
        g_disc(mx + r * cos_turns(0.62f - glow * 0.5f), my + r * sin_turns(0.62f - glow * 0.5f), 3.0f + 3.0f * glow, a);
        break;
    case 3:                                      /* AURORA: curtains */
        for (i = 0; i < 44; i++) {
            f32 x = -6.0f + (f32)i * 5.8f, w1 = sin_turns(x * 0.006f + t * 0.05f), w2 = sin_turns(x * 0.017f - t * 0.08f);
            f32 top = 26.0f + 16.0f * w1 + 8.0f * w2, len = (18.0f + 62.0f * glow) * (0.35f + 0.65f * (0.5f + 0.5f * sin_turns(x * 0.011f + t * 0.11f + w2 * 0.2f))) + 30.0f * lev;
            f32 al = (0.3f + 0.7f * (0.5f + 0.5f * sin_turns(x * 0.023f - t * 0.13f))) * a;
            g_line(x + 5.0f * w2, top, x + 9.0f * w2, minf(top + len, H - 4.0f), 2.0f, al);
        }
        break;
    case 4: {                                    /* STORM: a cloud, rain, and lightning where you play */
        f32 cy = 44.0f - 14.0f * glow, drift = 8.0f * sin_turns(t * 0.05f);
        static const f32 PUFF[6][3] = {{-52, 6, 20}, {-24, -8, 27}, {8, -12, 30}, {40, -4, 24}, {64, 8, 17}, {4, 10, 24}};
        for (i = 0; i < 14; i++) {
            f32 u = fractf((f32)i * 0.381f + t * (0.5f + 0.06f * (f32)(i % 5))), rx = 30.0f + fractf((f32)i * 0.618f) * 190.0f + drift;
            f32 ry = cy + 20.0f + u * (H - cy - 22.0f);
            g_line(rx - u * 14.0f, ry, rx - u * 14.0f - 3.0f, ry + 9.0f, 1.5f, 0.6f * a * sat(u * 6.0f) * sat((1.0f - u) * 4.0f));
        }
        if (bolt_t > 0.0f) {
            f32 xy[2 * 7], bx = bolt_x;
            uint32_t s = bolt_seed;
            for (i = 0; i < 7; i++) {
                xy[2 * i] = bx + (i == 0 || i == 6 ? 0.0f : rng_s(&s) * 15.0f);
                xy[2 * i + 1] = cy + 16.0f + (H - cy - 16.0f) * (f32)i / 6.0f;
            }
            g_poly(xy, 7, 0, 2.6f, sat(bolt_t * 5.0f) * a);
        }
        for (i = 0; i < 6; i++)
            g_disc(mx - 14.0f + PUFF[i][0] + drift, cy + PUFF[i][1], PUFF[i][2], (0.55f + 0.45f * sat(glow + bolt_t * 2.0f)) * a);
        break;
    }
    case 5:                                      /* FOG: a moon behind slow bands */
        g_disc(mx, my, r * 1.08f, (0.35f + 0.6f * glow) * a);
        g_mode(G_PAINT);
        for (i = 0; i < 5; i++) {
            f32 by = my - r + fractf((f32)i * 0.2f + t * 0.012f) * 2.0f * r;
            g_rect(mx - r - 6.0f, by - 2.5f, 2.0f * r + 12.0f, 4.5f + 2.0f * sin_turns(t * 0.1f + (f32)i * 0.3f), 0.0f);
        }
        g_mode(G_MAX);
        for (i = 0; i < 7; i++) {
            f32 fy = 22.0f + (f32)i * 15.0f + 4.0f * sin_turns(t * 0.03f + (f32)i * 0.4f), fx = fractf((f32)i * 0.37f + t * 0.008f * (f32)(1 + i % 3)) * 380.0f - 110.0f;
            g_line(fx, fy, fx + 70.0f + 14.0f * (f32)(i % 4), fy, 2.2f, 0.4f * a);
        }
        break;
    case 6: {                                    /* COMET: each note throws it further round */
        f32 an = 0.5f + comet_u * 0.5f, cx = 120.0f + 150.0f * cos_turns(an), cy = H + 10.0f + (118.0f + 0.0f * glow) * sin_turns(an);
        f32 tx = -sin_turns(an), ty = cos_turns(an), tl = 26.0f + 80.0f * glow + 60.0f * lev;
        for (i = -2; i <= 2; i++) {
            f32 sp = (f32)i * 0.11f, ex = cx - (tx + ty * sp) * tl * (1.0f - 0.16f * absf((f32)i)), ey = cy - (ty - tx * sp) * tl * (1.0f - 0.16f * absf((f32)i));
            g_line(cx, cy, ex, ey, i == 0 ? 2.6f : 1.5f, (i == 0 ? 1.0f : 0.55f) * a);
        }
        g_disc(cx, cy, 6.5f + 2.5f * lev, a);
        break;
    }
    case 7:                                      /* ABYSS: a small moon far up; the rest is underwater */
        g_disc(mx, my, 15.0f, a);
        break;
    }
}

static void tide_draw(const Vis *vis)
{
    f32 dt = vis->dt, t = vis->t, glow = vis->k[0], drift = vis->k[2], surge = vis->k[3], mt = smooth01(vis->algo_t);
    f32 H, mx = 152.0f, my, big, want[NVOICE] = {0};
    int i, j, k;

    if (!bg_ready) {
        uint32_t s = 0xC0FFEE11u;
        for (i = 0; i < NBG; i++) {
            bg[i][0] = rng_f(&s) * 240.0f;
            bg[i][1] = rng_f(&s);
        }
        bg_ready = 1;
    }
    if (vis->fresh) {
        seen = vis->notes;
        lev = 0.0f;
        bolt_t = 0.0f;
        hz_y = vis->algo == 7 ? 56.0f : 128.0f;
        hz_v = 0.0f;
        for (i = 0; i < NVOICE; i++)
            swell[i] = 0.0f;
    }
    lev += (minf(1.0f, vis->level * 3.5f) - lev) * minf(1.0f, dt * 6.0f);
    spring(&hz_y, &hz_v, vis->algo == 7 ? 56.0f : 128.0f, 7.0f, dt);
    H = hz_y;
    my = vis->algo == 7 ? H - 22.0f - glow * 14.0f : H + 16.0f - glow * 98.0f;
    if (vis->notes != seen) {
        seen = vis->notes;
        bolt_t = 0.3f;
        bolt_x = key_x(vis->last_pitch);
        bolt_seed = rng_next(vis->rng) | 1u;
        comet_v += 0.22f;
    }
    bolt_t = maxf(0.0f, bolt_t - dt);
    comet_v -= comet_v * minf(1.0f, dt * 2.5f);
    comet_u = fractf(comet_u + dt * (0.012f + comet_v));
    for (i = 0; i < NVOICE; i++) {
        if (vis->v[i].on) {
            want[i] = vis->v[i].env;
            swx[i] = key_x(vis->v[i].pitch);
        }
        swell[i] += (want[i] - swell[i]) * minf(1.0f, dt * (want[i] > swell[i] ? 5.0f : 2.2f));
    }
    for (i = 0; i < 3; i++)
        sea_ph[i] = fractf(sea_ph[i] + dt * (0.05f + 0.07f * (f32)i) * (1.0f + 1.5f * surge));

    /* the sky */
    for (i = 0; i < NBG; i++) {
        f32 y = 8.0f + bg[i][1] * (H - 30.0f), tw = 0.5f + 0.5f * sin_turns(t * (0.2f + 0.05f * (f32)(i % 4)) + bg[i][1] * 3.0f);
        if (y < H - 8.0f)
            g_disc(bg[i][0], y, 1.1f, 0.2f + 0.3f * tw);
    }
    if (mt < 1.0f)
        sky(vis->algo_prev, 1.0f - mt, vis, mx, my, H);
    sky(vis->algo, mt, vis, mx, my, H);
    for (i = 0; i < NVOICE; i++)                 /* a star for every sounding note, above its swell */
        if (swell[i] > 0.02f) {
            f32 sy = 14.0f + fractf((f32)i * 0.618f + 0.1f) * (H - 50.0f);
            star(swx[i], sy, (3.0f + 6.0f * swell[i]) * (0.85f + 0.15f * sin_turns(t * 1.3f + (f32)i * 0.2f)), sat(swell[i] * 3.0f));
        }

    /* everything below the horizon is water */
    g_mode(G_PAINT);
    g_rect(-1.0f, H, 242.0f, 242.0f - H, 0.0f);
    g_mode(G_MAX);

    if (vis->algo == 7 || (mt < 1.0f && vis->algo_prev == 7)) {      /* ABYSS: something down there opens an eye when you play */
        f32 a = vis->algo == 7 ? mt : 1.0f - mt, ex = 120.0f, ey = H + 104.0f, lid;
        spring(&eye_x, &eye_xv, (key_x(vis->last_pitch) - 120.0f) * 0.32f, 6.0f, dt);
        eye_o += ((0.1f + 0.9f * sat(lev * 1.6f)) - eye_o) * minf(1.0f, dt * 5.0f);
        lid = 34.0f * eye_o;
        g_ellipse(ex, ey, 66.0f, maxf(lid, 2.0f), 2.6f, (0.5f + 0.5f * glow) * a);
        if (lid > 9.0f) {
            g_disc(ex + eye_x, ey, minf(lid - 5.0f, 17.0f), (0.5f + 0.5f * glow) * a);
            g_mode(G_PAINT);
            g_disc(ex + eye_x, ey, minf(lid - 5.0f, 17.0f) * 0.45f, 0.0f);
            g_mode(G_MAX);
        }
    }

    /* the sea: lines that come nearer and bigger, bleeding off both sides */
    big = 0.35f + 0.9f * surge + 0.45f * lev;
    for (k = 0; k < NLINE; k++) {
        f32 u = (f32)k / (f32)(NLINE - 1), y0 = H + 5.0f + u * sqrtfx(u) * (236.0f - H), amp = (1.0f + 7.0f * u) * big, xy[2 * NPT];
        f32 wl = 1.0f / (46.0f + 70.0f * u), dir = (k & 1) ? 1.0f : -1.0f, heave = 5.0f * surge * u * sin_turns(sea_ph[0] * 0.5f + u * 0.2f);
        for (j = 0; j < NPT; j++) {
            f32 x = -9.0f + (f32)j * 6.0f, y;
            y = amp * (sin_turns(x * wl + dir * sea_ph[0] + u * 1.7f) + 0.5f * sin_turns(x * wl * 1.9f - dir * sea_ph[1] + u * 0.6f));
            if (drift > 0.01f)
                y += amp * drift * (0.45f * sin_turns(x * wl * 2.7f + sea_ph[2] * 3.0f + u * 2.0f) + 0.3f * sin_turns(x * wl * 4.1f - sea_ph[1] * 4.0f + u));
            for (i = 0; i < NVOICE; i++)
                if (swell[i] > 0.01f) {
                    f32 d = (x - swx[i]) / (16.0f + 22.0f * u);
                    if (d > -2.0f && d < 2.0f) {
                        d = 1.0f - d * d * 0.25f;
                        y -= swell[i] * (4.0f + 13.0f * u) * d * d;
                    }
                }
            xy[2 * j] = x;
            xy[2 * j + 1] = y0 + y + heave;
        }
        g_poly(xy, NPT, 0, 1.3f + 2.0f * u, 0.3f + 0.7f * u);
        /* the light on the water */
        if (vis->algo != 3 && vis->algo != 4 && vis->algo != 6 && vis->algo != 7 && my < H - 4.0f) {
            f32 gl = (0.25f + 0.75f * glow) * (0.5f + 0.5f * sin_turns(t * (0.5f + 0.13f * (f32)k) + u * 3.0f)) * mt;
            f32 half = (5.0f + 20.0f * u) * (0.5f + glow) * (0.7f + 0.3f * sin_turns(t * 0.37f + (f32)k * 0.41f));
            f32 cx = mx + 6.0f * sin_turns(t * 0.21f + (f32)k * 0.3f) * u, yy = y0 + heave - 3.0f - 2.0f * u;
            g_line(cx - half, yy, cx + half, yy, 2.0f + 1.6f * u, gl);
        }
    }
}

const InstDef inst_tide = {
    "TIDE",
    {128, 212, 255},
    {"GLOW", "HALO", "DRIFT", "SURGE"},
    {"MOON", "SUN", "ECLIPSE", "AURORA", "STORM", "FOG", "COMET", "ABYSS"},
    {{55, 25, 35, 30}, 0, {55, 60, 80, 62}, {0, 55, 0, 0}, {20, 4, 45, 50}, {0, 3, 1, 60}, {0, 0, 0}},
    0.3f,
    tide_block, tide_note, tide_render, 0, tide_draw,
};
