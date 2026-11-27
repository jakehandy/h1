/* h1 - TURBO: the eighties, at dusk, at speed. Polysynth saws, an FM electric piano, slap bass,
 * synth brass, all of it a little warped the way an old tape is. The picture is the drive: a road
 * running off to a slatted sun over a wire-mesh world, things going by on both sides, and a car
 * that goes where the tune goes: low notes pull it left, high notes right, and the road bends
 * after it.
 *
 *   K1 CHORUS  the oscillators spread apart             more lanes on the road
 *   K2 TAPE    wow, flutter, a tired machine            the picture tears and slips like a worn tape
 *   K3 GLARE   the filter opens                         the sun grows and burns through its slats
 *   K4 DRIVE   pushed into the red                      the whole world goes by faster
 */
#include "internal.h"
#include "dsp.h"

/* ---- sound ------------------------------------------------------------------------------ */
static struct {
    int algo;
    f32 chorus, tape, glare, drive, wow, step;
} T;
static f32 wow_ph, flut_ph, step_ph;

static void turbo_block(const Blk *b)
{
    f32 dt = b->step;
    T.algo = b->algo;
    T.chorus = b->k[0];
    T.tape = b->k[1];
    T.glare = b->k[2];
    T.drive = b->k[3];
    wow_ph = fractf(wow_ph + 0.55f * dt);
    flut_ph = fractf(flut_ph + 6.3f * dt);
    T.wow = T.tape * T.tape * (0.55f * tsin(wow_ph) + 0.12f * tsin(flut_ph)) - 0.25f * T.tape * T.tape;
    step_ph = fractf(step_ph + b->bpm * (1.0f / 15.0f) * dt);         /* sixteenths, for the arcade's echo */
    T.step = step_ph;
}

static void turbo_note(Voice *v, const Blk *b)
{
    int i;
    (void)b;
    if (!v->u[0]) v->u[0] = 0x7B0B0001u + v->idx * 7919u;
    if (v->env < 0.001f) {
        for (i = 0; i < 4; i++)
            v->ph[i] = fractf(v->r1 * (f32)(i + 1) * 0.37f);
        v->s[0] = v->s[1] = 0.0f;
    }
    v->s[2] = 1.0f;                              /* the pluck of the filter, or of the FM */
    v->s[3] = 0.0f;
}

