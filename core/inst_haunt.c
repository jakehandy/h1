/* H1 - HAUNT: the spooky one. Slow, cold, mostly air: a few sines that drift against each other,
 * a wind that whistles at the note's pitch, and a great deal of room. The picture is a dark place
 * with something in it. Every note calls up an apparition, a hooded shape hung from threads, that
 * lets itself down while you hold the key and frays away when you let go. Something small is
 * always out there whether you play or not.
 *
 *   K1 FOG     the wind                                fog drifts across and cuts the threads
 *   K2 DREAD   the voices pull out of tune, a shadow   they loom taller, and each grows a second self
 *              a semitone above
 *   K3 SHIVER  a tremble in level and pitch            the threads shake
 *   K4 DEPTH   an octave below, and darker             the black water under them rises
 */
#include "internal.h"
#include "dsp.h"

/* ---- sound ------------------------------------------------------------------------------ */
static struct {
    int algo;
    f32 fog, dread, shiver, depth, hum;
} H;
static f32 hum_ph;

static void haunt_block(const Blk *b)
{
    H.algo = b->algo;
    H.fog = b->k[0];
    H.dread = b->k[1];
    H.shiver = b->k[2];
    H.depth = b->k[3];
    hum_ph = fractf(hum_ph + 50.0f * (f32)CTRL * b->isr);
    H.hum = hum_ph < 0.5f ? 1.0f : 0.35f;
}

static void haunt_note(Voice *v, const Blk *b)
{
    int i;
    (void)b;
    if (!v->u[0]) v->u[0] = 0x0DDBA11u + v->idx * 7919u;
    if (v->env < 0.001f) {
        for (i = 0; i < 14; i++)
            v->s[i] = 0.0f;
        for (i = 0; i < 6; i++)
            v->ph[i] = fractf(v->r1 * (f32)(i + 1) * 0.31f);
    }
    v->s[8] = 1.0f;                              /* BELLS: struck */
    v->s[9] = 1.4f + 2.5f * v->r2;
}

