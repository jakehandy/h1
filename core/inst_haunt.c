/* h1 - HAUNT: the spooky one. Slow, cold, mostly air: a few sines that drift against each other,
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
    hum_ph = fractf(hum_ph + 50.0f * b->step);
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
    d3 = algo == 6 ? d0 : d0 * 0.5f;             /* (DEEP is two octaves down already: an octave under that is not a sound) */
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
typedef struct Ghost { f32 x, y, home, grow, a, fray, toll; } Ghost;
static Ghost gh[NVOICE];
static uint32_t seen_g[NVOICE];
static f32 lev_h, wisp_x = 60.0f, wisp_y = 150.0f, flick;
static f32 thin;                                 /* 0 alone .. 1 a crowd: every other thread is let go */
static f32 twist;                                /* VOID: how far round the hole a hem is carried, in turns */
static uint32_t jitter_seed = 1;

static f32 hsh(uint32_t i)
{
    i = i * 747796405u + 2891336453u;
    i = ((i >> ((i >> 28) + 4u)) ^ i) * 277803737u;
    return (f32)((i ^ (i >> 22)) >> 8) * (1.0f / 16777216.0f);
}

static f32 key_x(f32 pitch) { return 22.0f + clampf((pitch - 41.0f) / 50.0f, 0.0f, 1.0f) * 196.0f; }

/* One apparition: a hood of threads from (x, top), len long. dir +1 hangs down, -1 grows up from
 * below. style 0 threads, 1 scan-lines. bend pulls the hems toward (bx, by). fine is how much of
 * the odd threads is drawn (1 all of them, 0 none): a crowd is drawn with fewer threads each, so
 * that eight apparitions cost the frame about what three do. lines is how much of the folds and
 * the hood's rim is drawn over the cloth (0: cloth alone, for what stands behind). cloth 0: no
 * cloth, only its outline. */
