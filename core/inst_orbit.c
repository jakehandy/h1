/* h1 - ORBIT: synths from a long way out. Slow strings, a pulsar's ticking filter, tuned dust,
 * radio from a dead probe, an endless riser, a hole. The picture is the view forward: stars coming
 * at you, something large in the middle of the screen that depends on the algorithm, and things
 * going by. Every note throws one more past you, out along the clock hand of its pitch.
 *
 *   K1 SWEEP    the filter                              the thing in the middle grows and brightens
 *   K2 SPIN     how fast everything that turns, turns   the sky rolls, beams and rings turn faster
 *   K3 SHIMMER  octaves glittering on top               more stars, and they twinkle
 *   K4 GRAVITY  notes sag and fall away when let go     a mass bends the starlight around the middle
 */
#include "internal.h"
#include "dsp.h"

/* ---- sound ------------------------------------------------------------------------------ */
static struct {
    int algo;
    f32 sweep, spin, shimmer, grav, lfo, rate, rise;
} O;
static f32 spin_ph, rise_ph;

/* the order on the knob (NEBULA first); the code below keeps the order they were written in */
static const uint8_t ORDER[NALGO] = {2, 0, 1, 3, 4, 5, 6, 7};

static void orbit_block(const Blk *b)
{
    f32 dt = b->step;
    O.algo = ORDER[b->algo];
    O.sweep = b->k[0];
    O.spin = b->k[1];
    O.shimmer = b->k[2];
    O.grav = b->k[3];
    O.rate = 0.07f * exp2fx(O.spin * 6.2f);      /* 0.07 .. 5 Hz */
    spin_ph = fractf(spin_ph + O.rate * dt);
    O.lfo = tsin(spin_ph);
    rise_ph = fractf(rise_ph + (0.03f + 0.25f * O.spin) * dt);
    O.rise = rise_ph;
}

static void orbit_note(Voice *v, const Blk *b)
{
    int i;
    (void)b;
    if (!v->u[0]) v->u[0] = 0x0B170001u + v->idx * 7919u;
    if (v->env < 0.001f) {
        for (i = 0; i < 14; i++)
            v->s[i] = 0.0f;
        for (i = 0; i < 6; i++)
            v->ph[i] = fractf(v->r1 * (f32)(i + 1) * 0.29f);
    }
    v->s[5] = 0.0f;                              /* how far it has fallen */
    v->s[6] = 1.0f;
    v->s[7] = 0.0f;
}