static void haunt_render(Voice *v, const Blk *b, f32 *out, int n)
{
    Svf fw, f2;
    int algo = H.algo, i;
    f32 dt = (f32)n * b->isr, pitch = v->pitch + b->bend, f, d0, d1, d2, d3, amp, shadow = H.dread * H.dread * 0.55f;
    f32 wind = H.fog * H.fog * 5.0f, wf, wq = 9.0f, sub = H.depth * 0.8f, lpk, swell, vib = 0.0f, bell = 0.0f, tone = 1.0f, up = 0.0f, det;
    f32 p0 = v->ph[0], p1 = v->ph[1], p2 = v->ph[2], p3 = v->ph[3], lp = v->s[6], fbk = v->s[12], dc = v->s[7];

    /* every voice wanders by itself, and shivers by itself */
    v->s[3] += (rng_s(&v->u[0]) - v->s[3]) * 0.004f;
    v->s[2] += (v->s[3] - v->s[2]) * 0.012f;
    v->s[5] += (rng_s(&v->u[0]) - v->s[5]) * 0.09f;
    v->s[4] += (v->s[5] - v->s[4]) * 0.2f;
    v->ph[4] = fractf(v->ph[4] + (0.11f + 0.05f * v->r2) * dt);
    v->ph[5] = fractf(v->ph[5] + 4.3f * dt);
    swell = 0.5f + 0.5f * tsin(v->ph[4]);
    pitch += v->s[2] * (0.6f + 3.5f * H.dread) + H.shiver * v->s[4] * 1.2f;
    amp = 1.0f - H.shiver * (0.5f + 0.5f * clampf(v->s[4] * 4.0f, -1.0f, 1.0f)) * 0.85f;
    det = 0.08f + 0.5f * H.dread;

    switch (algo) {
    default: break;                                                                   /* SEANCE */
    case 1: pitch -= 12.0f; det += 0.12f; wq = 2.5f; sub += 0.3f; break;              /* CRYPT */
    case 2: pitch -= 0.8f * exp2fx(-v->age * 1.1f); vib = 0.3f * sat(v->age * 0.6f); break;   /* WRAITH */
    case 3: tone = 0.12f; wind = wind * 1.5f + 1.6f; wq = 24.0f; amp *= H.hum; break; /* STATIC */
    case 4: tone = 0.0f; wind *= 0.5f; break;                                         /* CHOIR */
    case 5: tone = 0.35f; break;                                                      /* BELLS */
    case 6: pitch -= 24.0f; sub += 0.5f; amp *= 0.75f; break;                                       /* DEEP */
    case 7:                                                                           /* VOID: every swell runs backwards */
        v->s[13] = fractf(v->s[13] + 0.62f * dt);
        amp *= 0.12f + v->s[13] * v->s[13] * 1.5f;
        up = 0.35f * v->s[13];
        break;
    }
    pitch += vib * tsin(v->ph[5]);
    f = mtof(pitch);
    d0 = minf(f * b->isr, 0.4f);
    d1 = d0 * exp2fx(det * (1.0f / 12.0f));
    d2 = minf(d0 * (algo == 5 ? 2.76f : (up > 0.0f ? 2.0f : 1.0595f)), 0.45f);             /* the shadow: a semitone up */
    d3 = d0 * 0.5f;
    wf = algo == 1 ? f * 1.5f : (algo == 3 ? f * 4.0f : f * (2.5f + 1.5f * swell));
    svf_set(&fw, minf(wf, 9000.0f), wq, b->isr);
    lpk = onepole_k(1.0f / (H1_TAU * (7000.0f * exp2fx(-H.depth * 3.8f))), b->sr);

    if (algo == 5) {                             /* BELLS: it tolls again, when it likes */
        v->s[9] -= dt;
        if (v->s[9] <= 0.0f && v->gate) {
            v->s[8] = 0.7f + 0.3f * rng_f(&v->u[0]);
            v->s[9] = 1.0f + 3.0f * rng_f(&v->u[0]);
        }
        bell = v->s[8];
        v->s[8] *= 1.0f - onepole_k(1.3f, b->sr / (f32)n);
    }
    if (algo == 4) {                             /* CHOIR: two formants, sliding between OO and AH */
        f32 m = 0.5f + 0.5f * tsin(v->ph[4] * 0.7f + v->r1);
        svf_set(&fw, lerpf(320.0f, 720.0f, m), 9.0f, b->isr);
        svf_set(&f2, lerpf(820.0f, 1150.0f, m), 10.0f, b->isr);
    } else
        svf_set(&f2, 1000.0f, 1.0f, b->isr);

    for (i = 0; i < n; i++) {
        f32 x, nz = noise_s(&v->u[0]), lo, bp;
        switch (algo) {
        default: x = tsin(p0) + 0.8f * tsin(p1); break;
        case 2: fbk = tsin(p0 + 0.19f * fbk); x = 1.3f * fbk + 0.4f * tsin(p1); break;
        case 5:                                  /* (s[10], s[11]: two more of the bell's partials) */
            v->s[10] += d0 * 5.4f; if (v->s[10] >= 1.0f) v->s[10] -= (f32)(int)v->s[10];
            v->s[11] += d0 * 1.19f; if (v->s[11] >= 1.0f) v->s[11] -= 1.0f;
            x = tsin(p0) + 0.8f * tsin(p1) + bell * (1.1f * tsin(p2) + 0.5f * tsin(v->s[10]) + 0.7f * tsin(v->s[11]));
            break;
        case 6: x = 1.4f * tsin(p0 + 0.2f * swell * tsin(p1 * 2.0f)) + 0.8f * tsin(p1); break;
        }
        x = x * tone * 0.5f + shadow * 0.5f * tsin(p2) * (algo == 5 ? 0.0f : 1.0f) + up * tsin(p2);
        if (algo == 4) {
            f32 src = pulse_bl(p0, d0, 0.3f) + 0.6f * pulse_bl(p1, d1, 0.3f) + shadow * pulse_bl(p2, d2, 0.3f) + H.fog * 0.5f * nz, a1, a2;
            SVF_TICK(fw, &v->s[0], src, lo, a1);
            SVF_TICK(f2, &v->s[10], src, lo, a2);
            x = (a1 + 0.7f * a2) * 0.22f;
        } else if (wind > 0.001f) {
            SVF_TICK(fw, &v->s[0], nz, lo, bp);
            x += bp * wind * (0.6f + 0.4f * swell) * (1.0f / wq) * 2.2f;
        }
        x += sub * tsin(p3);
        lp += (x - lp) * lpk;
        dc += (lp - dc) * 0.0008f;               /* (the wraith's feedback leaves an offset) */
        out[i] = (lp - dc) * amp;
        p0 += d0; if (p0 >= 1.0f) p0 -= 1.0f;
        p1 += d1; if (p1 >= 1.0f) p1 -= 1.0f;
        p2 += d2; if (p2 >= 1.0f) p2 -= 1.0f;
        p3 += d3; if (p3 >= 1.0f) p3 -= 1.0f;
    }
    v->ph[0] = p0; v->ph[1] = p1; v->ph[2] = p2; v->ph[3] = p3;
    v->s[6] = lp;
    v->s[7] = dc;
    v->s[12] = fbk;
}