static void shroud(f32 x, f32 top, f32 len, f32 hw, f32 a, f32 t, f32 sway, f32 shiver, uint32_t id, int dir, int style, f32 bend, f32 bx, f32 by, f32 fine, f32 lines, int cloth)
{
    int k, j, step;
    uint32_t js = jitter_seed + id * 7919u;
    static f32 P[NTH][NSEG + 1][2];              /* (not on the stack: the FM-1's is small) */
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
    for (k = 0; k < NTH; k++) {                  /* where every thread runs: the veil is hung on them */
        f32 u = (f32)k / (f32)(NTH - 1) * 2.0f - 1.0f, au = absf(u);
        f32 hx = x + u * hw, hy = top + (f32)dir * hw * 1.25f * (1.0f - sqrtfx(1.0f - u * u * 0.97f));
        f32 lk = len * (0.6f + 0.4f * hsh(id * 131u + (uint32_t)k)), ph = hsh(id * 17u + (uint32_t)k + 50u);
        f32 jx = shiver * 3.0f * (hsh(js + (uint32_t)k) - 0.5f);
        if (au < 0.34f)                          /* the middle threads start lower: the hood has nothing in it */
            hy += (f32)dir * hw * 1.5f * (1.0f - au / 0.34f);
        P[k][0][0] = hx;
        P[k][0][1] = hy;
        for (j = 1; j <= NSEG; j++) {
            f32 s = (f32)j / (f32)NSEG, d = s * lk, qx, qy;
            qx = hx + u * d * 0.13f + sway * s * (5.0f + 9.0f * s) * sin_turns(d * 0.009f - t * 0.33f + u * 0.13f + ph * 0.25f) + jx * s;
            qy = hy + (f32)dir * d;
            if (bend > 0.0f) {                   /* VOID: drawn toward the hole, and round it */
                f32 w = bend * s * s, rx, ry, c = cos_turns(twist * w), sn = sin_turns(twist * w);
                qx += (bx - qx) * w;
                qy += (by - qy) * w;
                rx = qx - bx; ry = qy - by;
                qx = bx + rx * c - ry * sn;
                qy = by + ry * c + rx * sn;
            }
            P[k][j][0] = qx;
            P[k][j][1] = qy;
        }
    }
    /* the cloth: one piece from edge to edge for each length of thread, thinning to nothing at the hem,
     * and the hollow of the hood cut out of it. A crowd, and whatever stands behind, gets longer pieces. */
    step = lines > 0.0f && fine >= 0.5f ? 1 : 2;
    for (j = 0; j < NSEG && cloth; j += step) {
        int j1 = imin(j + step, NSEG);
        f32 s0 = (f32)j / (f32)NSEG, s1 = (f32)j1 / (f32)NSEG, a0 = a * 0.4f * (1.0f - s0) * (1.0f - 0.4f * s0), a1 = a * 0.4f * (1.0f - s1) * (1.0f - 0.4f * s1), q[8];
        if (a0 < 0.02f)
            break;
        q[0] = P[0][j][0]; q[1] = P[0][j][1];
        q[2] = P[NTH - 1][j][0]; q[3] = P[NTH - 1][j][1];
        q[4] = P[NTH - 1][j1][0]; q[5] = P[NTH - 1][j1][1] + 0.7f * (f32)dir;        /* (each piece laps over the next: no seam) */
        q[6] = P[0][j1][0]; q[7] = P[0][j1][1] + 0.7f * (f32)dir;
        g_fillv(q, 4, 0.5f * (q[1] + q[3]), a0, 0.5f * (P[NTH - 1][j1][1] + P[0][j1][1]), a1);
    }
    if (cloth) {                                 /* the top of the hood, over the first piece */
        f32 q[2 * 7];
        for (k = 0; k < 7; k++) {
            f32 u = ((f32)k / 6.0f) * 2.0f - 1.0f;
            q[2 * k] = x + u * hw;
            q[2 * k + 1] = top + (f32)dir * hw * 1.25f * (1.0f - sqrtfx(1.0f - u * u * 0.97f));
        }
        g_fill(q, 7, a * 0.4f);
    }
    if (lines > 0.0f) {
        static const uint8_t FOLD[4] = {0, NTH - 1, 3, 7};
        int i, js2 = cloth ? 1 : 2;              /* (an outline alone is drawn in longer strokes) */
        for (i = 0; i < (cloth && fine > 0.3f ? 4 : 2); i++) {       /* its two edges, and two folds that a crowd goes without */
            f32 ak = a * lines * (i < 2 ? 1.0f : 0.85f * fine);
            k = FOLD[i];
            for (j = js2; j <= NSEG + js2 - 1; j += js2) {
                int jb = imin(j, NSEG), ja = j - js2;
                f32 s = (f32)jb / (f32)NSEG, al = ak * (1.0f - (s - 0.5f / (f32)NSEG)) * sat(1.15f - s * s);
                if (al >= 0.012f)                /* (fainter than that is black on the panel) */
                    g_line(P[k][ja][0], P[k][ja][1], P[k][jb][0], P[k][jb][1], 1.3f, al);
            }
        }
        if (cloth) {                             /* the rim of the hood */
            f32 q[2 * 7];
            for (k = 0; k < 7; k++) {
                f32 u = ((f32)k / 6.0f) * 2.0f - 1.0f;
                q[2 * k] = x + u * hw;
                q[2 * k + 1] = top + (f32)dir * hw * 1.25f * (1.0f - sqrtfx(1.0f - u * u * 0.97f));
            }
            g_poly(q, 7, 0, 1.4f, a * lines);
        }
    }
    if (cloth) {
        g_mode(G_PAINT);                         /* and under the hood, nothing */
        g_ellipse(x, top + (f32)dir * hw * 0.95f, hw * 0.4f, hw * 0.8f, 0.0f, 0.0f);
        g_mode(G_MAX);
    }
}

/* a pointed arch: two jambs up from the row gy to the springing at sy, half the opening hw wide */
static void lancet(f32 cx, f32 gy, f32 sy, f32 hw, f32 w, f32 a)
{
    g_line(cx - hw, gy, cx - hw, sy, w, a);
    g_line(cx + hw, gy, cx + hw, sy, w, a);
    g_arc(cx + hw * 0.62f, sy, hw * 1.62f, 0.5f, 0.672f, w, a);
    g_arc(cx - hw * 0.62f, sy, hw * 1.62f, -0.172f, 0.0f, w, a);
}