static void orbit_render(Voice *v, const Blk *b, f32 *out, int n)
{
    Svf fl, f2;
    f32 dt = (f32)n * b->isr, pitch = v->pitch + b->bend, f, d0, d1, d2, cut, q = 2.2f, amp = 1.0f, sh = O.shimmer, lo, bp;
    f32 p0 = v->ph[0], p1 = v->ph[1], p2 = v->ph[2], p3 = v->ph[3], p4 = v->ph[4], pw = 0.5f, idx = 0.0f, tw, gate = 1.0f;
    f32 w0 = 0.0f, w1 = 0.0f, w2 = 0.0f, wa0 = 0.0f, wa1 = 0.0f, wa2 = 0.0f;
    static const f32 TRIM[NALGO] = {0.5f, 1.0f, 1.6f, 0.55f, 0.65f, 0.75f, 0.55f, 1.0f};
    f32 trim;
    int algo = O.algo, i;

    trim = TRIM[algo];
    /* GRAVITY: a scoop into the note, and once let go it falls */
    if (!v->gate)
        v->s[5] += dt * (0.3f + 2.0f * O.grav) * O.grav;
    pitch -= v->s[5] * 14.0f + O.grav * 2.5f * exp2fx(-v->age * 12.0f);
    if (algo == 3) pitch -= v->age * (0.4f + 2.0f * O.grav);          /* COMET is always falling */
    if (algo == 7) pitch -= 12.0f + sat(v->age * 0.1f) * 5.0f * O.grav;

    /* SHIMMER: the glitter comes and goes by itself */
    v->s[9] += (rng_f(&v->u[0]) - v->s[9]) * 0.05f;
    tw = sh * (0.35f + 0.9f * v->s[9]);

    cut = 110.0f * exp2fx(O.sweep * 6.6f);
    switch (algo) {
    default:
    case 0: cut *= exp2fx(O.lfo * 0.9f); amp = sat(v->age * 4.0f); pw = 0.5f + 0.4f * tsin(spin_ph * 0.5f + v->r1); break;
    case 1: {                                    /* PULSAR: the filter jumps to a new place on every tick */
        f32 tick = fractf(spin_ph * 4.0f + v->r1 * 0.0f);
        if (tick < v->s[7]) v->s[8] = rng_f(&v->u[0]);
        v->s[7] = tick;
        cut *= exp2fx(v->s[8] * 3.2f - 1.2f);
        q = 5.0f;
        amp = 0.55f + 0.45f * exp2fx(-tick * 6.0f);
        break;
    }
    case 2: q = 26.0f; amp = sat(v->age * 2.5f); break;         /* (levelled below, once the pitch is known) */
    case 3: cut *= 1.6f; break;
    case 4: idx = (0.15f + 0.9f * O.sweep) * (0.6f + 0.4f * O.lfo); break;
    case 5: {                                    /* RADIO: keyed like a number station */
        f32 tick = spin_ph * 8.0f + v->r1 * 16.0f;
        uint32_t h = (uint32_t)(int)tick * 2654435761u;
        gate = (v->age < 0.18f || ((h >> 13) & 3u)) ? 1.0f : 0.3f;      /* a new note always gets through */
        v->s[10] += (gate - v->s[10]) * 0.2f;
        amp = v->s[10];
        break;
    }
    case 6: cut *= 1.0f + 1.5f * O.rise; break;
    case 7: amp = 0.25f + 0.75f * sat(v->age * 0.9f); cut *= 0.5f; break;
    }
    f = mtof(pitch);
    if (algo == 2)                               /* NEBULA: a band of noise an octave down is half as wide: as loud all the same */
        amp *= clampf(sqrtfx(330.0f / maxf(f, 20.0f)), 0.6f, 2.2f);
    d0 = minf(f * b->isr, 0.45f);
    d1 = minf(d0 * 1.0045f, 0.45f);
    d2 = minf(d0 * 2.0f, 0.45f);
    svf_set(&fl, clampf(cut, 40.0f, 15000.0f), q, b->isr);
    svf_set(&f2, clampf(algo == 5 ? 1400.0f * clampf(sqrtfx(f * (1.0f / 400.0f)), 0.5f, 2.0f) : f * 2.0f, 60.0f, 12000.0f), algo == 5 ? 2.0f : q, b->isr);   /* (RADIO: the set's band leans after the note, so low ones get through) */
    if (algo == 2)
        svf_set(&fl, clampf(f, 60.0f, 9000.0f), q, b->isr);
    if (algo == 6) {                             /* the riser: when it wraps, each saw hands its place to the one below */
        f32 r = O.rise;
        if (r < v->s[11] - 0.5f) { p3 = p1; p1 = p0; p0 = 0.0f; }
        v->s[11] = r;
        w0 = minf(d0 * exp2fx(r - 1.5f), 0.45f); w1 = minf(d0 * exp2fx(r - 0.5f), 0.45f); w2 = minf(d0 * exp2fx(r + 0.5f), 0.45f);
        wa0 = sin_turns(r * (0.5f / 3.0f)); wa1 = sin_turns((r + 1.0f) * (0.5f / 3.0f)); wa2 = sin_turns((r + 2.0f) * (0.5f / 3.0f));
    }

    for (i = 0; i < n; i++) {
        f32 x = 0.0f, nz, y;
        switch (algo) {
        default:
        case 0:
            x = pulse_bl(p0, d0, pw) * 0.6f + pulse_bl(p1, d1, 1.0f - pw * 0.8f) * 0.6f;
            SVF_TICK(fl, &v->s[0], x, lo, bp);
            y = lo;
            break;
        case 1:
            x = saw_bl(p0, d0) * 0.8f + saw_bl(p1, d1) * 0.4f;
            SVF_TICK(fl, &v->s[0], x, lo, bp);
            y = lo * 0.5f + bp * 0.25f;
            break;
        case 2: {                                /* NEBULA: noise that has a pitch, and a breath of sine in it */
            f32 b2;
            nz = noise_s(&v->u[0]);
            SVF_TICK(fl, &v->s[0], nz, lo, bp);
            SVF_TICK(f2, &v->s[2], nz, lo, b2);
            y = (bp + 0.6f * b2 * O.sweep) * 0.22f + 0.3f * tsin(p0) * (0.3f + 0.7f * O.sweep);
            break;
        }
        case 3:
            x = tsin(p0) * 0.8f + saw_bl(p1, d1) * 0.5f;
            SVF_TICK(fl, &v->s[0], x, lo, bp);
            y = lo;
            break;
        case 4:
            y = tsin(p0 + idx * tsin(p2) + 0.35f * idx * tsin(p3)) * 0.8f;
            break;
        case 5:
            x = tsin(p0) * tsin(p2) * 1.2f + 0.25f * noise_s(&v->u[0]) * (1.0f - O.sweep);
            SVF_TICK(f2, &v->s[2], x, lo, bp);
            y = bp * 0.9f + 0.2f * tsin(p0);
            break;
        case 6:                                  /* WARP: three saws an octave apart, all climbing, each through a window */
            x = saw_bl(p0, w0) * wa0 + saw_bl(p1, w1) * wa1 + saw_bl(p3, w2) * wa2;
            SVF_TICK(fl, &v->s[0], x * 0.75f, lo, bp);
            y = lo + 0.15f * bp;
            break;
        case 7:
            nz = noise_s(&v->u[0]);
            SVF_TICK(fl, &v->s[0], nz, lo, bp);
            y = tsin(p0 + 0.3f * tsin(p1 * 0.5f)) * 0.9f + lo * 0.5f;
            break;
        }
        if (tw > 0.001f)
            y += tw * (0.22f * tsin(p4) + 0.12f * tsin(p4 * 1.5f));
        out[i] = y * amp * trim;
        p0 += d0; if (p0 >= 1.0f) p0 -= 1.0f;
        if (algo == 6) { p0 += w0 - d0; p1 += w1; p3 += w2; }
        else { p1 += d1; p3 += d0 * 0.5f; }
        if (p1 >= 1.0f) p1 -= 1.0f;
        if (p3 >= 1.0f) p3 -= 1.0f;
        p2 += algo == 4 ? minf(d0 * 3.01f, 0.45f) : (algo == 5 ? minf(d0 * (1.5f + 2.5f * O.sweep), 0.45f) : d2);
        if (p2 >= 1.0f) p2 -= 1.0f;
        p4 += d2 * 2.0f > 0.45f ? d2 : d2 * 2.0f;                    /* two octaves up while there is room */
        if (p4 >= 2.0f) p4 -= 2.0f;
    }
    v->ph[0] = p0; v->ph[1] = p1; v->ph[2] = p2; v->ph[3] = p3; v->ph[4] = p4;
}