/* ---- picture ---------------------------------------------------------------------------- */
#define NTH 11
#define NSEG 7
typedef struct Ghost { f32 x, y, vy, grow, a, fray, toll; } Ghost;
static Ghost gh[NVOICE];
static uint32_t seen_g[NVOICE];
static f32 lev_h, wisp_x = 60.0f, wisp_y = 150.0f, flick;
static uint32_t jitter_seed = 1;

static f32 hsh(uint32_t i)
{
    i = i * 747796405u + 2891336453u;
    i = ((i >> ((i >> 28) + 4u)) ^ i) * 277803737u;
    return (f32)((i ^ (i >> 22)) >> 8) * (1.0f / 16777216.0f);
}

static f32 key_x(f32 pitch) { return 22.0f + clampf((pitch - 41.0f) / 50.0f, 0.0f, 1.0f) * 196.0f; }

/* One apparition: a hood of threads from (x, top), len long. dir +1 hangs down, -1 grows up from
 * below. style 0 threads, 1 scan-lines. bend pulls the hems toward (bx, by). */
static void shroud(f32 x, f32 top, f32 len, f32 hw, f32 a, f32 t, f32 sway, f32 shiver, uint32_t id, int dir, int style, f32 bend, f32 bx, f32 by)
{
    int k, j;
    uint32_t js = jitter_seed + id * 7919u;
    if (a <= 0.01f || len < 2.0f)
        return;
    if (style == 1) {                            /* STATIC: the same shape, as lines of a bad picture */
        for (j = 0; j < 22; j++) {
            f32 s = (f32)j / 21.0f, y = top + s * len, w = hw * (s < 0.18f ? sqrtfx(s / 0.18f) : 1.0f + (s - 0.18f) * 0.9f);
            f32 off = (hsh(js + (uint32_t)j) < 0.12f + shiver * 0.4f ? (hsh(js + 99u + (uint32_t)j) - 0.5f) * 30.0f : 0.0f);
            f32 al = a * (1.0f - s) * (0.5f + 0.5f * hsh(js + 300u + (uint32_t)j));
            if (s > 0.06f && s < 0.3f) {         /* the hood is empty */
                g_line(x + off - w, y, x + off - w * 0.45f, y, 1.5f, al);
                g_line(x + off + w * 0.45f, y, x + off + w, y, 1.5f, al);
            } else
                g_line(x + off - w, y, x + off + w, y, 1.5f, al);
        }
        return;
    }
    for (k = 0; k < NTH; k++) {
        f32 u = (f32)k / (f32)(NTH - 1) * 2.0f - 1.0f, au = absf(u);
        f32 hx = x + u * hw, hy = top + (f32)dir * hw * 1.25f * (1.0f - sqrtfx(1.0f - u * u * 0.97f));
        f32 lk = len * (0.6f + 0.4f * hsh(id * 131u + (uint32_t)k)), px, py, ph = hsh(id * 17u + (uint32_t)k + 50u);
        f32 jx = shiver * 3.0f * (hsh(js + (uint32_t)k) - 0.5f);
        if (au < 0.34f)                          /* the middle threads start lower: the hood has nothing in it */
            hy += (f32)dir * hw * 1.5f * (1.0f - au / 0.34f);
        px = hx;
        py = hy;
        for (j = 1; j <= NSEG; j++) {
            f32 s = (f32)j / (f32)NSEG, d = s * lk, qx, qy, al;
            qx = hx + u * d * 0.13f + sway * s * (5.0f + 9.0f * s) * sin_turns(d * 0.009f - t * 0.33f + u * 0.13f + ph * 0.25f) + jx * s;
            qy = hy + (f32)dir * d;
            if (bend > 0.0f) {
                qx += (bx - qx) * bend * s * s;
                qy += (by - qy) * bend * s * s;
            }
            al = a * (1.0f - (s - 0.5f / (f32)NSEG)) * (0.5f + 0.5f * (1.0f - au));
            g_line(px, py, qx, qy, 1.3f, al * sat(1.15f - s * s));
            px = qx; py = qy;
        }
    }
}