/* CHOIR's cathedral, standing in the water at gy. It is drawn faint: the choir is in front of it. Its
 * windows have light behind them when there is singing, and something goes round the spires. */
static void minster(f32 gy, f32 t, f32 lev, f32 dim)
{
    f32 a = (0.2f + 0.06f * sin_turns(t * 0.05f)) * dim, lit = (0.1f + 0.6f * lev) * dim, ry = gy - 112.0f;
    int i, k;
    for (k = -1; k <= 1; k += 2) {               /* the two towers, their spires not quite true */
        f32 cx = 120.0f + (f32)k * 68.0f, in = cx - (f32)k * 22.0f, out = cx + (f32)k * 22.0f, ty = gy - 138.0f, ax = cx + (f32)k * 2.5f, ay = gy - 212.0f, q[8];
        g_line(in, gy, in, ty, 1.3f, a * 1.2f);
        g_line(out, gy, out, ty + 4.0f, 1.3f, a * 1.2f);
        g_line(in, ty, out, ty + 4.0f, 1.3f, a * 1.2f);
        g_line(in + (f32)k * 2.0f, ty, ax, ay, 1.2f, a * 1.2f);
        g_line(out - (f32)k * 2.0f, ty + 4.0f, ax, ay, 1.2f, a * 1.2f);
        g_line(ax, ay, ax, ay - 9.0f, 1.0f, a * 1.3f);                   /* a cross on each */
        g_line(ax - 3.0f, ay - 6.0f, ax + 3.0f, ay - 6.0f, 1.0f, a * 1.3f);
        g_line(out, ty + 4.0f, out, ty - 12.0f, 1.0f, a);               /* pinnacles at the corners */
        g_line(in, ty, in, ty - 14.0f, 1.0f, a);
        g_line(in, gy - 60.0f, out, gy - 60.0f, 1.0f, a * 0.8f);        /* string courses */
        g_line(in, gy - 104.0f, out, gy - 104.0f, 1.0f, a * 0.8f);
        for (i = 0; i < 2; i++) {                /* the belfry: two tall lights with louvres, and a window under them */
            f32 lx = cx + ((f32)i - 0.5f) * 17.0f;
            lancet(lx, gy - 108.0f, gy - 126.0f, 5.0f, 1.0f, a);
            g_line(lx - 5.0f, gy - 114.0f, lx + 5.0f, gy - 116.0f, 1.0f, a * 0.7f);
            g_line(lx - 5.0f, gy - 120.0f, lx + 5.0f, gy - 122.0f, 1.0f, a * 0.7f);
        }
        q[0] = cx - 6.0f; q[1] = gy - 92.0f; q[2] = cx + 6.0f; q[3] = gy - 92.0f; q[4] = cx + 6.0f; q[5] = gy - 66.0f; q[6] = cx - 6.0f; q[7] = gy - 66.0f;
        g_fillv(q, 4, gy - 92.0f, lit * (0.7f + 0.3f * sin_turns(t * 0.9f + (f32)k)), gy - 66.0f, lit * 0.15f);
        lancet(cx, gy - 66.0f, gy - 90.0f, 7.0f, 1.2f, a * 1.2f);
        g_line(cx, gy - 66.0f, cx, gy - 96.0f, 1.0f, a);
        lancet(cx, gy, gy - 26.0f, 9.0f, 1.0f, a);                      /* and a side door */
    }
    /* between them: the gable, the rose, a gallery of little arches, and the great door */
    g_line(74.0f, gy - 128.0f, 120.0f, gy - 170.0f, 1.3f, a * 1.2f);
    g_line(166.0f, gy - 128.0f, 120.0f, gy - 170.0f, 1.3f, a * 1.2f);
    g_line(120.0f, gy - 170.0f, 120.0f, gy - 181.0f, 1.0f, a * 1.3f);
    g_line(116.5f, gy - 177.0f, 123.5f, gy - 177.0f, 1.0f, a * 1.3f);
    g_line(74.0f, gy - 128.0f, 166.0f, gy - 128.0f, 1.0f, a);
    g_line(74.0f, gy - 80.0f, 166.0f, gy - 80.0f, 1.0f, a);
    for (i = 0; i < 7; i++)
        g_arc(81.0f + (f32)i * 13.0f, gy - 70.0f, 5.0f, 0.5f, 1.0f, 1.0f, a * 0.9f);
    g_line(76.0f, gy - 70.0f, 164.0f, gy - 70.0f, 1.0f, a * 0.5f);
    g_mode(G_ADD);
    g_glow(120.0f, ry, 30.0f, lit * 0.55f);
    g_mode(G_MAX);
    for (i = 0; i < 12; i++) {                   /* the rose: twelve petals turning too slowly to see */
        f32 an = (f32)i / 12.0f + t * 0.004f, c = cos_turns(an), sn = sin_turns(an);
        g_line(120.0f + 6.0f * c, ry + 6.0f * sn, 120.0f + 15.0f * c, ry + 15.0f * sn, 1.0f, a + lit * 0.5f);
        g_ring(120.0f + 18.0f * cos_turns(an + 1.0f / 24.0f), ry + 18.0f * sin_turns(an + 1.0f / 24.0f), 3.6f, 1.0f, a + lit * 0.6f);
    }
    g_ring(120.0f, ry, 23.5f, 1.4f, a * 1.3f + lit * 0.5f);
    g_ring(120.0f, ry, 5.5f, 1.2f, a * 1.3f + lit * 0.6f);
    for (i = 0; i < 3; i++)                      /* the door: three orders of arch, each inside the last, and dark all the way in */
        lancet(120.0f, gy, gy - 34.0f + 3.0f * (f32)i, 21.0f - 5.0f * (f32)i, 1.2f, a * (1.3f - 0.2f * (f32)i));
    g_line(120.0f, gy, 120.0f, gy - 44.0f, 1.0f, a * 0.8f);
    g_line(86.0f, gy - 3.0f, 154.0f, gy - 3.0f, 1.0f, a * 0.8f);        /* steps */
    for (i = 0; i < 3; i++) {                    /* and whatever it is that lives in the spires */
        f32 ph = t * (0.05f + 0.013f * (f32)i) + (f32)i * 0.37f, bx = 120.0f + (f32)(i == 1 ? -1 : 1) * 68.0f + 26.0f * sin_turns(ph), by = gy - 176.0f - 12.0f * (f32)i + 9.0f * sin_turns(ph * 2.0f + 0.2f);
        f32 fl = 2.6f * sin_turns(t * 2.6f + (f32)i * 0.3f);
        g_line(bx, by, bx - 3.5f, by - 1.0f - fl, 1.0f, a * 1.6f);
        g_line(bx, by, bx + 3.5f, by - 1.0f - fl, 1.0f, a * 1.6f);
    }
}