/* ---- picture ---------------------------------------------------------------------------- */
#define NS 64
#define NF 7
enum { ROCK, RINGED, COMET, PROBE, MAN, GATE };
static struct { f32 x, y, z; } star[NS];
static struct { f32 ang, z, rot, rotv, seed; uint8_t on, kind; } fly[NF];
static f32 roll, turn, pop, popv, idle_t, lens, big, ping[4], ping_a[4], comet_t;
static uint32_t seen;
static int ready;

static f32 hsh(uint32_t n) { n = (n ^ 61u) ^ (n >> 16); n *= 9u; n ^= n >> 4; n *= 0x27d4eb2du; n ^= n >> 15; return (f32)(n & 0xFFFFu) * (1.0f / 65536.0f); }

/* bend a point away from the middle the way a mass in front of it would */
static void lensed(f32 *x, f32 *y)
{
    f32 dx = *x - 120.0f, dy = *y - 120.0f, r2 = dx * dx + dy * dy, k;
    if (lens < 1.0f || r2 < 0.5f)
        return;
    k = 1.0f + lens * lens / r2;
    *x = 120.0f + dx * k;
    *y = 120.0f + dy * k;
}

static void spawn(int kind, f32 ang, uint32_t *rng)
{
    int i, best = 0;
    for (i = 0; i < NF; i++) {
        if (!fly[i].on) { best = i; break; }
        if (fly[i].z < fly[best].z) best = i;
    }
    fly[best].on = 1;
    fly[best].kind = (uint8_t)kind;
    fly[best].ang = ang;
    fly[best].z = 3.2f;
    fly[best].rot = rng_f(rng);
    fly[best].rotv = rng_s(rng) * 0.35f;
    fly[best].seed = rng_f(rng);
}

static void half_ellipse(f32 cx, f32 cy, f32 rx, f32 ry, f32 w, f32 a)
{
    f32 xy[2 * 13];
    int i;
    for (i = 0; i < 13; i++) {
        f32 u = (f32)i / 12.0f * 0.5f;
        xy[2 * i] = cx + cos_turns(u) * rx;
        xy[2 * i + 1] = cy + sin_turns(u) * ry;
    }
    g_poly(xy, 13, 0, w, a);
}