static void haunt_draw(const Vis *vis)
{
    f32 dt = vis->dt, t = vis->t, fog = vis->k[0], dread = vis->k[1], shiver = vis->k[2], depth = vis->k[3], mt = smooth01(vis->algo_t);
    f32 water = 232.0f - 46.0f * depth, dim = 0.15f + 0.85f * absf(2.0f * mt - 1.0f), tall = 84.0f + 80.0f * dread, vx = 120.0f, vy = 78.0f;
    int i, j, algo = mt < 0.5f ? vis->algo_prev : vis->algo, dir = 1, style = 0;

    if (vis->fresh) {
        for (i = 0; i < NVOICE; i++) {
            gh[i].a = gh[i].grow = gh[i].fray = gh[i].toll = 0.0f;
            gh[i].x = 120.0f;
            gh[i].y = 60.0f;
            gh[i].vy = 0.0f;
            seen_g[i] = vis->v[i].serial;
        }
        lev_h = 0.0f;
    }
    lev_h += (minf(1.0f, vis->level * 4.0f) - lev_h) * minf(1.0f, dt * 5.0f);
    flick += dt;
    if (flick > 0.06f) {                         /* the shiver is redrawn sixteen times a second */
        flick = 0.0f;
        jitter_seed = rng_next(vis->rng) | 1u;
    }
    if (algo == 6) dir = -1;
    if (algo == 3) style = 1;

    /* what stands in the dark */
    switch (algo) {
    default: {                                   /* SEANCE: a candle */
        f32 fl = 0.7f + 0.3f * sin_turns(t * 1.7f) * sin_turns(t * 0.61f) + 0.5f * lev_h, cx = 120.0f + 1.5f * sin_turns(t * 0.9f), cy = water - 22.0f;
        g_line(120.0f, water - 2.0f, 120.0f, cy + 9.0f, 3.0f, 0.5f * dim);
        g_ellipse(cx, cy, 3.0f + 1.0f * fl, 7.0f + 4.0f * fl, 0.0f, (0.6f + 0.4f * sat(fl)) * dim);
        g_ring(120.0f, cy, 18.0f + 6.0f * fl, 1.0f, 0.22f * dim);
        g_ring(120.0f, cy, 34.0f + 10.0f * fl, 1.0f, 0.12f * dim);
        break;
    }
    case 1:                                      /* CRYPT: arches */
        for (i = 0; i < 3; i++) {
            f32 cx = 44.0f + (f32)i * 76.0f, w = 30.0f, a = (0.22f + 0.1f * sin_turns(t * 0.07f + (f32)i * 0.3f)) * dim;
            g_line(cx - w, water, cx - w, 96.0f, 1.3f, a);
            g_line(cx + w, water, cx + w, 96.0f, 1.3f, a);
            g_arc(cx + w * 0.62f, 96.0f, w * 1.62f, 0.5f, 0.672f, 1.3f, a);
            g_arc(cx - w * 0.62f, 96.0f, w * 1.62f, -0.172f, 0.0f, 1.3f, a);
        }
        break;
    case 2:                                      /* WRAITH: a thin moon */
        g_disc(196.0f, 40.0f, 15.0f, 0.75f * dim);
        g_mode(G_PAINT);
        g_disc(190.0f, 36.0f, 14.0f, 0.0f);
        g_mode(G_MAX);
        break;
    case 3:                                      /* STATIC: snow */
        for (i = 0; i < 26; i++) {
            f32 sx = hsh(jitter_seed + (uint32_t)i) * 240.0f, sy = hsh(jitter_seed + 500u + (uint32_t)i) * 240.0f;
            g_line(sx, sy, sx + 2.0f + 5.0f * hsh(jitter_seed + 900u + (uint32_t)i), sy, 1.0f, (0.18f + 0.3f * lev_h) * dim);
        }
        break;
    case 4:                                      /* CHOIR: a rose window, far up */
        for (i = 0; i < 12; i++) {
            f32 an = (f32)i / 12.0f + t * 0.004f;
            g_line(120.0f + 9.0f * cos_turns(an), 44.0f + 9.0f * sin_turns(an), 120.0f + 27.0f * cos_turns(an), 44.0f + 27.0f * sin_turns(an), 1.2f, (0.2f + 0.25f * lev_h) * dim);
        }
        g_ring(120.0f, 44.0f, 30.0f, 1.3f, (0.25f + 0.3f * lev_h) * dim);
        break;
    case 5:                                      /* BELLS: the beam they hang from */
        g_line(0.0f, 9.0f, 240.0f, 9.0f, 2.0f, 0.4f * dim);
        break;
    case 6:                                      /* DEEP: light from far above */
        for (i = 0; i < 5; i++) {
            f32 x0 = 60.0f + (f32)i * 30.0f + 10.0f * sin_turns(t * 0.03f + (f32)i * 0.2f);
            g_line(x0, -4.0f, x0 - 34.0f + (f32)i * 17.0f, 70.0f + 30.0f * hsh((uint32_t)i + 5u), 1.2f, (0.1f + 0.08f * sin_turns(t * 0.11f + (f32)i * 0.37f)) * dim);
        }
        break;
    case 7:                                      /* VOID: the hole (drawn after the threads, over them) */
        break;
    }

    /* something small is always out there */
    {
        f32 gx = 120.0f + 86.0f * sin_turns(t * 0.013f), gy = water - 56.0f + 5.0f * sin_turns(t * 0.05f);
        shroud(gx, dir > 0 ? gy : water - 4.0f, 34.0f, 6.5f, (0.28f + 0.1f * sin_turns(t * 0.09f)) * dim * (1.0f - 0.6f * sat(lev_h * 2.0f)), t, 1.0f, shiver, 77u, dir, style, 0.0f, 0.0f, 0.0f);
    }

    /* the apparitions */
    for (i = 0; i < NVOICE; i++) {
        const VVoice *v = &vis->v[i];
        Ghost *g = &gh[i];
        f32 want = 0.0f, top, len, hw;
        if (v->serial != seen_g[i]) {
            seen_g[i] = v->serial;
            g->toll = 1.0f;
            if (g->a < 0.03f) {
                g->grow = 0.0f;
                g->y = 46.0f + 26.0f * hsh(v->serial * 31u + (uint32_t)i);
                g->vy = 0.0f;
                g->x = key_x(v->pitch);
            }
        }
        if (v->on) {
            want = v->env;
            g->x += (key_x(v->pitch) - g->x) * minf(1.0f, dt * 3.0f);
        }
        g->a += (want - g->a) * minf(1.0f, dt * (want > g->a ? 6.0f : 1.6f));
        g->grow += ((v->on && v->gate ? 1.0f : 0.55f) - g->grow) * minf(1.0f, dt * 1.8f);
        g->fray += ((v->on && v->gate ? 0.0f : 1.0f) - g->fray) * minf(1.0f, dt * 1.5f);
        g->toll = maxf(0.0f, g->toll - dt * 0.8f);
        if (!(v->on && v->gate))
            g->y -= dt * 9.0f;                   /* let go, it drifts up and away */
        if (g->a < 0.015f)
            continue;
        hw = 10.0f + 8.0f * dread;
        len = tall * g->grow;
        top = g->y + 4.0f * sin_turns(t * 0.11f + (f32)i * 0.21f);
        if (algo == 5) {                         /* BELLS: ropes from the beam, a bell on each, swinging when it tolls */
            f32 sw = 0.05f * sin_turns(t * 0.45f + (f32)i * 0.2f) * (0.3f + g->toll * 2.0f), l = 60.0f + 70.0f * hsh((uint32_t)i + 9u);
            f32 ex = g->x + l * sin_turns(sw), ey = 9.0f + l * cos_turns(sw);
            g_line(g->x, 9.0f, ex, ey, 1.4f, g->a * dim);
            g_ring(ex, ey + 8.0f, 8.0f, 2.0f, g->a * dim);
            g_ring(ex, ey + 8.0f, 13.0f + 22.0f * (1.0f - g->toll), 1.0f, g->a * g->toll * 0.6f * dim);
            top = ey + 24.0f;
            len *= 0.6f;
        }
        if (dir < 0)
            top = water - 6.0f;
        if (dread > 0.03f)                       /* DREAD: its second self, a step to one side */
            shroud(g->x + 9.0f + 14.0f * dread, top - (f32)dir * 7.0f * dread, len * 1.06f, hw, g->a * dread * 0.5f * dim, t + 1.7f, 1.0f + 2.0f * g->fray, shiver, (uint32_t)i + 40u, dir, style,
                   algo == 7 ? 0.5f : 0.0f, vx, vy);
        shroud(g->x, top, len, hw, g->a * dim, t, 1.0f + 2.0f * g->fray, shiver, (uint32_t)i, dir, style, algo == 7 ? 0.55f : 0.0f, vx, vy);
        if (algo == 4)                           /* CHOIR: it does not come alone */
            for (j = -1; j <= 1; j += 2)
                shroud(g->x + (f32)j * (hw * 2.0f + 6.0f), top + 16.0f, len * 0.7f, hw * 0.7f, g->a * 0.55f * dim, t + (f32)j, 1.0f + 2.0f * g->fray, shiver, (uint32_t)(i * 3 + j + 60), dir, style, 0.0f, 0.0f, 0.0f);
    }

    if (algo == 7) {                             /* VOID: everything hangs toward it */
        g_mode(G_PAINT);
        g_disc(vx, vy, 27.0f + 3.0f * lev_h, 0.0f);
        g_mode(G_MAX);
        g_ring(vx, vy, 27.0f + 3.0f * lev_h, 1.6f, (0.5f + 0.5f * lev_h) * dim);
        g_ring(vx, vy, 34.0f + 7.0f * lev_h + 2.0f * sin_turns(t * 0.3f), 1.0f, 0.25f * dim);
    }

    /* DEPTH: black water, and what it shows */
    g_mode(G_PAINT);
    g_rect(-1.0f, water, 242.0f, 242.0f - water, 0.0f);
    g_mode(G_MAX);
    for (j = 0; j < 5; j++) {
        f32 y = water + 2.0f + (f32)j * (3.0f + 2.0f * (f32)j), a = (0.4f - 0.07f * (f32)j) * dim;
        if (y > 238.0f)
            break;
        for (i = 0; i < 7; i++) {
            f32 x0 = fractf(hsh((uint32_t)(j * 7 + i)) + t * 0.006f * (f32)(1 + (j & 1))) * 300.0f - 30.0f, w = 12.0f + 30.0f * hsh((uint32_t)(j * 7 + i) + 33u);
            g_line(x0, y, x0 + w, y, 1.2f, a * (0.5f + 0.5f * sin_turns(t * 0.2f + (f32)i * 0.3f + (f32)j)));
        }
    }
    for (i = 0; i < NVOICE; i++)                 /* each apparition's light on the water */
        if (gh[i].a > 0.03f)
            for (j = 0; j < 4; j++) {
                f32 y = water + 4.0f + (f32)j * 6.0f, w = (10.0f + 6.0f * (f32)j) * (0.7f + 0.3f * sin_turns(t * 0.6f + (f32)j * 0.3f + (f32)i));
                if (y < 238.0f)
                    g_line(gh[i].x - w, y, gh[i].x + w, y, 1.6f, gh[i].a * (0.6f - 0.12f * (f32)j) * dim);
            }

    /* FOG: bands that drift across, in front of everything, and cut what they cross */
    if (fog > 0.02f) {
        int nf = 3 + (int)(fog * 9.0f);
        for (i = 0; i < nf; i++) {
            f32 y = 26.0f + hsh((uint32_t)i + 200u) * (water - 40.0f) + 5.0f * sin_turns(t * 0.04f + (f32)i * 0.4f);
            f32 x0 = fractf(hsh((uint32_t)i + 240u) + t * (0.01f + 0.012f * hsh((uint32_t)i + 280u))) * 420.0f - 150.0f, w = 60.0f + 80.0f * hsh((uint32_t)i + 320u);
            g_mode(G_PAINT);
            g_line(x0, y, x0 + w, y, 5.0f + 5.0f * fog, 0.0f);
            g_mode(G_MAX);
            g_line(x0 + 6.0f, y, x0 + w - 6.0f, y, 1.2f, 0.32f * fog * dim);
        }
    }

    /* a will-o'-the-wisp, wandering */
    wisp_x = 120.0f + 92.0f * sin_turns(t * 0.021f) * cos_turns(t * 0.0083f);
    wisp_y = water - 30.0f - 60.0f * (0.5f + 0.5f * sin_turns(t * 0.017f + 0.2f));
    g_disc(wisp_x, wisp_y, 1.6f + 0.8f * sin_turns(t * 0.8f), (0.5f + 0.4f * sin_turns(t * 0.37f)) * dim);
    g_ring(wisp_x, wisp_y, 5.0f + 2.0f * sin_turns(t * 0.8f), 1.0f, 0.16f * dim);
}

const InstDef inst_haunt = {
    "HAUNT",
    {150, 255, 214},
    {"FOG", "DREAD", "SHIVER", "DEPTH"},
    {"SEANCE", "CRYPT", "WRAITH", "STATIC", "CHOIR", "BELLS", "DEEP", "VOID"},
    {{35, 30, 20, 30}, 0, {64, 60, 85, 80}, {5, 12, 22, 2}, {34, 6, 78, 42}, {0, 3, 1, 60}, {0, 0, 0}},
    0.3f,
    haunt_block, haunt_note, haunt_render, 0, haunt_draw,
};
