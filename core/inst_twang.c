/* h1 - TWANG: plucked strings. Each voice is a real delay-line string, loaded at the note with the
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
#include "smp.h"
#include "str.h"

/* ---- sound ------------------------------------------------------------------------------
 * Seven of the eight are the string of str.h, fed with the first moments of a recorded pluck (smp.h): the
 * finger or the plectrum, and the instrument's body answering it. The string's decay starts from what was
 * measured on the recording, and DAMP moves it from there. BOW is a recorded cello, held. */
typedef struct TAlgo {
    uint8_t bank;
    f32 ring;                    /* decay time, against the recording's */
    f32 bright;                  /* loop filter, against the recording's */
    f32 transpose;
    f32 level;
    uint8_t second;              /* 0 none, 1 an octave string beside it, 2 a sympathetic drone */
} TAlgo;

static const TAlgo TA[NALGO] = {
    /* HARP   */ {SMP_CONCERTHARP, 1.0f, 1.0f, 0.0f, 1.0f, 0},
    /* NYLON  */ {SMP_GUITAR_SOFT, 0.6f, 0.9f, 0.0f, 1.15f, 0},
    /* TWELVE */ {SMP_GUITAR_HARD, 1.0f, 1.0f, 0.0f, 0.8f, 1},
    /* SITAR  */ {SMP_SITAR, 1.0f, 1.0f, 0.0f, 0.9f, 2},
    /* BANJO  */ {SMP_BANJO, 1.0f, 1.0f, 0.0f, 1.0f, 0},
    /* KOTO   */ {SMP_KOTO, 1.0f, 1.0f, 0.0f, 1.0f, 0},
    /* BASS   */ {SMP_BASS, 1.0f, 1.0f, -12.0f, 1.3f, 0},
    /* BOW    */ {SMP_CELLO, 1.0f, 1.0f, 0.0f, 1.0f, 0},
};

#define T_LEVEL 3.4f             /* a string's level against the other instruments (inside, it stays small: the
                                  * bridge only rattles when BUZZ says so) */
#define T_BOW 3.0f
#define T_FADE 48                /* samples in which a string that is plucked again lets go of what it held */

static struct {
    int algo;
    f32 ts, gs, pick, comb, tone, buzz, thr, drone, drive, idrive, mute;
} T;
static f32 dc_y;

static void twang_block(const Blk *b)
{
    const TAlgo *a = &TA[b->algo];
    f32 d = b->k[0], p = b->k[1], t = b->k[2];
    T.algo = b->algo;
    /* DAMP: at its default the string is the recording's; either way from there it rings on or is muted */
    T.ts = exp2fx((0.25f - d) * 5.5f) * a->ring;
    T.gs = lerpf(1.1f, 0.3f, d * sqrtfx(d)) * a->bright;
    T.mute = d;
    /* PICK: where along the string, as a comb on what it gives. Its default is the player's own. */
    T.pick = b->algo == 4 ? lerpf(0.25f, 0.03f, p) : lerpf(0.5f, 0.04f, p);
    T.comb = 0.75f * sat(absf(p - 0.35f) * 4.0f);
    T.tone = t;
    T.buzz = b->algo == 3 ? 0.5f + 0.5f * b->k[3] : b->k[3];
    T.thr = T.buzz < 0.01f ? 100.0f : lerpf(0.75f, 0.012f, sqrtfx(T.buzz));
    T.drive = 1.0f + T.buzz * 2.5f;
    T.idrive = 1.0f / (1.0f + T.buzz * 0.8f);
    T.drone = mtof(48.0f + (f32)st.keys[0] + b->bend);
}

static void twang_note(Voice *v, const Blk *b)
{
    const TAlgo *a = &TA[T.algo];
    f32 pitch = v->target + b->bend + a->transpose;
    Str s;
    if (!v->u[2]) v->u[2] = 0x9E3779B9u + v->idx * 7919u;
    v->q[1] = (uint32_t)smp_pick(a->bank, pitch);
    if (T.algo == 7) {                           /* BOW: from the note's start, or straight on if it is still sounding */
        if (v->env < 0.001f) {
            smp_start(&v->r[0]);
            v->s[6] = v->s[7] = 0.0f;
        }
        v->s[10] = 0.0f;
        return;
    }
    smp_fit(smp_zone(a->bank, v->q[1], pitch), pitch, &v->s[8], &v->s[9]);
    str_set(&s, mtof(pitch), 0.5f, 1.0f, b->sr);
    smp_start(&v->r[0]);                         /* the pluck starts */
    v->q[3] = smp_inc(smp_zone(a->bank, v->q[1], pitch), pitch, b->isr);
    v->q[2] = (uint32_t)s.len + 4u;              /* and for one trip round, what the string held is let go */
    v->s[13] = (f32)v->q[2];
    v->s[12] = 0.0f;
}