static void body(int kind, f32 x, f32 y, f32 r, f32 rot, f32 seed, f32 a, f32 ang)
{
    f32 xy[2 * 9], c = cos_turns(rot), s = sin_turns(rot);
    int i;
    if (r < 0.7f)
        return;
    switch (kind) {
    default:
    case ROCK:
        for (i = 0; i < 8; i++) {
            f32 rr = r * (0.65f + 0.55f * hsh((uint32_t)(seed * 9973.0f) + (uint32_t)i)), an = (f32)i / 8.0f + rot;
            xy[2 * i] = x + cos_turns(an) * rr;
            xy[2 * i + 1] = y + sin_turns(an) * rr;
        }
        g_poly(xy, 8, 1, 1.0f + r * 0.06f, a);
        if (r > 7.0f) {
            g_arc(x + c * r * 0.25f, y + s * r * 0.25f, r * 0.22f, rot, rot + 0.6f, 1.0f, a * 0.8f);
            g_arc(x - s * r * 0.3f, y + c * r * 0.3f, r * 0.13f, rot + 0.3f, rot + 0.9f, 1.0f, a * 0.8f);
        }
        break;
    case RINGED:
        g_ellipse(x, y, r * 1.9f, r * 0.5f, 1.0f + r * 0.05f, a * 0.9f);
        g_mode(G_PAINT);
        g_disc(x, y, r + 1.0f, 0.0f);
        g_mode(G_MAX);
        g_disc(x, y, r, a);
        g_mode(G_PAINT);
        half_ellipse(x, y, r * 1.9f, r * 0.5f, 3.0f + r * 0.1f, 0.0f);
        g_mode(G_MAX);
        half_ellipse(x, y, r * 1.9f, r * 0.5f, 1.0f + r * 0.05f, a);
        break;
    case COMET:                                  /* its tail streams back the way it came, and lags as it is swung round */
        g_comet(x, y, r * 0.8f, -cos_turns(ang), -sin_turns(ang), r * 6.5f, r * 1.6f * (seed - 0.5f), a, comet_t, seed);
        break;
    case PROBE: {                                /* a box, two panels, a dish */
        f32 bx = c * r * 0.35f, by = s * r * 0.35f, px = -s, py = c;
        for (i = -1; i <= 1; i += 2) {
            f32 ox = c * r * 1.05f * (f32)i, oy = s * r * 1.05f * (f32)i, hw = r * 0.55f, hh = r * 0.3f;
            xy[0] = x + ox - c * hw - px * hh; xy[1] = y + oy - s * hw - py * hh;
            xy[2] = x + ox + c * hw - px * hh; xy[3] = y + oy + s * hw - py * hh;
            xy[4] = x + ox + c * hw + px * hh; xy[5] = y + oy + s * hw + py * hh;
            xy[6] = x + ox - c * hw + px * hh; xy[7] = y + oy - s * hw + py * hh;
            g_poly(xy, 4, 1, 1.0f, a);
            g_line(x + ox - px * hh, y + oy - py * hh, x + ox + px * hh, y + oy + py * hh, 1.0f, a * 0.7f);
        }
        xy[0] = x - bx - px * r * 0.4f; xy[1] = y - by - py * r * 0.4f; xy[2] = x + bx - px * r * 0.4f; xy[3] = y + by - py * r * 0.4f;
        xy[4] = x + bx + px * r * 0.4f; xy[5] = y + by + py * r * 0.4f; xy[6] = x - bx + px * r * 0.4f; xy[7] = y - by + py * r * 0.4f;
        g_fill(xy, 4, a);
        g_line(x, y, x - px * r * 0.95f, y - py * r * 0.95f, 1.0f, a);
        g_arc(x - px * r * 1.15f, y - py * r * 1.15f, r * 0.35f, rot - 0.02f, rot + 0.52f, 1.2f, a);
        break;
    }
    case MAN: {                                  /* somebody who let go */
        f32 hx = x + c * r * 0.75f, hy = y + s * r * 0.75f, px = -s, py = c;
        g_ring(hx, hy, r * 0.42f, 1.0f + r * 0.07f, a);
        g_disc(hx + px * r * 0.1f, hy + py * r * 0.1f, r * 0.2f, a);
        g_line(x + c * r * 0.3f, y + s * r * 0.3f, x - c * r * 0.4f, y - s * r * 0.4f, r * 0.55f, a);
        g_line(x + c * r * 0.2f, y + s * r * 0.2f, x + c * r * 0.5f + px * r * 0.9f, y + s * r * 0.5f + py * r * 0.9f, r * 0.2f + 1.0f, a);
        g_line(x + c * r * 0.2f, y + s * r * 0.2f, x - c * r * 0.1f - px * r * 0.95f, y - s * r * 0.1f - py * r * 0.95f, r * 0.2f + 1.0f, a);
        g_line(x - c * r * 0.4f, y - s * r * 0.4f, x - c * r * 1.2f + px * r * 0.5f, y - s * r * 1.2f + py * r * 0.5f, r * 0.22f + 1.0f, a);
        g_line(x - c * r * 0.4f, y - s * r * 0.4f, x - c * r * 1.1f - px * r * 0.6f, y - s * r * 1.1f - py * r * 0.6f, r * 0.22f + 1.0f, a);
        break;
    }
    case GATE:
        for (i = 0; i < 6; i++) {
            f32 an = (f32)i / 6.0f + rot;
            xy[2 * i] = x + cos_turns(an) * r;
            xy[2 * i + 1] = y + sin_turns(an) * r;
        }
        g_poly(xy, 6, 1, 1.0f + r * 0.08f, a);
        break;
    }
}