static void turbo_render(Voice *v, const Blk *b, f32 *out, int n)
{
    Svf fl;
    f32 dt = (f32)n * b->isr, pitch = v->pitch + b->bend + T.wow, f, d0, d1, d2, det = 0.05f + 0.3f * T.chorus, pk = v->s[2];
    f32 cut, q = 1.1f, drive = 1.0f + 5.0f * T.drive * T.drive, comp = 1.0f / (1.0f + 1.6f * T.drive), amp = 1.0f, lo, bp;
    f32 p0 = v->ph[0], p1 = v->ph[1], p2 = v->ph[2], fmi = 0.0f, pw = 0.5f;
    int algo = T.algo, i;

    switch (algo) {
    default: break;
    case 2: pitch -= 12.0f; break;
    case 3: pitch -= 0.5f * exp2fx(-v->age * 18.0f); break;          /* BRASS scoops up into the note */
    case 4: amp = sat(v->age * 3.5f); det *= 1.8f; break;            /* MALL: slow, wide, half asleep */
    case 5:
        v->s[3] = fractf(v->s[3] + 5.4f * dt);
        pitch += 0.22f * tsin(v->s[3]) * sat(v->age * 3.0f - 0.6f);
        pw = 0.5f + 0.38f * tsin(v->s[3] * 0.23f + v->r2);
        break;
    case 6:                                      /* ARCADE: the note comes back on every sixteenth, an octave up, fading */
        amp = 0.45f + 1.1f * exp2fx(-fractf(T.step + 0.001f) * 5.0f) * exp2fx(-v->age * 1.2f);
        if (((int)(v->age * b->bpm * (1.0f / 15.0f))) & 1)
            pitch += 12.0f;
        break;
    }
    f = mtof(pitch);
    d0 = minf(f * b->isr, 0.45f);
    d1 = minf(d0 * exp2fx(det * (1.0f / 12.0f)), 0.45f);
    d2 = minf(d0 * exp2fx(-det * 0.8f * (1.0f / 12.0f)), 0.45f);
    cut = 180.0f * exp2fx(T.glare * 6.2f);
    switch (algo) {
    default: cut *= 1.0f + 5.0f * pk; break;
    case 3: cut *= 0.6f + 5.0f * sat(v->age * 11.0f) * (0.35f + 0.65f * pk); q = 1.5f; break;
    case 4: cut *= 0.55f; break;
    case 5: cut *= 2.0f; q = 2.4f; break;
    case 1: case 2: case 7: cut = 900.0f * exp2fx(T.glare * 4.2f); break;
    }
    svf_set(&fl, minf(cut, 15000.0f), q, b->isr);
    fmi = (algo == 2 ? 0.9f : (algo == 7 ? 0.35f : 0.5f)) * (0.25f + 1.4f * T.glare) * VEL_FM(v);

    for (i = 0; i < n; i++) {
        f32 x;
        switch (algo) {
        default:
        case 0: case 3: case 4:
            x = (saw_bl(p0, d0) + saw_bl(p1, d1) + saw_bl(p2, d2)) * 0.55f;
            break;
        case 1:                                  /* KEYS: a tine (a bright bar that dies fast) on a soft body */
            x = tsin(p0 + fmi * (0.5f + 2.5f * pk) * tsin(p1)) * 0.9f + 0.25f * pk * tsin(p2 + 0.3f * tsin(p1));
            break;
        case 2:                                  /* SLAP: the index snaps shut */
            x = tsin(p0 + fmi * (0.3f + 3.6f * pk * pk) * tsin(p1 + 0.25f * tsin(p1))) * 1.1f;
            break;
        case 5:
            x = pulse_bl(p0, d0, pw) * 0.7f + saw_bl(p1, d1) * 0.45f;
            break;
        case 6:
            x = pulse_bl(p0, d0, 0.5f) * 0.6f + pulse_bl(p1, d1, 0.25f) * 0.4f;
            break;
        case 7:                                  /* NIGHT: a glass bell over a soft saw */
            x = tsin(p0 + fmi * (0.4f + 1.6f * pk) * tsin(p2)) * 0.8f + saw_bl(p1, d1) * 0.3f * sat(v->age * 2.0f);
            break;
        }
        x = softclip(x * drive) * comp;
        SVF_TICK(fl, &v->s[0], x, lo, bp);
        (void)bp;
        out[i] = lo * amp;
        p0 += d0; if (p0 >= 1.0f) p0 -= 1.0f;
        if (algo == 1) {                         /* (the piano's modulators: the same pitch, and fourteen times it) */
            p1 += d0; if (p1 >= 1.0f) p1 -= 1.0f;
            p2 += minf(d0 * 14.0f, 0.45f); if (p2 >= 1.0f) p2 -= 1.0f;
        } else if (algo == 2) {
            p1 += d0; if (p1 >= 1.0f) p1 -= 1.0f;
        } else {
            p1 += d1; if (p1 >= 1.0f) p1 -= 1.0f;
            p2 += algo == 7 ? minf(d0 * 3.5f, 0.45f) : d2; if (p2 >= 1.0f) p2 -= 1.0f;
        }
    }
    v->ph[0] = p0; v->ph[1] = p1; v->ph[2] = p2;
    v->s[2] = pk * (1.0f - onepole_k(algo == 2 ? 0.07f : (algo == 1 ? 0.22f : (algo == 7 ? 0.9f : 0.16f)), b->sr / (f32)n));
}

/* ---- picture ---------------------------------------------------------------------------- */
#define HZ 126.0f
#define DEP 124.0f
#define NOBJ 12
static struct { f32 z; int8_t side; uint8_t lit; f32 seed; } ob[NOBJ];
static f32 dist, curve, curvev, steer, steerv, bounce, bouncev, slat, flash, tear_y = 60.0f;
static uint32_t seen;

static f32 hsh(uint32_t n) { n = (n ^ 61u) ^ (n >> 16); n *= 9u; n ^= n >> 4; n *= 0x27d4eb2du; n ^= n >> 15; return (f32)(n & 0xFFFFu) * (1.0f / 65536.0f); }
static f32 road_cx(f32 y) { f32 u = (250.0f - y) * (1.0f / DEP); return 120.0f + curve * 78.0f * u * u; }
static f32 road_hw(f32 y) { return 2.0f + (y - HZ) * 0.8f; }