/* BOW: a cello, as long as the key is down. */
static void bow_render(Voice *v, const Blk *b, f32 *out, int n, f32 pitch)
{
    const SmpZone *z = smp_zone(SMP_CELLO, v->q[1], pitch);
    f32 lp = v->s[6], hp = v->s[7], fade = v->s[10];
    f32 kl = onepole_k(1.0f / (H1_TAU * (350.0f + 9000.0f * (1.0f - T.mute) * (1.0f - T.mute))), b->sr);
    f32 kh = onepole_k(1.0f / (H1_TAU * 900.0f), b->sr), edge = (T.pick < 0.2f ? (0.2f - T.pick) * 9.0f : 0.0f) + T.tone * 1.2f - 0.5f;
    int i;
    v->ph[2] = fractf(v->ph[2] + 5.3f * (f32)n * b->isr);
    pitch += 0.14f * tsin(v->ph[2]) * sat(v->age * 2.0f - 0.7f);
    smp_read(z, &v->r[0], smp_inc(z, pitch, b->isr), z->gain * T_BOW, out, n, 0);
    for (i = 0; i < n; i++) {
        f32 x = out[i];
        hp += kh * (x - hp);
        x += edge * (x - hp);                    /* TONE and a bow near the bridge: more of the top */
        lp += kl * (x - lp);                     /* DAMP: a mute on the bridge */
        fade += (1.0f - fade) * 0.01f;
        out[i] = softclip(lp * T.drive) * T.idrive * fade;
    }
    v->s[6] = lp;
    v->s[7] = hp;
    v->s[10] = fade;
}

static void twang_render(Voice *v, const Blk *b, f32 *out, int n)
{
    const TAlgo *a = &TA[T.algo];
    const SmpZone *z;
    f32 *line = ks_line[v->idx][0], *line2 = ks_line[v->idx][1];
    f32 e[CTRL], pitch = v->pitch + b->bend + a->transpose, thr = T.thr, tl = v->s[12], amp, lev = a->level * T_LEVEL;
    f32 ta = T.tone < 0.45f ? lerpf(0.12f, 1.0f, T.tone * (1.0f / 0.45f)) : 1.0f + (T.tone - 0.45f) * 3.0f;
    Str s1, s2;
    uint32_t w = v->u[0] & STR_MASK, w2 = v->u[1] & STR_MASK, kill = v->q[2], pk;
    int i, second = a->second, fed;

    if (n > CTRL) n = CTRL;
    if (T.algo == 7) {
        bow_render(v, b, out, n, pitch);
        return;
    }
    z = smp_zone(a->bank, v->q[1], pitch);
    if (T.algo == 5)                             /* KOTO: the hand behind the bridge, once the note has settled */
        pitch += 0.12f * tsin(v->age * 5.5f) * sat(v->age * 2.0f - 0.6f);
    str_set(&s1, mtof(pitch), clampf(v->s[8] * T.gs, 0.05f, 0.985f), maxf(v->s[9], 0.05f) * T.ts, b->sr);
    s2 = s1;
    if (second == 1)
        str_set(&s2, mtof(pitch + 12.0f) * 1.002f, clampf(v->s[8] * T.gs, 0.05f, 0.985f), maxf(v->s[9], 0.05f) * T.ts * 0.7f, b->sr);
    else if (second == 2)
        str_set(&s2, T.drone, 0.9f, 5.0f, b->sr);

    /* what the string is fed: the recorded pluck at the note's speed, rounded off or sharpened (what with) */
    amp = z->gain;
    fed = !smp_done(z, &v->r[0]);
    if (fed) {
        smp_read(z, &v->r[0], v->q[3], amp, e, n, 0);
        for (i = 0; i < n; i++) {
            tl += 0.16f * (e[i] - tl);
            e[i] = tl + ta * (e[i] - tl);
        }
    } else
        for (i = 0; i < n; i++)
            e[i] = 0.0f;
    pk = (uint32_t)clampf(T.pick * s1.len, 1.0f, (f32)(KS_LEN - 8));

    for (i = 0; i < n; i++) {
        f32 fb = str_back(line, w, &s1, &v->s[0]), o, y;
        int old = kill != 0;
        if (kill) {                              /* (the first trip round still carries the last note) */
            fb *= maxf(0.0f, 1.0f - (v->s[13] - (f32)kill) * (1.0f / (f32)T_FADE));
            kill--;
        }
        o = e[i] + fb;
        if (o > thr) o = thr + (o - thr) * 0.2f;                 /* the bridge: one-sided, so it rattles */
        line[w] = o;
        y = o - T.comb * line[(w - pk) & STR_MASK];              /* PICK: where along the string */
        w = (w + 1u) & STR_MASK;
        if (second) {
            f32 o2 = str_back(line2, w2, &s2, &v->s[2]);
            if (second == 1) {
                if (old)
                    o2 = 0.0f;
                o2 += e[i] * 0.8f;
            } else
                o2 += o * 0.012f;
            line2[w2] = o2;
            w2 = (w2 + 1u) & STR_MASK;
            y += o2 * (second == 2 ? 0.8f : 0.75f);
        }
        out[i] = softclip(y * T.drive) * T.idrive * lev;
    }
    v->u[0] = w;
    v->u[1] = w2;
    v->q[2] = kill;
    v->s[12] = tl;
}