static void centre(int algo, f32 a, const Vis *vis, f32 S, f32 lev)
{
    f32 t = vis->t, cx = 120.0f + 5.0f * sin_turns(t * 0.05f), cy = 120.0f + 4.0f * sin_turns(t * 0.037f + 0.2f), xy[2 * 13];
    int i, k;
    if (a < 0.02f)
        return;
    switch (algo) {
    default:
    case 0: {                                    /* DRIFT: a banded giant, its ring, and a moon going round */
        f32 mo = fractf(turn * 0.6f), mx = cx + cos_turns(mo) * S * 2.5f, my = cy + sin_turns(mo) * S * 0.7f;
        if (mo > 0.5f) g_disc(mx, my, 2.0f + S * 0.08f, a);
        g_ellipse(cx, cy, S * 2.0f, S * 0.52f, 1.6f, a * 0.9f);
        g_ellipse(cx, cy, S * 1.7f, S * 0.44f, 1.0f, a * 0.6f);
        g_mode(G_PAINT);
        g_disc(cx, cy, S + 1.5f, 0.0f);
        g_mode(G_MAX);
        g_disc(cx, cy, S, a * (0.75f + 0.25f * lev));
        g_mode(G_PAINT);
        for (i = -2; i <= 2; i++) {
            f32 by = cy + (f32)i * S * 0.3f + S * 0.08f * sin_turns(t * 0.07f + (f32)i);
            g_line(cx - S, by, cx + S, by + S * 0.05f, 1.0f + S * 0.04f * (f32)((i + 5) % 3), 0.0f);
        }
        half_ellipse(cx, cy, S * 2.0f, S * 0.52f, 4.5f, 0.0f);
        g_mode(G_MAX);
        half_ellipse(cx, cy, S * 2.0f, S * 0.52f, 1.6f, a);
        half_ellipse(cx, cy, S * 1.7f, S * 0.44f, 1.0f, a * 0.7f);
        if (mo <= 0.5f) g_disc(mx, my, 2.0f + S * 0.08f, a);
        break;
    }
    case 1:                                      /* PULSAR: two beams sweeping, a tick of light each time round */
        for (k = 0; k < 2; k++)
            for (i = 0; i < 5; i++) {
                f32 an = turn * 2.0f + (f32)k * 0.5f - (f32)i * 0.008f, wd = 0.004f + 0.0025f * (f32)i;
                xy[0] = cx; xy[1] = cy;
                xy[2] = cx + cos_turns(an - wd) * 190.0f; xy[3] = cy + sin_turns(an - wd) * 190.0f * 0.8f;
                xy[4] = cx + cos_turns(an + wd) * 190.0f; xy[5] = cy + sin_turns(an + wd) * 190.0f * 0.8f;
                g_fill(xy, 3, a * (0.55f - 0.1f * (f32)i) * (0.5f + 0.5f * S * (1.0f / 40.0f)));
            }
        g_disc(cx, cy, 3.0f + S * 0.16f + 3.0f * lev, a);
        g_ring(cx, cy, 8.0f + S * 0.3f, 1.0f, a * 0.6f);
        g_ring(cx, cy, 8.0f + S * 0.3f + 30.0f * fractf(turn * 4.0f), 1.0f, a * 0.6f * (1.0f - fractf(turn * 4.0f)));
        break;
    case 2:                                      /* NEBULA: a wheel of dust */
        for (k = 0; k < 2; k++)
            for (i = 0; i < 46; i++) {
                f32 u = (f32)i / 45.0f, an = u * 1.15f + (f32)k * 0.5f + turn * 0.4f, r = (5.0f + u * 62.0f) * (S * (1.0f / 30.0f));
                f32 jx = (hsh((uint32_t)(i * 2 + k)) - 0.5f) * 9.0f * u, jy = (hsh((uint32_t)(i * 2 + k) + 99u) - 0.5f) * 6.0f * u;
                g_disc(cx + cos_turns(an) * r + jx, cy + sin_turns(an) * r * 0.5f + jy, 0.7f + 1.6f * hsh((uint32_t)i + 7u) * (1.0f - 0.5f * u) + lev,
                       a * (1.0f - 0.6f * u) * (0.6f + 0.4f * sin_turns(t * 0.3f + (f32)i * 0.21f)));
            }
        g_disc(cx, cy, 3.0f + S * 0.12f, a);
        g_ellipse(cx, cy, S * 0.5f, S * 0.25f, 1.0f, a * 0.5f);
        break;
    case 3: {                                    /* COMET: one great one going round, its tail always away from the middle */
        f32 an = turn * 0.5f, hx = cx + cos_turns(an) * 62.0f, hy = cy + sin_turns(an) * 34.0f;
        f32 dx = hx - cx, dy = hy - cy, il = rsqrtf(dx * dx + dy * dy + 0.01f);
        dx *= il; dy *= il;
        g_glow(cx, cy, 9.0f + 3.0f * lev, a * 0.5f);         /* the star it goes round */
        g_disc(cx, cy, 2.2f, a);
        g_comet(hx, hy, 3.6f + S * 0.1f + 1.5f * lev, dx, dy, 40.0f + S * 1.7f, (18.0f + S * 0.3f) * sin_turns(an + 0.15f), a, t, 0.37f);
        break;
    }
    case 4:                                      /* QUASAR: a disc of light falling in, and two jets getting out */
        for (i = 3; i >= 0; i--)
            g_ellipse(cx, cy, S * (0.7f + 0.5f * (f32)i), S * (0.7f + 0.5f * (f32)i) * 0.28f, 1.0f + (i == 0 ? 1.5f : 0.0f), a * (1.0f - 0.2f * (f32)i));
        for (k = -1; k <= 1; k += 2) {
            f32 L = 26.0f + S * 2.2f + 14.0f * lev;
            g_line(cx, cy, cx + 3.0f * sin_turns(t * 1.3f) * (f32)k, cy + (f32)k * L, 2.2f, a);
            g_line(cx - 3.0f, cy + (f32)k * 6.0f, cx - 7.0f - 3.0f * sin_turns(t * 0.9f), cy + (f32)k * L * 0.7f, 1.0f, a * 0.6f);
            g_line(cx + 3.0f, cy + (f32)k * 6.0f, cx + 7.0f + 3.0f * sin_turns(t * 1.1f), cy + (f32)k * L * 0.7f, 1.0f, a * 0.6f);
        }
        g_mode(G_PAINT);
        g_disc(cx, cy, S * 0.3f, 0.0f);
        g_mode(G_MAX);
        g_ring(cx, cy, S * 0.3f, 1.5f, a);
        break;
    case 5:                                      /* RADIO: a probe, still calling */
        body(PROBE, cx, cy, 9.0f + S * 0.35f, turn * 0.15f, 0.3f, a, 0.0f);
        for (i = 0; i < 4; i++)
            if (ping[i] < 1.0f) {
                f32 r = 18.0f + ping[i] * 110.0f;
                g_arc(cx, cy, r, ping_a[i] - 0.07f, ping_a[i] + 0.07f, 1.4f, a * (1.0f - ping[i]));
                g_arc(cx, cy, r - 7.0f, ping_a[i] - 0.05f, ping_a[i] + 0.05f, 1.0f, a * (1.0f - ping[i]) * 0.7f);
            }
        break;
    case 6:                                      /* WARP: a tunnel of rings coming up at you */
        for (k = 0; k < 7; k++) {
            f32 u = ((f32)k + fractf(turn * 1.6f)) / 7.0f, r = 5.0f * exp2fx(u * 5.0f) * (0.7f + S * (1.0f / 60.0f));
            for (i = 0; i < 12; i++) {
                f32 an = (f32)i / 12.0f + ((k & 1) ? turn : -turn) * 0.3f;
                xy[2 * i] = cx + cos_turns(an) * r;
                xy[2 * i + 1] = cy + sin_turns(an) * r;
            }
            g_poly(xy, 12, 1, 1.0f + 1.6f * u, a * sat(u * 3.0f) * (1.0f - 0.4f * u));
        }
        break;
    case 7:                                      /* HOLE: nothing, with a bright edge */
        g_ellipse(cx, cy, S * 2.3f, S * 0.5f, 1.4f, a * 0.8f);
        g_ellipse(cx, cy, S * 1.8f, S * 0.36f, 1.0f, a * 0.5f);
        g_mode(G_PAINT);
        g_disc(cx, cy, S * 0.95f, 0.0f);
        g_mode(G_MAX);
        g_ring(cx, cy, S * 0.95f, 2.2f + 2.0f * lev, a);
        g_ring(cx, cy, S * 1.15f, 1.0f, a * 0.5f);
        g_mode(G_PAINT);
        half_ellipse(cx, cy, S * 2.3f, S * 0.5f, 4.0f, 0.0f);
        g_mode(G_MAX);
        half_ellipse(cx, cy, S * 2.3f, S * 0.5f, 1.6f, a);
        break;
    }
}