/* something beside the road: its foot at (x, y), s = 1 when it is right beside you */
static void thing(int kind, f32 x, f32 y, f32 s, f32 a, f32 seed, f32 t, int lit)
{
    f32 h = 118.0f * s, w = 1.0f + 2.2f * s;
    int i;
    if (h < 2.0f)
        return;
    switch (kind) {
    default:
    case 0: {                                    /* a palm: a leaning trunk and fronds that hang */
        f32 lean = (seed - 0.5f) * 0.5f * h, tx = x + lean, ty = y - h;
        g_line(x, y, x + lean * 0.35f, y - h * 0.5f, w * 1.5f, a);
        g_line(x + lean * 0.35f, y - h * 0.5f, tx, ty, w * 1.2f, a);
        for (i = 0; i < 7; i++) {                /* each frond arches up and out, then hangs */
            f32 an = -0.5f + (f32)i / 6.0f * 0.5f + 0.012f * sin_turns(t * 0.4f + (f32)i * 0.2f + seed);
            f32 c = cos_turns(an), sn = sin_turns(an), L = h * (0.34f + 0.1f * absf(c));
            f32 x1 = tx + c * L * 0.45f, y1 = ty + sn * L * 0.5f - h * 0.03f, x2 = tx + c * L * 0.85f, y2 = ty + sn * L * 0.45f + h * 0.03f;
            f32 x3 = tx + c * L * 1.05f, y3 = ty + sn * L * 0.2f + h * 0.13f;
            g_line(tx, ty, x1, y1, w * 1.5f, a);
            g_line(x1, y1, x2, y2, w * 1.2f, a);
            g_line(x2, y2, x3, y3, w * 0.8f, a);
        }
        g_disc(tx, ty + h * 0.02f, w * 1.6f, a);
        break;
    }
    case 1: {                                    /* a beach umbrella */
        f32 r = h * 0.3f, cy = y - h * 0.55f;
        g_line(x, y, x, cy - r * 0.6f, w, a);
        g_arc(x, cy, r, 0.5f, 1.0f, w * 1.3f, a);
        g_line(x - r, cy, x + r, cy, w, a);
        for (i = -1; i <= 1; i++)
            g_line(x, cy - r, x + (f32)i * r * 0.5f, cy, w * 0.7f, a * 0.8f);
        break;
    }
    case 2: {                                    /* a tower block, lit windows */
        f32 bw = h * (0.2f + 0.14f * seed), bh = h * (0.7f + 0.6f * seed);
        g_line(x - bw, y, x - bw, y - bh, w, a); g_line(x + bw, y, x + bw, y - bh, w, a);
        g_line(x - bw, y - bh, x + bw, y - bh, w, a);
        if (s > 0.12f)
            for (i = 0; i < 12; i++)
                if (hsh((uint32_t)(seed * 977.0f) + (uint32_t)i) > 0.4f)
                    g_rect(x - bw * 0.6f + (f32)(i % 3) * bw * 0.45f, y - bh * 0.9f + (f32)(i / 3) * bh * 0.21f, bw * 0.28f, bh * 0.09f, a * 0.9f);
        g_line(x, y - bh, x, y - bh - h * 0.14f, w * 0.7f, a);
        break;
    }
    case 3: {                                    /* a bend sign: chevrons */
        f32 bw = h * 0.22f, top = y - h * 0.62f, bot = y - h * 0.34f, mid = (top + bot) * 0.5f;
        g_line(x, y, x, bot, w, a);
        g_line(x - bw, top, x + bw, top, w, a); g_line(x - bw, bot, x + bw, bot, w, a);
        g_line(x - bw, top, x - bw, bot, w, a); g_line(x + bw, top, x + bw, bot, w, a);
        for (i = -1; i <= 1; i += 2) {
            f32 cx = x + (f32)i * bw * 0.4f, dir = curve >= 0.0f ? 1.0f : -1.0f;
            g_line(cx - dir * bw * 0.22f, top + 2.0f * s, cx + dir * bw * 0.22f, mid, w, a);
            g_line(cx + dir * bw * 0.22f, mid, cx - dir * bw * 0.22f, bot - 2.0f * s, w, a);
        }
        break;
    }
    case 4: {                                    /* a column from somebody's idea of Greece */
        f32 cw = h * 0.085f, top = y - h * 0.8f;
        for (i = -2; i <= 2; i++)
            g_line(x + (f32)i * cw * 0.5f, y - h * 0.05f, x + (f32)i * cw * 0.5f, top + h * 0.05f, w * 0.7f, a * (i == -2 || i == 2 ? 1.0f : 0.6f));
        g_rect(x - cw * 1.6f, top, cw * 3.2f, h * 0.05f, a); g_rect(x - cw * 1.6f, y - h * 0.05f, cw * 3.2f, h * 0.05f, a);
        g_arc(x - cw * 1.6f, top + h * 0.05f, h * 0.03f, 0.0f, 1.0f, w * 0.7f, a);
        g_arc(x + cw * 1.6f, top + h * 0.05f, h * 0.03f, 0.0f, 1.0f, w * 0.7f, a);
        break;
    }
    case 5: {                                    /* a saguaro */
        f32 ah = h * (0.28f + 0.2f * seed);
        g_line(x, y, x, y - h * 0.75f, w * 2.4f, a);
        g_line(x, y - h * 0.3f, x - h * 0.16f, y - h * 0.3f, w * 2.0f, a); g_line(x - h * 0.16f, y - h * 0.3f, x - h * 0.16f, y - h * 0.3f - ah, w * 2.0f, a);
        g_line(x, y - h * 0.42f, x + h * 0.14f, y - h * 0.42f, w * 2.0f, a); g_line(x + h * 0.14f, y - h * 0.42f, x + h * 0.14f, y - h * 0.42f - ah * 0.7f, w * 2.0f, a);
        break;
    }
    case 6: {                                    /* a wire pyramid */
        f32 bw = h * 0.42f, ty = y - h * 0.62f, sh = bw * 0.5f * (seed - 0.5f);
        g_line(x - bw, y, x, ty, w, a); g_line(x + bw, y, x, ty, w, a); g_line(x - bw, y, x + bw, y, w, a);
        g_line(x + sh, y - h * 0.08f, x, ty, w * 0.7f, a * 0.8f);
        g_line(x - bw, y, x + sh, y - h * 0.08f, w * 0.7f, a * 0.6f); g_line(x + bw, y, x + sh, y - h * 0.08f, w * 0.7f, a * 0.6f);
        for (i = 1; i < 4; i++) {
            f32 u = (f32)i * 0.25f;
            g_line(x - bw * (1.0f - u), y - (y - ty) * u, x + bw * (1.0f - u), y - (y - ty) * u, w * 0.6f, a * 0.5f);
        }
        break;
    }
    case 7: {                                    /* a street lamp */
        f32 top = y - h * 0.86f, dir = x < 120.0f ? 1.0f : -1.0f, lx = x + dir * h * 0.2f;
        g_line(x, y, x, top, w, a);
        g_line(x, top, lx, top, w, a);
        g_disc(lx, top + 2.0f * s, 1.0f + 4.0f * s, a);
        g_ring(lx, top + 2.0f * s, 3.0f + 9.0f * s, 1.0f, a * 0.5f);
        break;
    }
    }
    if (lit) {                                   /* the one a note just called up: it carries a star */
        f32 r = 3.0f + 9.0f * s, yy = y - h - 4.0f - 9.0f * s;
        g_line(x - r, yy, x + r, yy, w, a); g_line(x, yy - r, x, yy + r, w, a);
        g_line(x - r * 0.5f, yy - r * 0.5f, x + r * 0.5f, yy + r * 0.5f, w * 0.7f, a); g_line(x - r * 0.5f, yy + r * 0.5f, x + r * 0.5f, yy - r * 0.5f, w * 0.7f, a);
    }
}