/* (hidden) hold a tritone, bare, the interval they would not have in a church: lightning, thunder, and for a
 * moment you can see what has been standing behind the dark the whole time, looking at you */
static f32 tri_t, eyes;
static int eyes_done;

static APART void eyes_run(const Vis *vis)
{
    f32 t = vis->t, u, open, look;
    int i;
    {
        int held = 0, n0 = 0, n1 = 0;
        for (i = 0; i < NVOICE; i++)
            if (vis->v[i].on && vis->v[i].gate && !vis->v[i].seq) {
                n1 = n0;
                n0 = vis->v[i].note;
                held++;
            }
        if (held == 2 && (n0 - n1 + 120) % 12 == 6)
            tri_t += vis->dt;
        else
            tri_t = 0.0f, eyes_done = 0;
        if (tri_t > 1.0f && !eyes_done && eyes <= 0.0f && EGGS_ON) {
            eyes_done = 1;
            eyes = 1.0f;
            eng_cue(CUE_HAUNT);
        }
    }
    if (eyes <= 0.0f)
        return;
    eyes -= vis->dt * 0.24f;
    u = 1.0f - eyes;
    open = sat(u * 9.0f) * sat((1.0f - u) * 4.0f) * (fractf(t * 0.4f) > 0.94f ? 0.15f : 1.0f);
    look = 5.0f * sin_turns(t * 0.23f);
    if (u < 0.05f || (u > 0.09f && u < 0.11f))   /* the flash, and its second stroke */
        g_rect(0.0f, 0.0f, 240.0f, 240.0f, 0.55f);
    for (i = -1; i <= 1; i += 2) {
        f32 cx = 120.0f + 36.0f * (f32)i, cy = 34.0f;
        g_mode(G_PAINT);
        g_ellipse(cx, cy, 19.0f, 10.0f * open, 0.0f, 0.0f);
        g_mode(G_MAX);
        g_ellipse(cx, cy, 19.0f, 10.0f * open, 1.5f, 0.9f);
        g_line(cx + look, cy - 8.0f * open, cx + look, cy + 8.0f * open, 4.0f, 1.0f);
        g_line(cx + 22.0f * (f32)i, cy - 18.0f * open, cx - 12.0f * (f32)i, cy - 9.0f * open, 2.0f, 0.8f * open);
    }
}