static const uint8_t FLYKIND[NALGO][4] = {
    {ROCK, RINGED, ROCK, MAN}, {ROCK, ROCK, PROBE, ROCK}, {RINGED, ROCK, RINGED, COMET}, {COMET, COMET, ROCK, COMET},
    {ROCK, GATE, ROCK, RINGED}, {PROBE, ROCK, PROBE, MAN}, {GATE, GATE, ROCK, GATE}, {MAN, ROCK, ROCK, RINGED},
};

/* (hidden) hold one note, alone, for five seconds: a call going out. Something comes to see who is calling,
 * has a look, and is gone. */
static f32 ufo;
static int ufo_done;

static APART void saucer(const Vis *vis)
{
    f32 t = vis->t, c, x, y, near;
    int i;
    {
        int held = 0;
        f32 age = 0.0f;
        for (i = 0; i < NVOICE; i++)
            if (vis->v[i].on && vis->v[i].gate && !vis->v[i].seq) {
                held++;
                age = vis->v[i].age;
            }
        if (!held)
            ufo_done = 0;
        if (held == 1 && age > 5.0f && !ufo_done) {
            ufo_done = 1;
            ufo = 1.0f;
            eng_cue(CUE_ORBIT);
        }
    }
    if (ufo <= 0.0f)
        return;
    ufo -= vis->dt * 0.19f;
    c = 1.0f - 2.0f * ufo;
    x = 120.0f + 170.0f * c * c * c;
    y = 58.0f + 30.0f * c * c + 2.5f * sin_turns(t * 1.1f);
    near = sat(1.0f - absf(c) * 2.5f);
    if (near > 0.0f)                             /* its light, on whatever is in the middle */
        g_beam(x, y + 5.0f, x, y + 62.0f, 10.0f, 44.0f, 0.4f * near, 0.0f, 1.0f);
    g_mode(G_PAINT);
    g_ellipse(x, y, 21.0f, 6.5f, 0.0f, 0.0f);
    g_disc(x, y - 4.0f, 9.0f, 0.0f);
    g_mode(G_MAX);
    g_arc(x, y - 4.0f, 9.0f, 0.5f, 1.0f, 1.4f, 1.0f);
    g_ellipse(x, y, 21.0f, 6.5f, 1.5f, 1.0f);
    for (i = 0; i < 5; i++)                      /* the lights round its rim, running */
        g_disc(x - 14.0f + 7.0f * (f32)i, y + 1.0f - (i == 0 || i == 4 ? 1.5f : 0.0f), 1.5f, ((int)(t * 9.0f) + i) % 5 ? 0.3f : 1.0f);
}