/* (hidden) play fast enough for long enough and somebody notices: a mirror comes down, and what is in it
 * has a light bar going */
static f32 fast, fast_t, cop;
static int cop_done;
static uint32_t cop_seen;

static APART void mirror(const Vis *vis)
{
    f32 u, y, q[8];
    int i, side = (int)(vis->t * 5.0f) & 1;
    if (vis->hand != cop_seen) {
        cop_seen = vis->hand;
        if (fast_t > 3.0f)                       /* (ready again once the playing has stopped) */
            cop_done = 0;
        fast_t = 0.0f;
        fast += 1.0f;
        if (fast > 14.0f && !cop_done && EGGS_ON) {         /* a dozen notes a second, kept up for a couple of seconds */
            cop_done = 1;
            cop = 1.0f;
            eng_cue(CUE_TURBO);
        }
    }
    fast_t += vis->dt;
    fast -= fast * minf(1.0f, vis->dt * 0.8f);
    if (cop <= 0.0f)
        return;
    cop -= vis->dt * 0.3f;
    u = cop > 0.88f ? (1.0f - cop) * 8.4f : sat(cop * 6.0f);
    y = -30.0f + 38.0f * smooth01(u);
    g_line(120.0f, -2.0f, 120.0f, y, 2.0f, 0.9f);
    g_mode(G_PAINT);
    g_rect(78.0f, y, 84.0f, 24.0f, 0.0f);
    g_mode(G_MAX);
    g_line(80.0f, y, 160.0f, y, 1.6f, 1.0f); g_line(80.0f, y + 24.0f, 160.0f, y + 24.0f, 1.6f, 1.0f);
    g_arc(80.0f, y + 12.0f, 12.0f, 0.25f, 0.75f, 1.6f, 1.0f); g_arc(160.0f, y + 12.0f, 12.0f, 0.75f, 1.25f, 1.6f, 1.0f);
    q[0] = 104.0f; q[1] = y + 21.0f; q[2] = 136.0f; q[3] = y + 21.0f; q[4] = 132.0f; q[5] = y + 11.0f; q[6] = 108.0f; q[7] = y + 11.0f;
    g_poly(q, 4, 1, 1.2f, 0.9f);                                /* the car behind: nose on */
    g_line(111.0f, y + 11.0f, 113.0f, y + 6.0f, 1.2f, 0.9f); g_line(129.0f, y + 11.0f, 127.0f, y + 6.0f, 1.2f, 0.9f);
    g_line(113.0f, y + 6.0f, 127.0f, y + 6.0f, 1.2f, 0.9f);
    g_disc(108.5f, y + 16.0f, 2.2f, 1.0f); g_disc(131.5f, y + 16.0f, 2.2f, 1.0f);
    for (i = 0; i < 2; i++) {                    /* its bar: one side, then the other, and the light it throws */
        f32 x = i ? 121.0f : 113.0f, on = i == side ? 1.0f : 0.15f;
        g_rect(x, y + 2.5f, 6.0f, 3.0f, on);
        if (i == side)
            g_glow(x + 3.0f + (i ? 18.0f : -18.0f), y + 8.0f, 26.0f, 0.5f);
    }
}