static SMALL void haunt_draw(const Vis *vis)
{
    f32 dt = vis->dt, t = vis->t, fog = vis->k[0], dread = vis->k[1], shiver = vis->k[2], depth = vis->k[3], mt = smooth01(vis->algo_t);
    f32 fine, water = 232.0f - 46.0f * depth, dim = 0.15f + 0.85f * absf(2.0f * mt - 1.0f), tall = 84.0f + 80.0f * dread, vx = 120.0f, vy = 78.0f;
    int i, j, algo = mt < 0.5f ? vis->algo_prev : vis->algo, dir = 1, style = 0;

    if (vis->fresh) {
        for (i = 0; i < NVOICE; i++) {
            gh[i].a = gh[i].grow = gh[i].fray = gh[i].toll = 0.0f;
            gh[i].x = 120.0f;
            gh[i].y = gh[i].home = 60.0f;
            seen_g[i] = vis->v[i].serial;
        }
        lev_h = eyes = tri_t = 0.0f;
        thin = 0.0f;
    }
    lev_h += (minf(1.0f, vis->level * 4.0f) - lev_h) * minf(1.0f, dt * 5.0f);
    flick += dt;
    if (flick > 0.06f) {                         /* the shiver is redrawn sixteen times a second */
        flick = 0.0f;
        jitter_seed = rng_next(vis->rng) | 1u;
    }
    twist = 0.07f + 0.06f * sin_turns(t * 0.07f) + 0.1f * lev_h;
    if (algo == 6) dir = -1;
    if (algo == 3) style = 1;

    /* what stands in the dark */
    switch (algo) {
    default: {                                   /* SEANCE: a candle burning down in its dish */
        f32 fl = 0.72f + 0.16f * sin_turns(t * 1.7f) * sin_turns(t * 0.61f) + 0.07f * sin_turns(t * 4.3f + 0.2f) + 0.45f * lev_h;
        f32 wt = water - 31.0f, lean = 1.6f * sin_turns(t * 0.9f) + 1.1f * sin_turns(t * 2.3f) * (0.4f + lev_h), fh = 10.0f + 6.5f * fl, fw = 3.7f + 0.9f * fl, fy = wt - 4.0f, q[16];
        static const f32 TEAR[8][2] = {{0.0f, 0.03f}, {-0.5f, -0.14f}, {-0.6f, -0.33f}, {-0.36f, -0.64f}, {0.0f, -1.0f}, {0.36f, -0.64f}, {0.6f, -0.33f}, {0.5f, -0.14f}};
        g_mode(G_ADD);                           /* its light: close and warm, then wide and thin */
        g_glow(120.0f, fy - fh * 0.45f, 38.0f + 7.0f * fl, 0.15f * fl * dim);
        g_glow(120.0f + lean * 0.4f, fy - fh * 0.45f, 17.0f + 5.0f * fl, 0.42f * fl * dim);
        g_mode(G_MAX);
        g_line(107.0f, water - 2.5f, 133.0f, water - 2.5f, 2.2f, 0.75f * dim);       /* the dish, and its ring to carry it by */
        g_arc(120.0f, water - 15.0f, 15.5f, 0.17f, 0.33f, 1.3f, 0.6f * dim);
        g_ring(137.0f, water - 5.0f, 3.2f, 1.2f, 0.6f * dim);
        q[0] = 116.2f; q[1] = wt; q[2] = 123.8f; q[3] = wt; q[4] = 124.4f; q[5] = water - 4.0f; q[6] = 115.6f; q[7] = water - 4.0f;
        g_fillv(q, 4, wt, (0.62f + 0.3f * sat(fl)) * dim, water - 4.0f, 0.2f * dim);     /* the wax, lit from its own top */
        g_line(116.2f, wt, 115.6f, water - 4.0f, 1.0f, 0.6f * dim);
        g_line(123.8f, wt, 124.4f, water - 4.0f, 1.0f, 0.6f * dim);
        g_line(124.4f, wt + 1.0f, 124.8f, wt + 9.0f, 2.0f, 0.75f * dim);             /* where it has run, and set */
        g_disc(124.9f, wt + 10.0f, 1.6f, 0.75f * dim);
        g_line(117.0f, wt + 1.0f, 116.6f, wt + 5.0f, 1.8f, 0.7f * dim);
        g_ellipse(120.0f, wt, 3.8f, 1.2f, 0.0f, 0.9f * dim);                         /* the pool round the wick */
        g_line(120.0f, wt, 120.0f + lean * 0.15f, fy + 0.5f, 1.0f, 0.9f * dim);
        for (i = 0; i < 8; i++) {                /* the flame: a tear that leans and stretches */
            f32 up = -TEAR[i][1];
            q[2 * i] = 120.0f + TEAR[i][0] * fw + lean * up * up;
            q[2 * i + 1] = fy + TEAR[i][1] * fh;
        }
        g_fill(q, 8, sat(0.75f + 0.3f * fl) * dim);
        g_mode(G_PAINT);                         /* (dark at its root, where the wick is) */
        g_ellipse(120.0f + lean * 0.03f, fy - fh * 0.2f, fw * 0.2f, fh * 0.15f, 0.0f, 0.25f * dim);
        g_mode(G_MAX);
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
    case 4:                                      /* CHOIR: the west front of a cathedral nobody goes into now */
        minster(water, t, lev_h, dim);
        break;
    case 5:                                      /* BELLS: the beam they hang from */
        g_line(0.0f, 9.0f, 240.0f, 9.0f, 2.0f, 0.4f * dim);
        break;
    case 6:                                      /* DEEP: light from far above, in shafts that sway, and what drifts through them */
        for (i = 0; i < 3; i++) {
            f32 x0 = 62.0f + (f32)i * 58.0f + 12.0f * sin_turns(t * 0.023f + (f32)i * 0.27f), run = 96.0f + 30.0f * hsh((uint32_t)i + 5u) + 16.0f * sin_turns(t * 0.05f + (f32)i * 0.4f);
            f32 br = (0.26f + 0.1f * sin_turns(t * 0.11f + (f32)i * 0.37f) + 0.07f * sin_turns(t * 0.47f + (f32)i * 0.21f) + 0.12f * lev_h) * dim;
            g_beam(x0, -6.0f, x0 - 34.0f - 6.0f * sin_turns(t * 0.031f), run, 6.0f, 24.0f + 6.0f * (f32)(i & 1), br, 0.0f, 1.0f);
            g_line(x0 - 9.0f, 1.5f, x0 + 9.0f + 5.0f * sin_turns(t * 0.3f + (f32)i), 1.5f, 1.6f, br * 1.6f);       /* where it comes through the surface */
        }
        for (i = 0; i < 12; i++) {               /* marine snow, going the slow way up */
            f32 h = hsh((uint32_t)i + 600u), u = fractf(h + t * (0.012f + 0.014f * hsh((uint32_t)i + 610u)));
            f32 mx = hsh((uint32_t)i + 620u) * 240.0f + 9.0f * sin_turns(t * 0.07f + h * 5.0f), my = water - 8.0f - u * (water - 16.0f);
            g_disc(mx, my, 0.8f + 0.7f * h, (0.2f + 0.45f * (0.5f + 0.5f * sin_turns(t * 0.3f + h * 9.0f))) * sat(u * 8.0f) * sat((1.0f - u) * 6.0f) * dim);
        }
        break;
    case 7:                                      /* VOID: the hole (drawn after the threads, over them) */
        break;
    }

    eyes_run(vis);
    /* something small is always out there */
    {
        f32 gx = 120.0f + 86.0f * sin_turns(t * 0.013f), gy = water - 56.0f + 5.0f * sin_turns(t * 0.05f);
        shroud(gx, dir > 0 ? gy : water - 4.0f, 34.0f, 6.5f, (0.28f + 0.1f * sin_turns(t * 0.09f)) * dim * (1.0f - 0.6f * sat(lev_h * 2.0f)), t, 1.0f, shiver, 77u, dir, style, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1);
    }

    /* the apparitions: alone, each has all its threads; from the third on they thin out */
    {
        f32 crowd = 0.0f;
        for (i = 0; i < NVOICE; i++)
            crowd += sat(gh[i].a * 5.0f);
        thin += (sat((crowd - 2.0f) * 0.5f) - thin) * minf(1.0f, dt * 3.0f);
        fine = 1.0f - thin;
    }
    for (i = 0; i < NVOICE; i++) {
        const VVoice *v = &vis->v[i];
        Ghost *g = &gh[i];
        f32 want = 0.0f, top, len, hw;
        if (v->serial != seen_g[i]) {
            seen_g[i] = v->serial;
            g->toll = 1.0f;
            g->home = 46.0f + 26.0f * hsh(v->serial * 31u + (uint32_t)i);
            if (g->a < 0.03f) {
                g->grow = 0.0f;
                g->y = g->home;
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
        if (v->on && v->gate)
            g->y += (g->home - g->y) * minf(1.0f, dt * 4.0f);   /* struck again, it comes back down */
        else
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
                   algo == 7 ? 0.5f : 0.0f, vx, vy, 0.0f, 1.0f, 0);
        shroud(g->x, top, len, hw, g->a * dim, t, 1.0f + 2.0f * g->fray, shiver, (uint32_t)i, dir, style, algo == 7 ? 0.55f : 0.0f, vx, vy, fine, 1.0f, 1);
        if (algo == 4)                           /* CHOIR: it does not come alone */
            for (j = -1; j <= 1; j += 2)
                if (thin < 0.5f || ((i & 1) ? j > 0 : j < 0))    /* (in a crowd, one each) */
                shroud(g->x + (f32)j * (hw * 2.0f + 6.0f), top + 16.0f, len * 0.7f, hw * 0.7f, g->a * 0.55f * dim, t + (f32)j, 1.0f + 2.0f * g->fray, shiver, (uint32_t)(i * 3 + j + 60), dir, style, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1);
    }

    if (algo == 7) {                             /* VOID: everything hangs toward it, is carried round it, and goes out */
        f32 toll = 0.0f, R, xy[2 * 24], spin = t * 0.11f;
        for (i = 0; i < NVOICE; i++)
            toll = maxf(toll, gh[i].toll * sat(gh[i].a * 8.0f + 0.2f));
        R = 25.0f + 5.0f * lev_h + 3.0f * toll * toll;
        g_mode(G_PAINT);                         /* light does not get away from near it */
        g_glow(vx, vy, R * 1.8f, 0.97f * dim);
        g_mode(G_ADD);
        g_glow(vx, vy, R * 1.7f, (0.24f + 0.34f * lev_h) * dim);
        g_mode(G_MAX);
        for (j = 0; j < 4; j++) {                /* what is left of what went in: arcs of it, each ring turning at its own rate, the inner ones fastest */
            f32 rr = R + 5.0f + (f32)j * (5.0f + 1.5f * (f32)j) + 2.0f * sin_turns(t * 0.21f + (f32)j * 0.3f), w = spin * (4.0f - (f32)j) * ((j & 1) ? -1.0f : 1.0f);
            for (i = 0; i < 3; i++) {
                f32 h = hsh(jitter_seed * (shiver > 0.5f ? 1u : 0u) + (uint32_t)(j * 3 + i)), a0 = w + (f32)i / 3.0f + 0.07f * sin_turns(t * 0.13f + (f32)(i + j)), span = 0.06f + 0.16f * hsh((uint32_t)(j * 5 + i) + 70u);
                g_arc(vx + shiver * 3.0f * (h - 0.5f), vy, rr, a0, a0 + span, 1.7f - 0.3f * (f32)j, (0.75f - 0.15f * (f32)j) * (0.5f + 0.5f * lev_h + 0.3f * sin_turns(t * 0.4f + h * 7.0f)) * dim);
            }
        }
        for (i = 0; i < 14; i++) {               /* and what is going in now: slower, then round, then all at once */
            f32 h = hsh((uint32_t)i + 400u), q = fractf(h + t * (0.07f + 0.06f * hsh((uint32_t)i + 410u)) * (1.0f + 1.5f * lev_h)), q2 = q * q;
            f32 r0 = R * (3.6f - 2.6f * q2), an = h * 3.0f + q2 * 1.4f + spin, r1 = R * (3.6f - 2.6f * (q2 + 0.03f + 0.09f * q)), an1 = an + 0.012f + 0.05f * q;
            if (r1 > R)
                g_line(vx + cos_turns(an) * r0, vy + sin_turns(an) * r0, vx + cos_turns(an1) * r1, vy + sin_turns(an1) * r1, 1.0f + 0.8f * q, sat(q * 3.0f) * (0.4f + 0.6f * q) * dim);
        }
        g_mode(G_PAINT);
        g_disc(vx, vy, R, 0.0f);
        g_mode(G_MAX);
        for (i = 0; i < 24; i++) {               /* its edge is not a circle, and will not keep still */
            f32 an = (f32)i / 24.0f, rr = R * (1.0f + (0.02f + 0.03f * lev_h) * sin_turns(an * 3.0f + t * 0.31f) + 0.02f * sin_turns(an * 5.0f - t * 0.53f)
                                                 + shiver * 0.07f * (hsh(jitter_seed + (uint32_t)i) - 0.5f));
            xy[2 * i] = vx + cos_turns(an) * rr;
            xy[2 * i + 1] = vy + sin_turns(an) * rr;
        }
        g_poly(xy, 24, 1, 1.7f + 1.6f * lev_h, (0.6f + 0.4f * lev_h) * dim);
        if (toll > 0.02f)                        /* a note goes out from it as a ring, once */
            g_ring(vx, vy, R + 4.0f + 74.0f * (1.0f - toll), 1.0f + 1.5f * toll, toll * toll * 0.8f * dim);
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
    if (algo == 0)                               /* SEANCE: the flame, in the water */
        for (j = 0; j < 5; j++) {
            f32 y = water + 4.0f + (f32)j * 5.0f, w = (4.0f + 2.5f * (f32)j) * (0.7f + 0.3f * sin_turns(t * 0.5f + (f32)j * 0.3f));
            if (y < 238.0f)
                g_beam(120.0f - w + 2.0f * sin_turns(t * 0.3f + (f32)j), y, 120.0f + w + 2.0f * sin_turns(t * 0.3f + (f32)j), y, 2.0f, 2.0f, (0.5f - 0.08f * (f32)j) * (0.7f + 0.5f * lev_h) * dim, (0.5f - 0.08f * (f32)j) * (0.7f + 0.5f * lev_h) * dim, 0.6f);
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
            g_mode(G_PAINT);                     /* (a soft cut: it goes through the candle's light as mist, not as a bar) */
            g_beam(x0, y, x0 + w, y, 8.0f + 7.0f * fog, 8.0f + 7.0f * fog, 0.85f, 0.85f, 1.0f);
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