static SMALL void orbit_draw(const Vis *vis)          /* (built small: room for BLAB) */
{
    f32 dt = vis->dt, t = vis->t, sweep = vis->k[0], spin = vis->k[1], shimmer = vis->k[2], grav = vis->k[3], mt = smooth01(vis->algo_t);
    f32 lev = sat(vis->level * 3.0f), speed, rate = 0.02f + 0.5f * spin * spin, S, cr, sr;
    int i, ns = 22 + (int)(shimmer * 42.0f), algo = ORDER[vis->algo];

    if (!ready || vis->fresh) {
        uint32_t s = 0x51A45u;
        for (i = 0; i < NS; i++) {
            star[i].x = rng_s(&s); star[i].y = rng_s(&s); star[i].z = 0.08f + 0.92f * rng_f(&s);
        }
        for (i = 0; i < NF; i++)
            fly[i].on = 0;
        for (i = 0; i < 4; i++)
            ping[i] = 1.0f;
        seen = vis->notes;
        pop = popv = ufo = 0.0f;
        idle_t = 1.0f;
        ready = 1;
    }
    if (vis->notes != seen) {
        int pc = ((int)(vis->last_pitch + 0.5f) % 12 + 12) % 12, best = 0;
        seen = vis->notes;
        popv += 3.0f;
        spawn(FLYKIND[algo][vis->notes & 3u], (f32)pc / 12.0f - 0.25f, vis->rng);
        for (i = 1; i < 4; i++)
            if (ping[i] > ping[best]) best = i;
        ping[best] = 0.0f;
        ping_a[best] = (f32)pc / 12.0f - 0.25f;
        idle_t = 3.0f;
    }
    idle_t -= dt;
    if (idle_t < 0.0f) {                         /* and things go by whether you play or not */
        idle_t = 2.2f + 2.5f * rng_f(vis->rng);
        spawn(FLYKIND[algo][rng_next(vis->rng) & 3u], rng_f(vis->rng), vis->rng);
    }
    for (i = 0; i < 4; i++)
        ping[i] = minf(1.0f, ping[i] + dt * 0.7f);
    spring_z(&pop, &popv, 0.0f, 16.0f, 0.35f, dt);
    roll = fractf(roll + dt * rate * 0.12f);
    turn += dt * (0.05f + rate);
    if (turn > 4096.0f) turn -= 4096.0f;
    lens += ((grav * 34.0f + (algo == 7 ? 16.0f : 0.0f)) - lens) * minf(1.0f, dt * 5.0f);
    big += ((16.0f + 24.0f * sweep) - big) * minf(1.0f, dt * 6.0f);
    S = big * (1.0f + 0.12f * pop);
    comet_t = t;
    speed = 0.1f + 0.45f * lev + (algo == 6 ? 0.5f : 0.0f);
    cr = cos_turns(roll); sr = sin_turns(roll);

    /* the stars: each a short streak along its way out */
    for (i = 0; i < NS; i++) {
        f32 x0, y0, x1, y1, rx, ry, z0, tw, a;
        star[i].z -= speed * dt;
        if (star[i].z < 0.06f) {
            star[i].x = rng_s(vis->rng); star[i].y = rng_s(vis->rng); star[i].z = 1.0f;
        }
        if (i >= ns)
            continue;
        rx = star[i].x * cr - star[i].y * sr; ry = star[i].y * cr + star[i].x * sr;
        z0 = star[i].z + speed * 0.06f + 0.004f;
        x1 = 120.0f + rx / star[i].z * 62.0f; y1 = 120.0f + ry / star[i].z * 62.0f;
        x0 = 120.0f + rx / z0 * 62.0f; y0 = 120.0f + ry / z0 * 62.0f;
        if (x1 < -20.0f || x1 > 260.0f || y1 < -20.0f || y1 > 260.0f) {
            star[i].z = 0.0f;
            continue;
        }
        lensed(&x0, &y0);
        lensed(&x1, &y1);
        tw = 0.5f + 0.5f * sin_turns(t * (0.4f + 0.9f * hsh((uint32_t)i)) + (f32)i * 0.37f);
        a = sat(1.3f - star[i].z) * (1.0f - shimmer * 0.6f * tw);
        g_line(x0, y0, x1, y1, 1.0f + 1.2f * sat(1.0f - star[i].z * 2.0f), a);
        if (shimmer > 0.15f && (i % 5) == 0 && star[i].z < 0.6f) {       /* SHIMMER: some of them flare */
            f32 r = (2.0f + 5.0f * shimmer) * tw * tw;
            g_line(x1 - r, y1, x1 + r, y1, 1.0f, a * tw);
            g_line(x1, y1 - r, x1, y1 + r, 1.0f, a * tw);
        }
    }
    if (lens > 3.0f)                             /* GRAVITY: the ring the bent light makes */
        g_ring(120.0f, 120.0f, lens, 1.0f, 0.35f * sat((lens - 3.0f) * 0.1f));

    /* the thing in the middle */
    if (mt < 1.0f)
        centre(ORDER[vis->algo_prev], 1.0f - mt, vis, S * (1.0f - 0.5f * mt), lev);
    centre(algo, mt, vis, S * (0.5f + 0.5f * mt), lev);

    /* what goes by */
    for (i = 0; i < NF; i++) {
        f32 r, x, y;
        if (!fly[i].on)
            continue;
        fly[i].z -= dt * (0.5f + 0.9f * lev) * (0.4f + fly[i].z * 0.5f);
        fly[i].rot += fly[i].rotv * dt * (0.5f + 4.0f * spin);
        fly[i].ang += dt * grav * 0.22f / (0.3f + fly[i].z);              /* the mass swings them round */
        r = 36.0f / fly[i].z;
        x = 120.0f + cos_turns(fly[i].ang) * r;
        y = 120.0f + sin_turns(fly[i].ang) * r;
        if (fly[i].z < 0.16f || x < -70.0f || x > 310.0f || y < -70.0f || y > 310.0f) {
            fly[i].on = 0;
            continue;
        }
        body(fly[i].kind, x, y, 5.5f / fly[i].z, fly[i].rot, fly[i].seed, sat((3.2f - fly[i].z) * 2.0f), fly[i].ang);
    }
    saucer(vis);
}

const InstDef inst_orbit = {
    "ORBIT",
    {206, 224, 255},
    {"SWEEP", "SPIN", "SHIMMER", "GRAVITY"},
    {"NEBULA", "DRIFT", "PULSAR", "COMET", "QUASAR", "RADIO", "WARP", "HOLE"},
    {{55, 30, 35, 15}, 0, {34, 60, 85, 72}, {0, 55, 0, 0}, {30, 5, 66, 52}, {0, 3, 1, 60}, {0, 0, 0}},
    0.3f,
    orbit_block, orbit_note, orbit_render, 0, orbit_draw,
};