static SMALL void turbo_draw(const Vis *vis)
{
    f32 dt = vis->dt, t = vis->t, chorus = vis->k[0], tape = vis->k[1], glare = vis->k[2], drive = vis->k[3], mt = smooth01(vis->algo_t);
    f32 lev = sat(vis->level * 3.0f), speed = (1.4f + 6.5f * drive * (0.4f + 0.6f * drive)) * (0.85f + 0.3f * lev), want, par, sunx, R, cy;
    int i, k, algo = mt < 0.5f ? vis->algo_prev : vis->algo, night = algo == 7 || algo == 6, lanes;
    f32 sa = absf(2.0f * mt - 1.0f);

    if (vis->fresh) {
        seen = vis->notes;
        for (i = 0; i < NOBJ; i++) {
            ob[i].z = 1.2f + (f32)i * 1.3f;
            ob[i].side = (i & 1) ? 1 : -1;
            ob[i].lit = 0;
            ob[i].seed = hsh((uint32_t)i + 5u);
        }
        curve = curvev = steer = steerv = bounce = bouncev = flash = fast = cop = 0.0f;
        cop_done = 0;
        cop_seen = vis->hand;
    }
    want = clampf((vis->last_pitch - 65.0f) * (1.0f / 13.0f), -1.0f, 1.0f);
    if (vis->notes != seen) {
        int best = -1;
        seen = vis->notes;
        flash = 1.0f;
        bouncev -= 26.0f;
        for (i = 0; i < NOBJ; i++)               /* the furthest thing on the note's side of the road lights up */
            if (!ob[i].lit && ob[i].z > 5.0f && (best < 0 || ob[i].z > ob[best].z) && (ob[i].side > 0) == (want >= 0.0f))
                best = i;
        if (best >= 0)
            ob[best].lit = 1;
    }
    spring(&curve, &curvev, want * 0.85f, 1.3f, dt);
    spring(&steer, &steerv, want, 5.0f, dt);
    spring_z(&bounce, &bouncev, 0.0f, 22.0f, 0.3f, dt);
    flash = maxf(0.0f, flash - dt * 2.5f);
    dist += speed * dt;
    slat = fractf(slat + dt * 0.11f);
    par = -curve * 16.0f;
    sunx = 120.0f + par * 1.6f;
    R = 30.0f + 22.0f * glare + 3.0f * flash;
    cy = HZ - 22.0f - 8.0f * glare;

    /* the sky */
    if (night) {
        for (i = 0; i < 26; i++) {
            f32 sx = fractf(hsh((uint32_t)i + 9u) + par * 0.002f) * 240.0f, sy = 6.0f + hsh((uint32_t)i + 77u) * (HZ - 40.0f);
            g_disc(sx, sy, 0.9f, 0.3f + 0.5f * (0.5f + 0.5f * sin_turns(t * 0.3f + (f32)i * 0.13f)));
        }
    }
    if (algo == 6) {                             /* ARCADE has no sun: the grid goes over the sky as well */
        for (k = 0; k < 9; k++) {
            f32 z = 0.95f + ((f32)(k + 1) - fractf(dist * 0.5f)) * 1.1f, y = HZ - DEP / z;
            if (y > -2.0f)
                g_line(0.0f, y, 240.0f, y, 1.0f, sat(1.5f / z) * 0.7f * sa);
        }
        for (i = -6; i <= 6; i++)
            g_line(120.0f + par + (f32)i * 9.0f, HZ - 2.0f, 120.0f + (f32)i * 62.0f, -4.0f, 1.0f, 0.3f * sa);
    } else {
        g_disc(sunx, cy, R, sa);
        if (algo == 7) {                         /* NIGHT: a moon, the lit side only */
            g_mode(G_PAINT);
            g_disc(sunx + R * 0.45f, cy - R * 0.2f, R * 0.92f, 0.0f);
            g_mode(G_MAX);
        } else {
            g_mode(G_PAINT);                     /* the slats: they sink through the sun and fatten as they go */
            for (i = 0; i < 7; i++) {
                f32 u = ((f32)i + slat) / 7.0f, y = cy - R * 0.15f + R * 1.2f * u, th = (1.0f + 6.5f * u) * (1.0f - 0.6f * glare);
                g_rect(sunx - R - 2.0f, y, 2.0f * R + 4.0f, th, 0.0f);
            }
            g_mode(G_MAX);
            for (i = 1; i <= 2; i++)             /* GLARE: haze around it */
                g_ring(sunx, cy, R + 5.0f * (f32)i + 2.0f * sin_turns(t * 0.2f + (f32)i * 0.3f), 1.0f, sat(glare * 1.6f - 0.3f * (f32)i) * 0.6f * sa);
        }
    }
    /* what stands on the horizon: wire mountains, mesas, or a skyline */
    for (k = 0; k < 2; k++) {
        f32 px = 0.0f, py = 0.0f;
        for (i = 0; i <= 9; i++) {
            f32 u = (f32)i / 9.0f, x = (k ? 148.0f : -6.0f) + 98.0f * u + par * 0.6f, hh = hsh((uint32_t)(i + k * 16 + algo * 3)), y;
            f32 env = sin_turns(u * 0.5f) * (0.5f + 0.5f * (k ? u : 1.0f - u));
            if (algo == 2 || algo == 7)
                y = HZ - 6.0f - (f32)(int)(hh * 5.0f) * 6.0f * (0.4f + env);
            else if (algo == 5)
                y = HZ - (hh > 0.5f ? 20.0f : 5.0f) * (0.3f + env);
            else
                y = HZ - (5.0f + 34.0f * hh) * env * (algo == 3 ? 1.5f : 1.0f);
            if (algo == 1 || algo == 6)
                y = HZ;
            if (i) {
                if (algo == 2 || algo == 7 || algo == 5) {
                    g_line(px, py, x, py, 1.2f, 0.8f * sa);
                    g_line(x, py, x, y, 1.2f, 0.8f * sa);
                } else
                    g_line(px, py, x, y, 1.3f, 0.85f * sa);
                if (algo != 1 && algo != 6)
                    g_line(x, y, x + (x - 120.0f) * 0.05f, HZ, 1.0f, 0.28f * sa);
            }
            px = x; py = y;
        }
    }
    g_mode(G_PAINT);
    g_rect(-1.0f, HZ, 242.0f, 242.0f - HZ, 0.0f);
    g_mode(G_MAX);
    g_line(0.0f, HZ, 240.0f, HZ, 1.2f, 0.9f);

    /* the ground: a grid that runs at you, and lines that run to the vanishing point */
    for (k = 0; k < 11; k++) {
        f32 z = 0.95f + ((f32)(k + 1) - fractf(dist)) * 1.0f, y = HZ + DEP / z;
        if (y < 241.0f)
            g_line(0.0f, y, 240.0f, y, 1.0f, sat(1.6f / z) * 0.55f);
    }
    for (i = -9; i <= 9; i++) {
        f32 vx = road_cx(HZ);
        if (i == 0) continue;
        g_line(vx + (f32)i * 3.0f, HZ + 1.0f, 120.0f + (f32)i * 58.0f - curve * 20.0f, 241.0f, 1.0f, 0.34f);
    }
    /* the road: cut out of the grid, then its edges, rumble strips and lanes put back */
    g_mode(G_PAINT);
    for (i = (int)HZ + 1; i < 240; i++)
        g_rect(road_cx((f32)i) - road_hw((f32)i), (f32)i, 2.0f * road_hw((f32)i), 1.0f, 0.0f);
    g_mode(G_MAX);
    {
        f32 xl[2 * 16], xr[2 * 16];
        for (i = 0; i < 16; i++) {
            f32 u = (f32)i / 15.0f, y = HZ + 1.0f + (239.0f - HZ) * u * u;
            xl[2 * i] = road_cx(y) - road_hw(y); xl[2 * i + 1] = y;
            xr[2 * i] = road_cx(y) + road_hw(y); xr[2 * i + 1] = y;
        }
        g_poly(xl, 16, 0, 1.6f, 1.0f);
        g_poly(xr, 16, 0, 1.6f, 1.0f);
    }
    lanes = 1 + (int)(chorus * 2.99f);
    for (k = 0; k < 8; k++) {
        f32 z0 = 0.9f + ((f32)k - fractf(dist * 0.5f)) * 2.0f + 2.0f, z1 = z0 - 0.75f, y0, y1, q[8];
        int ln;
        if (z1 < 0.92f) z1 = 0.92f;
        if (z0 <= z1) continue;
        y0 = HZ + DEP / z0; y1 = HZ + DEP / z1;
        for (ln = 0; ln < lanes + 2; ln++) {     /* (the last two are the rumble strips on the edges) */
            f32 off, hw0, hw1, a;
            if (ln < lanes) {
                off = lanes == 1 ? 0.0f : ((f32)ln / (f32)(lanes - 1) - 0.5f) * (lanes == 2 ? 0.7f : 1.1f);
                hw0 = road_hw(y0) * 0.03f; hw1 = road_hw(y1) * 0.03f; a = 0.95f;
            } else {
                off = ln == lanes ? -0.93f : 0.93f;
                hw0 = road_hw(y0) * 0.055f; hw1 = road_hw(y1) * 0.055f; a = 0.7f;
            }
            q[0] = road_cx(y0) + off * road_hw(y0) - hw0; q[1] = y0;
            q[2] = road_cx(y0) + off * road_hw(y0) + hw0; q[3] = y0;
            q[4] = road_cx(y1) + off * road_hw(y1) + hw1; q[5] = y1;
            q[6] = road_cx(y1) + off * road_hw(y1) - hw1; q[7] = y1;
            g_fill(q, 4, a * sat(3.0f / z0));
        }
    }

    /* what goes by: far things first */
    for (k = 0; k < NOBJ; k++) {
        ob[k].z -= speed * dt;
        if (ob[k].z < 0.86f) {
            ob[k].z += (f32)NOBJ * 1.3f;
            ob[k].lit = 0;
            ob[k].seed = hsh((uint32_t)(dist * 31.0f) + (uint32_t)k * 131u);
        }
    }
    for (i = 0; i < NOBJ; i++) {
        int j, idx = -1;
        f32 y, s, x;
        for (j = 0; j < NOBJ && idx < 0; j++) {  /* the i-th furthest */
            int rank = 0;
            for (k = 0; k < NOBJ; k++)
                if (ob[k].z > ob[j].z || (ob[k].z == ob[j].z && k < j))
                    rank++;
            if (rank == i)
                idx = j;
        }
        if (idx < 0)
            continue;
        y = HZ + DEP / ob[idx].z;
        s = 1.0f / ob[idx].z;
        x = road_cx(y) + (f32)ob[idx].side * (road_hw(y) + 30.0f * s + 60.0f * s * ob[idx].seed * (algo == 6 || algo == 2 ? 1.0f : 0.3f));
        thing(algo, x, y, s, sat(2.6f * s + 0.15f) * sa * (ob[idx].lit ? 1.0f : 0.85f), ob[idx].seed, t, ob[idx].lit);
    }

    /* the car */
    {
        f32 x = 120.0f + steer * 46.0f, y = 216.0f + bounce * 0.25f, q[8], lean = clampf(steerv * 0.9f, -4.0f, 4.0f);
        g_mode(G_PAINT);
        g_rect(x - 34.0f, y - 20.0f, 68.0f, 28.0f, 0.0f);
        g_mode(G_MAX);
        g_rect(x - 32.0f, y - 3.0f, 10.0f, 9.0f, 0.75f);                           /* tyres */
        g_rect(x + 22.0f, y - 3.0f, 10.0f, 9.0f, 0.75f);
        q[0] = x - 32.0f; q[1] = y; q[2] = x + 32.0f; q[3] = y; q[4] = x + 29.0f + lean; q[5] = y - 17.0f; q[6] = x - 29.0f + lean; q[7] = y - 17.0f;
        g_fill(q, 4, 1.0f);                                                          /* the body */
        q[0] = x - 23.0f + lean; q[1] = y - 17.0f; q[2] = x + 23.0f + lean; q[3] = y - 17.0f; q[4] = x + 16.0f + lean * 1.6f; q[5] = y - 30.0f; q[6] = x - 16.0f + lean * 1.6f; q[7] = y - 30.0f;
        g_fill(q, 4, 1.0f);                                                          /* the cabin */
        g_line(x - 27.0f + lean * 1.6f, y - 33.0f, x + 27.0f + lean * 1.6f, y - 33.0f, 2.6f, 1.0f);    /* the wing */
        g_line(x - 21.0f + lean * 1.6f, y - 33.0f, x - 20.0f + lean, y - 26.0f, 2.0f, 1.0f);
        g_line(x + 21.0f + lean * 1.6f, y - 33.0f, x + 20.0f + lean, y - 26.0f, 2.0f, 1.0f);
        g_mode(G_PAINT);
        q[0] = x - 19.0f + lean; q[1] = y - 19.0f; q[2] = x + 19.0f + lean; q[3] = y - 19.0f; q[4] = x + 13.5f + lean * 1.6f; q[5] = y - 28.0f; q[6] = x - 13.5f + lean * 1.6f; q[7] = y - 28.0f;
        g_fill(q, 4, 0.0f);                                                          /* the back window */
        g_rect(x - 28.0f + lean * 0.8f, y - 13.5f, 56.0f, 5.5f, 0.0f);               /* the light bar's slot */
        g_rect(x - 30.0f, y - 4.5f, 60.0f, 1.2f, 0.0f);                              /* the bumper line */
        g_mode(G_MAX);
        for (i = 0; i < 6; i++) {                /* the light bar: it runs outward from the middle with the music */
            f32 on = sat(lev * 6.0f - (f32)(i < 3 ? 2 - i : i - 3) * 1.6f + 0.35f) ;
            g_rect(x - 27.0f + lean * 0.8f + (f32)i * 9.2f, y - 12.5f, 7.6f, 3.5f, 0.35f + 0.65f * on);
        }
        g_line(x - 6.0f + lean, y - 21.0f, x - 6.0f + lean * 1.4f, y - 25.0f, 3.0f, 0.8f);       /* two headrests */
        g_line(x + 7.0f + lean, y - 21.0f, x + 7.0f + lean * 1.4f, y - 25.0f, 3.0f, 0.8f);
        if (drive > 0.3f)                        /* DRIVE: flame in the pipes */
            for (i = -1; i <= 1; i += 2) {
                f32 fl = (drive - 0.3f) * (5.0f + 6.0f * hsh((uint32_t)(t * 30.0f) + (uint32_t)(i + 2)));
                g_line(x + (f32)i * 15.0f, y + 2.0f, x + (f32)i * 15.0f, y + 2.0f + fl, 2.5f, 0.9f);
            }
    }

    mirror(vis);

    /* TAPE: a band of the picture slips sideways and crawls up the screen, with dropouts in it */
    if (tape > 0.03f) {
        int y0, hgt = 3 + (int)(tape * 14.0f), y, x, sh;
        tear_y -= dt * (18.0f + 50.0f * tape);
        if (tear_y < -20.0f)
            tear_y = 250.0f + 60.0f * hsh((uint32_t)(t * 3.0f));
        y0 = (int)tear_y;
        for (y = y0; y < y0 + hgt; y++) {
            uint8_t *row;
            if (y < 0 || y >= FB_H) continue;
            row = &g_fb[y * FB_W];
            sh = (int)(tape * 26.0f * sin_turns((f32)(y - y0) / (f32)hgt * 0.5f)) + (int)(3.0f * tape * (hsh((uint32_t)y + (uint32_t)(t * 20.0f)) - 0.5f) * 6.0f);
            if (sh > 0) {
                for (x = FB_W - 1; x >= sh; x--) row[x] = row[x - sh];
                for (x = 0; x < sh; x++) row[x] = 0;
            } else if (sh < 0) {
                for (x = 0; x < FB_W + sh; x++) row[x] = row[x - sh];
                for (x = FB_W + sh; x < FB_W; x++) row[x] = 0;
            }
        }
        for (i = 0; i < (int)(tape * 9.0f); i++) {
            f32 nx = hsh((uint32_t)(t * 24.0f) * 7u + (uint32_t)i) * 240.0f, ny = (f32)y0 + hsh((uint32_t)(t * 24.0f) * 13u + (uint32_t)i) * (f32)hgt;
            g_line(nx, ny, nx + 6.0f + 14.0f * tape, ny, 1.0f, 0.8f);
        }
    }
}

const InstDef inst_turbo = {
    "TURBO",
    {236, 96, 255},
    {"CHORUS", "TAPE", "GLARE", "DRIVE"},
    {"CRUISE", "KEYS", "SLAP", "BRASS", "MALL", "LEAD", "ARCADE", "NIGHT"},
    {{45, 20, 55, 25}, 0, {4, 58, 70, 52}, {0, 55, 0, 0}, {24, 4, 40, 55}, {0, 3, 1, 60}, {0, 0, 0}},
    0.26f,
    turbo_block, turbo_note, turbo_render, 0, turbo_draw,
};