static void twang_post(const Blk *b, f32 *l, f32 *r, int n)
{
    int i;
    (void)b;
    for (i = 0; i < n; i++) {
        /* the one-sided bridge leaves an offset behind: take it out */
        f32 m = (l[i] + r[i]) * 0.5f;
        dc_y += 0.0025f * (m - dc_y);
        l[i] -= dc_y;
        r[i] -= dc_y;
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

static SMALL void twang_draw(const Vis *vis)          /* (built small: room for BLAB) */
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
        f32 e[4], x0, y0, dx, dy, len, nx, ny, xy[2 * (NSEG + 1)], amp, live, A, zz = buzz * 9.0f, bzone = 0.16f + 0.4f * buzz;
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
            if (u < bzone && zz > 0.05f)         /* BUZZ: the bridge end rattles, further along the harder it buzzes */
                d += ((j & 1) ? zz : -zz) * (1.0f - u / bzone) * (0.55f + 1.6f * amp + 0.45f * sin_turns(vis->t * 9.0f + (f32)i * 0.3f));
            xy[2 * j] = x0 + dx * u * live + nx * d;
            xy[2 * j + 1] = y0 + dy * u * live + ny * d;
        }
        g_poly(xy, NSEG + 1, 0, (vis->algo == 6 ? wline * 1.5f : wline), 0.5f + 0.5f * sat(amp * 4.0f));
        if (buzz > 0.04f) {                      /* and the bridge it rattles on: a plate that grows teeth and throws sparks */
            f32 bw = 4.0f + 7.0f * buzz, tw = (f32)(int)(vis->t * 22.0f), k;
            int q, nsp = (int)(buzz * 3.99f) + (amp > 0.05f ? 2 : 0);
            g_line(x0 - nx * bw, y0 - ny * bw, x0 + nx * bw, y0 + ny * bw, 1.5f + 2.0f * buzz, 0.45f + 0.5f * buzz);
            for (q = 0; q < nsp; q++) {
                f32 h1 = fractf(sin_turns(tw * 0.137f + (f32)(i * 7 + q * 3) * 0.211f) * 43.7f), h2 = fractf(h1 * 17.3f + 0.31f);
                f32 su = 0.03f + 0.22f * h1 * (0.4f + buzz), sd = (h2 - 0.5f) * (10.0f + 26.0f * buzz) * (0.5f + amp);
                f32 sx = x0 + dx * su * live + nx * sd, sy = y0 + dy * su * live + ny * sd;
                k = 1.5f + 3.0f * h2 * buzz;
                g_line(sx - nx * k, sy - ny * k, sx + nx * k, sy + ny * k, 1.2f, (0.35f + 0.65f * sat(amp * 3.0f)) * buzz);
            }
        }
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
