/* h1 - COZY: lo-fi keys. An electric piano with tines, one with reeds, a felted upright, a tape
 * flute, vibes, all of them played back off a record that has been loved too much. The picture is
 * the room it is playing in: a window with weather in it and a town beyond, a cat on the sill with
 * its back to you, a record going round under a hanging lamp, a mug that is still warm. The town's
 * windows light up with the notes you hold.
 *
 *   K1 DUST    crackle and hiss                         motes drift in the lamplight
 *   K2 WOW     the record is warped                     the record wobbles, the arm rides it
 *   K3 MUFFLE  a blanket over the speaker               the curtains close
 *   K4 SWAY    tremolo, side to side                    the lamp swings, and the light with it
 */
#include "internal.h"
#include "dsp.h"

/* ---- sound ------------------------------------------------------------------------------ */
typedef struct CAlgo {
    f32 dec;                     /* seconds the note takes to die while held; 0 = it does not */
    f32 att;
    f32 idx;                     /* how much bite */
} CAlgo;
static const CAlgo CA[NALGO] = {
    /* TINE  */ {2.4f, 0.0f, 0.9f},
    /* REED  */ {1.3f, 0.0f, 1.3f},
    /* FELT  */ {2.8f, 0.004f, 0.3f},
    /* TAPE  */ {0.0f, 0.07f, 0.4f},
    /* VIBES */ {3.4f, 0.0f, 0.4f},
    /* DUSK  */ {0.0f, 0.16f, 0.5f},
    /* TOY   */ {0.75f, 0.0f, 0.8f},
    /* RADIO */ {1.9f, 0.0f, 1.0f},
};

static struct {
    int algo;
    f32 dust, wowk, muffle, sway, wow, kd, trem;
} C;
static f32 wow_ph, flut_ph, trem_ph, wander, wander_t, lp_l[2], lp_r[2], pop_e, pop_lp, hiss_lp, act, radio_s[2], hum_ph;
static uint32_t dust_rng = 0xD057D057u;
static volatile int sounding;

static void cozy_block(const Blk *b)
{
    f32 dt = b->step;
    C.algo = b->algo;
    C.dust = b->k[0];
    C.wowk = b->k[1];
    C.muffle = b->k[2];
    C.sway = b->k[3];
    wow_ph = fractf(wow_ph + 0.62f * dt);
    flut_ph = fractf(flut_ph + 5.7f * dt);
    if (dt > 0.0f) {
        wander_t += (rng_s(&dust_rng) - wander_t) * 0.01f;
        wander += (wander_t - wander) * 0.02f;
    }
    C.wow = C.wowk * C.wowk * (0.5f * tsin(wow_ph) + 0.09f * tsin(flut_ph) + 0.5f * wander);
    trem_ph = fractf(trem_ph + (3.2f + 2.4f * C.sway) * dt);
    C.trem = tsin(trem_ph);
    C.kd = CA[b->algo].dec > 0.0f ? 1.0f - onepole_k(CA[b->algo].dec, b->sr / (f32)CTRL) : 1.0f;
}

static void cozy_note(Voice *v, const Blk *b)
{
    int i;
    (void)b;
    if (!v->u[0]) v->u[0] = 0xC0270001u + v->idx * 7919u;
    if (v->env < 0.001f)
        for (i = 0; i < 6; i++)
            v->ph[i] = 0.0f;
    v->s[2] = 1.0f;                              /* the note's own dying away */
    v->s[3] = 1.0f;                              /* the hammer, the tine's ping */
    v->s[4] = 0.0f;
}

static void cozy_render(Voice *v, const Blk *b, f32 *out, int n)
{
    const CAlgo *a = &CA[C.algo];
    f32 pitch = v->pitch + b->bend + C.wow, f = mtof(pitch), d0 = minf(f * b->isr, 0.45f), body = v->s[2], hit = v->s[3];
    f32 p0 = v->ph[0], p1 = v->ph[1], p2 = v->ph[2], p3 = v->ph[3], amp = a->att > 0.0f ? sat(v->age / a->att) : 1.0f, fb = v->s[4];
    f32 key = clampf(1.0f - (pitch - 60.0f) * (1.0f / 60.0f), 0.5f, 1.4f);       /* high notes are thinner and shorter */
    f32 r1 = 1.0f, r2 = 14.0f, r3 = 4.0f, d1, d2, d3;
    int algo = C.algo, i;

    switch (algo) {                              /* where the other partials sit */
    default: break;
    case 1: r1 = 2.0f; break;
    case 2: r1 = 2.003f; r2 = 3.011f; r3 = 4.03f; break;
    case 3: r1 = 2.0f; break;
    case 4: r1 = 4.0f; r2 = 9.9f; break;
    case 5: r1 = 1.006f; r2 = 0.5f; break;
    case 6: r1 = 5.43f; r2 = 9.13f; break;
    }
    d1 = minf(d0 * r1, 0.45f); d2 = minf(d0 * r2, 0.45f); d3 = minf(d0 * r3, 0.45f);

    for (i = 0; i < n; i++) {
        f32 x;
        switch (algo) {
        default:
        case 0:                                  /* TINE: a soft bar, and a hard little bell that is gone at once */
            x = tsin(p0 + a->idx * VEL_FM(v) * (0.25f + 0.75f * body) * key * 0.5f * tsin(p1)) + 0.3f * hit * tsin(p2);
            break;
        case 1:                                  /* REED: it barks when you dig in */
            fb = tsin(p0 + a->idx * VEL_FM(v) * (0.2f + 0.8f * hit) * 0.3f * fb + 0.25f * body * tsin(p1));
            x = fb;
            break;
        case 2:                                  /* FELT: four partials going flat of each other, and the thump of the hammer */
            x = tsin(p0) + 0.5f * body * tsin(p1) + 0.28f * body * body * tsin(p2) + 0.12f * hit * tsin(p3)
                + 0.5f * hit * hit * noise_s(&v->u[0]) * 0.25f;
            break;
        case 3:                                  /* TAPE: a flute, more air than pipe */
            v->s[5] += (noise_s(&v->u[0]) - v->s[5]) * 0.15f;
            x = tsin(p0 + 0.12f * tsin(p1)) + 0.3f * tsin(p1) + v->s[5] * (0.5f + 1.5f * hit);
            break;
        case 4:                                  /* VIBES: a bar and its fourth harmonic, under a turning fan */
            x = (tsin(p0) + 0.35f * hit * tsin(p1) + 0.12f * hit * hit * tsin(p2)) * (1.0f - 0.35f * (0.5f + 0.5f * C.trem));
            break;
        case 5:                                  /* DUSK: two soft triangles leaning on each other */
            x = (absf(2.0f * p0 - 1.0f) * 2.0f - 1.0f) * 0.9f + (absf(2.0f * p1 - 1.0f) * 2.0f - 1.0f) * 0.7f + 0.3f * tsin(p2);
            break;
        case 6:                                  /* TOY: rods, not strings: bright, out of tune with itself, short */
            x = tsin(p0) + 0.55f * body * tsin(p1) + 0.3f * hit * tsin(p2);
            break;
        case 7:
            x = tsin(p0 + a->idx * (0.3f + 0.7f * body) * 0.45f * tsin(p1));
            break;
        }
        out[i] = x * 0.6f * amp * (a->dec > 0.0f ? body : 1.0f);
        p0 += d0; if (p0 >= 1.0f) p0 -= 1.0f;
        p1 += d1; if (p1 >= 1.0f) p1 -= 1.0f;
        p2 += d2; if (p2 >= 1.0f) p2 -= 1.0f;
        p3 += d3; if (p3 >= 1.0f) p3 -= 1.0f;
    }
    v->ph[1] = p1; v->ph[2] = p2; v->ph[3] = p3;
    v->ph[0] = p0;
    v->s[2] = body * (key < 1.0f ? C.kd * (1.0f - (1.0f - C.kd) * (1.0f - key) * 3.0f) : C.kd);
    v->s[3] = hit * (1.0f - onepole_k(algo == 2 ? 0.05f : 0.12f, b->sr / (f32)n));
    v->s[4] = fb;
    sounding = 1;
}

static void cozy_post(const Blk *b, f32 *l, f32 *r, int n)
{
    f32 cut = 15000.0f * exp2fx(-C.muffle * 4.6f), k = onepole_k(1.0f / (H1_TAU * cut), b->sr), d2 = C.dust * C.dust;
    f32 depth = C.sway * 0.7f, tl = 1.0f - depth * (0.5f + 0.5f * C.trem), tr = 1.0f - depth * (0.5f - 0.5f * C.trem);
    f32 pp = d2 * 0.0012f, want = sounding ? 1.0f : 0.3f;
    Svf rf;
    int i, radio = C.algo == 7;
    sounding = 0;
    svf_set(&rf, 1300.0f, 0.9f, b->isr);
    hum_ph = fractf(hum_ph + 100.0f * (f32)n * b->isr);
    for (i = 0; i < n; i++) {
        f32 nz = noise_s(&dust_rng), x, lo, bp;
        act += (want - act) * 0.00005f;
        if (rng_f(&dust_rng) < pp)               /* a pop: a click that rings for a moment */
            pop_e = (0.25f + 0.75f * rng_f(&dust_rng)) * (rng_next(&dust_rng) & 1u ? 1.0f : -1.0f);
        pop_lp += (pop_e - pop_lp) * 0.35f;
        pop_e *= 0.9f;
        hiss_lp += (nz - hiss_lp) * 0.3f;
        /* (the pops were 0.5 of full scale: on top of a chord they were the loudest thing in the mix) */
        x = (pop_lp * 0.25f + hiss_lp * 0.02f * (0.4f + C.dust)) * C.dust * act;
        if (radio) {                             /* RADIO: a small speaker, and the mains under it */
            f32 m = (l[i] + r[i]) * 0.5f;
            SVF_TICK(rf, radio_s, m, lo, bp);
            (void)lo;
            l[i] = r[i] = softclip(bp * 2.2f) * 0.8f + 0.006f * tsin(hum_ph + (f32)i * 100.0f * b->isr);
        }
        l[i] = l[i] * tl + x;
        r[i] = r[i] * tr + x;
        lp_l[0] += (l[i] - lp_l[0]) * k; lp_l[1] += (lp_l[0] - lp_l[1]) * k;
        lp_r[0] += (r[i] - lp_r[0]) * k; lp_r[1] += (lp_r[0] - lp_r[1]) * k;
        l[i] = lp_l[1];
        r[i] = lp_r[1];
    }
}

/* ---- picture ---------------------------------------------------------------------------- */
#define WX0 98.0f
#define WX1 226.0f
#define WY0 24.0f
#define WY1 150.0f
#define NMOTE 36
static f32 lit[12], ear, tail_ph, rec_ph, steam, lamp_a, lamp_v, cur_c, bolt, bolt_x;
static struct { f32 x, y, age; } fw[4];
static uint32_t seen, bolt_seed;
enum { W_RAIN, W_SUNSET, W_SNOW, W_MOON, W_STARS, W_FOG, W_FIREWORKS, W_STORM };

static f32 hsh(uint32_t n) { n = (n ^ 61u) ^ (n >> 16); n *= 9u; n ^= n >> 4; n *= 0x27d4eb2du; n ^= n >> 15; return (f32)(n & 0xFFFFu) * (1.0f / 65536.0f); }

static void weather(int w, f32 a, const Vis *vis)
{
    f32 t = vis->t, W = WX1 - WX0, H = WY1 - WY0;
    int i, j;
    if (a < 0.02f)
        return;
    switch (w) {
    default:
    case W_STORM:
    case W_RAIN: {
        int storm = w == W_STORM, nf = storm ? 24 : 15, nn = storm ? 13 : 8;
        f32 lean = storm ? 0.5f + 0.12f * sin_turns(t * 0.13f) : 0.24f + 0.06f * sin_turns(t * 0.07f), fast = storm ? 1.5f : 1.0f;
        for (i = 0; i < nf; i++) {               /* far off, a thin grey rain */
            f32 sp = 0.55f + 0.35f * hsh((uint32_t)i + 3u), u = fractf(hsh((uint32_t)i + 50u) + t * sp * fast);
            f32 y = WY0 + 2.0f + u * (H - 16.0f), x = WX0 + 6.0f + fractf(hsh((uint32_t)i + 90u) - u * lean * 0.5f) * (W - 12.0f);
            g_beam(x + lean * 9.0f, y - 9.0f, x, y, 1.0f, 1.1f, 0.0f, a * 0.38f * sat(u * 6.0f), 0.0f);
        }
        for (i = 0; i < nn; i++) {               /* nearer, drops you could count: a streak with a bright head */
            f32 sp = 1.0f + 0.6f * hsh((uint32_t)i + 13u), u = fractf(hsh((uint32_t)i + 150u) + t * sp * fast), len = 13.0f + 9.0f * hsh((uint32_t)i + 170u);
            f32 y = WY0 + 2.0f + u * (H - 6.0f), x = WX0 + 6.0f + fractf(hsh((uint32_t)i + 190u) - u * lean * 0.9f) * (W - 12.0f);
            g_beam(x + lean * len, maxf(y - len, WY0 + 1.0f), x, y, 1.0f, 1.8f, 0.0f, a * 0.8f * sat(u * 8.0f), 0.0f);
        }
        if (w == W_STORM && bolt > 0.0f) {       /* a note in a storm is a stroke of lightning */
            f32 xy[2 * 8], x = bolt_x, y = WY0 + 2.0f;
            for (i = 0; i < 8; i++) {
                xy[2 * i] = x; xy[2 * i + 1] = y;
                x += (hsh(bolt_seed + (uint32_t)i) - 0.5f) * 22.0f;
                x = clampf(x, WX0 + 6.0f, WX1 - 6.0f);
                y += 11.0f;
            }
            g_poly(xy, 8, 0, 1.0f + 2.0f * bolt, a * sat(bolt * 3.0f));
        }
        break;
    }
    case W_SUNSET: {
        f32 cx = WX0 + W * 0.5f, cy = WY1 - 34.0f;
        g_disc(cx, cy, 26.0f, a);
        for (i = 0; i < 9; i++) {
            f32 an = 0.5f + ((f32)i + 0.5f) / 9.0f * 0.5f, r0 = 30.0f + 2.0f * sin_turns(t * 0.2f + (f32)i * 0.3f), r1 = r0 + ((i & 1) ? 9.0f : 16.0f);
            g_line(cx + cos_turns(an) * r0, cy + sin_turns(an) * r0, cx + cos_turns(an) * r1, cy + sin_turns(an) * r1, 1.4f, a * 0.8f);
        }
        for (i = 0; i < 6; i++) {                /* birds going home: a loose skein, each beating in its own time and gliding when it likes */
            f32 h = hsh((uint32_t)i + 811u), h2 = hsh((uint32_t)i + 823u), row = (f32)((i + 1) / 2), side = (i & 1) ? 1.0f : -1.0f;
            f32 lead = fractf(t * 0.03f + (i == 5 ? 0.45f : 0.0f)) * (W + 90.0f) - 20.0f;
            f32 bx = WX0 + lead - row * (10.0f + 3.0f * h) + 2.5f * sin_turns(t * (0.11f + 0.07f * h) + h2);
            f32 by = WY0 + 24.0f + 7.0f * sin_turns(t * 0.05f) + 4.0f * sin_turns(t * 0.13f + 0.3f) + side * row * (5.5f + 2.0f * h2) + 2.0f * sin_turns(t * (0.17f + 0.1f * h2) + h);
            f32 beat = sat(0.6f + 1.6f * sin_turns(t * (0.09f + 0.05f * h) + h * 3.0f)), ph = t * (2.3f + 0.5f * h) + h2;
            f32 sz = 0.62f + 0.25f * h2, in = 0.05f + beat * 0.15f * sin_turns(ph), out = -0.03f + beat * 0.2f * sin_turns(ph - 0.17f);
            f32 ba = a * 0.9f * sat((bx - WX0 - 2.0f) * 0.25f) * sat((WX1 - 2.0f - bx) * 0.25f);
            if (i == 5) by += 22.0f;             /* (and one that is late) */
            if (ba < 0.03f)
                continue;
            by -= 1.4f * beat * sin_turns(ph + 0.2f) * sz;       /* the body rides the stroke */
            for (j = -1; j <= 1; j += 2) {
                f32 ex = bx + (f32)j * 3.6f * sz * cos_turns(in) - 0.8f * sz, ey = by - 3.6f * sz * sin_turns(in);
                f32 tx = ex + (f32)j * 4.2f * sz * cos_turns(out) - 1.2f * sz, ty = ey - 4.2f * sz * sin_turns(out);
                g_line(bx, by, ex, ey, 1.3f, ba);
                g_line(ex, ey, tx, ty, 1.0f, ba);
            }
            g_disc(bx, by + 0.3f, 1.1f * sz, ba);
        }
        break;
    }
    case W_SNOW:
        for (i = 0; i < 30; i++) {
            f32 sp = 0.1f + 0.14f * hsh((uint32_t)i + 3u), u = fractf(hsh((uint32_t)i + 50u) + t * sp);
            f32 x = WX0 + 5.0f + fractf(hsh((uint32_t)i + 90u)) * (W - 10.0f) + 5.0f * sin_turns(t * 0.3f * (0.5f + sp * 4.0f) + (f32)i), y = WY0 + 3.0f + u * (H - 8.0f);
            if (x > WX0 + 2.0f && x < WX1 - 2.0f)
                g_disc(x, y, 0.8f + 1.3f * hsh((uint32_t)i + 7u), a * (0.5f + 0.4f * hsh((uint32_t)i + 9u)));
        }
        break;
    case W_MOON:
    case W_STARS: {
        int n = w == W_STARS ? 22 : 9;
        for (i = 0; i < n; i++) {
            f32 x = WX0 + 6.0f + hsh((uint32_t)i + 200u) * (W - 12.0f), y = WY0 + 5.0f + hsh((uint32_t)i + 240u) * (H - 60.0f);
            f32 tw = 0.5f + 0.5f * sin_turns(t * (0.2f + 0.3f * hsh((uint32_t)i)) + (f32)i * 0.3f);
            if (w == W_STARS && i % 4 == 0) {
                g_line(x - 2.0f - 2.0f * tw, y, x + 2.0f + 2.0f * tw, y, 1.0f, a * 0.8f);
                g_line(x, y - 2.0f - 2.0f * tw, x, y + 2.0f + 2.0f * tw, 1.0f, a * 0.8f);
            } else
                g_disc(x, y, 0.9f, a * (0.4f + 0.5f * tw));
        }
        if (w == W_MOON) {
            g_disc(WX1 - 34.0f, WY0 + 30.0f, 15.0f, a);
            g_mode(G_PAINT);
            g_disc(WX1 - 28.0f, WY0 + 26.0f, 13.0f, 0.0f);
            g_mode(G_MAX);
            g_ring(WX1 - 34.0f, WY0 + 30.0f, 21.0f + 1.5f * sin_turns(t * 0.1f), 1.0f, a * 0.3f);
        } else {                                 /* now and then one falls */
            f32 u = fractf(t * 0.11f);
            if (u < 0.12f) {
                f32 x = WX0 + 30.0f + 70.0f * hsh((uint32_t)(t * 0.11f)) + u * 260.0f, y = WY0 + 10.0f + u * 220.0f;
                if (x < WX1 - 4.0f)
                    g_line(x - 12.0f, y - 10.0f, x, y, 1.2f, a * (1.0f - u * 8.0f));
            }
        }
        break;
    }
    case W_FOG:
        for (j = 0; j < 5; j++) {
            f32 xy[2 * 12], y0 = WY0 + 22.0f + (f32)j * 21.0f;
            for (i = 0; i < 12; i++) {
                xy[2 * i] = WX0 + 3.0f + (W - 6.0f) * (f32)i / 11.0f;
                xy[2 * i + 1] = y0 + 4.0f * sin_turns((f32)i * 0.11f + t * (0.05f + 0.02f * (f32)j) + (f32)j * 0.3f);
            }
            g_poly(xy, 12, 0, 2.0f + (f32)(j % 3), a * (0.2f + 0.1f * (f32)(j % 2)));
        }
        break;
    case W_FIREWORKS:
        for (i = 0; i < 4; i++)
            if (fw[i].age < 1.0f) {
                f32 r = 30.0f * ease_out(fw[i].age), fade = 1.0f - fw[i].age;
                for (j = 0; j < 12; j++) {
                    f32 an = (f32)j / 12.0f + (f32)i * 0.03f, c = cos_turns(an), s = sin_turns(an), dr = fw[i].age * fw[i].age * 10.0f;
                    f32 x0 = fw[i].x + c * r * 0.55f, y0 = fw[i].y + s * r * 0.55f + dr, x1 = fw[i].x + c * r, y1 = fw[i].y + s * r + dr;
                    if (x1 > WX0 + 2.0f && x1 < WX1 - 2.0f && y1 > WY0 + 2.0f)
                        g_line(x0, y0, x1, y1, 1.3f, a * fade);
                }
            }
        for (i = 0; i < 8; i++)
            g_disc(WX0 + 8.0f + hsh((uint32_t)i + 200u) * (W - 16.0f), WY0 + 6.0f + hsh((uint32_t)i + 240u) * 50.0f, 0.8f, a * 0.5f);
        break;
    }
}

/* RAIN and STORM: drops on the glass itself. Each sits and swells, then lets go and runs down in fits,
 * leaving a wet track that dries from the top. */
static void glass(int w, f32 a, const Vis *vis)
{
    f32 t = vis->t, W = WX1 - WX0;
    int i, n = w == W_STORM ? 9 : 6;
    if (a < 0.02f || (w != W_RAIN && w != W_STORM))
        return;
    for (i = 0; i < n; i++) {
        f32 per = (w == W_STORM ? 3.0f : 5.0f) + 4.0f * hsh((uint32_t)i + 31u), ph = t / per + hsh((uint32_t)i + 37u), q = fractf(ph);
        uint32_t cyc = (uint32_t)ifloor(ph) * 16u + (uint32_t)i;
        f32 x0 = WX0 + 14.0f + hsh(cyc + 41u) * (W - 28.0f), y0 = WY0 + 8.0f + hsh(cyc + 43u) * 56.0f, x = x0, y = y0, r = 0.9f + 1.1f * sat(q * 2.0f);
        if (q > 0.5f) {
            f32 g = (q - 0.5f) * 2.0f, run = g * g + 0.05f * sin_turns(g * 4.0f) * (1.0f - g);        /* it goes, catches, goes */
            y = y0 + sat(run) * (WY1 - 6.0f - y0);
            x = x0 + 2.5f * sin_turns(run * 1.7f + hsh(cyc + 47u)) * sat(run * 4.0f);
            g_beam(x0, y0 + (y - y0) * sat(g * 1.6f - 0.4f), x, y, 1.0f, 1.6f, 0.0f, a * 0.5f, 0.0f);
            r = 2.0f - 0.5f * g;
        }
        g_disc(x, y, r, a * (0.55f + 0.4f * sat(q * 2.0f)));
        g_mode(G_PAINT);
        g_disc(x + r * 0.3f, y + r * 0.3f, r * 0.45f, 0.0f);     /* (a drop is clear: only its edge shows) */
        g_mode(G_MAX);
    }
}

/* (hidden) the same note over and over walks the plant along the sill, a little each time. It walks back
 * when it is left alone. Keep on and it goes off the end. */
static f32 pot_x, pot_y, pot_v, pot_t, pot_p, pot_idle;
static uint32_t pot_seen;
static int pot_done;

static APART void pot(const Vis *vis)
{
    f32 t = vis->t;
    int i;
    if (vis->hand != pot_seen) {
        pot_seen = vis->hand;
        if (pot_idle > 3.0f)                     /* (ready again once the playing has stopped) */
            pot_done = 0;
        if (vis->hand_pitch == pot_p && !pot_done && pot_v == 0.0f && EGGS_ON)
            pot_x += 2.6f;
        pot_p = vis->hand_pitch;
        pot_idle = 0.0f;
    }
    pot_idle += vis->dt;
    pot_x = maxf(0.0f, pot_x - vis->dt * (pot_v == 0.0f ? 1.2f : 0.0f));
    if (pot_x > 24.0f || pot_v != 0.0f) {        /* over the end: down onto the desk, and that is that */
        pot_v += 600.0f * vis->dt;
        pot_y += pot_v * vis->dt;
        if (pot_y > 58.0f) {
            pot_v = pot_x = pot_y = 0.0f;
            pot_t = 1.0f;
            pot_done = 1;
            ear = 1.0f;
            eng_cue(CUE_COZY);
        }
    }
    pot_t = maxf(0.0f, pot_t - vis->dt * 0.14f);
    if (pot_t > 0.86f) {                         /* what is left of it, on the desk */
        for (i = 0; i < 7; i++) {
            f32 u = (1.0f - pot_t) * 7.0f, an = -0.05f - 0.4f * hsh((uint32_t)i + 640u), r = (10.0f + 30.0f * hsh((uint32_t)i + 650u)) * u;
            f32 x = 231.0f + cos_turns(an) * r, y = 206.0f + sin_turns(an) * r + 60.0f * u * u;
            if (y < 207.0f)
                g_line(x, y, x + 4.0f * cos_turns(an * 3.0f + u), y + 4.0f * sin_turns(an * 3.0f + u), 1.6f, 1.0f - u * 0.7f);
        }
    }
    if (pot_t < 0.25f) {                         /* the plant (and after a while somebody puts another one there) */
        f32 bx = WX1 - 18.0f + pot_x + 130.0f * pot_t, by = WY1 - 1.0f + pot_y, q[8];
        q[0] = bx - 7.0f; q[1] = by - 11.0f; q[2] = bx + 7.0f; q[3] = by - 11.0f; q[4] = bx + 5.0f; q[5] = by; q[6] = bx - 5.0f; q[7] = by;
        g_mode(G_PAINT);
        g_rect(bx - 8.0f, by - 12.0f, 16.0f, 12.0f, 0.0f);
        g_mode(G_MAX);
        g_fill(q, 4, 0.95f);
        for (i = 0; i < 5; i++) {
            f32 an = -0.42f + (f32)i * 0.085f + lamp_a * 0.6f + 0.01f * sin_turns(t * 0.3f + (f32)i), L = 16.0f + 7.0f * hsh((uint32_t)i + 600u);
            f32 mx = bx + cos_turns(an) * L * 0.6f, my = by - 11.0f + sin_turns(an) * L * 0.7f, ex = bx + cos_turns(an) * L * 1.1f, ey = by - 11.0f + sin_turns(an) * L * 0.85f + 3.0f;
            g_line(bx, by - 11.0f, mx, my, 1.6f, 0.95f);
            g_line(mx, my, ex, ey, 1.2f, 0.95f);
            g_disc(mx, my, 2.0f, 0.9f);
        }
    }
}

static void cozy_draw(const Vis *vis)
{
    f32 dt = vis->dt, t = vis->t, dust = vis->k[0], wow = vis->k[1], muffle = vis->k[2], sway = vis->k[3], mt = smooth01(vis->algo_t);
    f32 lev = sat(vis->level * 3.0f), want[12] = {0}, W = WX1 - WX0;
    int i, j;

    if (vis->fresh) {
        seen = vis->notes;
        for (i = 0; i < 12; i++) lit[i] = 0.0f;
        for (i = 0; i < 4; i++) fw[i].age = 1.0f;
        ear = bolt = 0.0f;
        cur_c = muffle;
        pot_x = pot_y = pot_v = pot_t = 0.0f;
        pot_seen = vis->hand;
        pot_done = 0;
    }
    for (i = 0; i < NVOICE; i++)
        if (vis->v[i].on) {
            int pc = ((int)(vis->v[i].pitch + 0.5f) % 12 + 12) % 12;
            if (vis->v[i].env > want[pc]) want[pc] = vis->v[i].env;
        }
    for (i = 0; i < 12; i++)
        lit[i] += (sat(want[i] * 1.5f) - lit[i]) * minf(1.0f, dt * (want[i] > lit[i] ? 20.0f : 2.0f));
    if (vis->notes != seen) {
        int best = 0;
        seen = vis->notes;
        ear = 1.0f;
        bolt = 0.35f;
        bolt_x = WX0 + 14.0f + sat((vis->last_pitch - 48.0f) * (1.0f / 36.0f)) * (W - 28.0f);
        bolt_seed = rng_next(vis->rng);
        for (i = 1; i < 4; i++)
            if (fw[i].age > fw[best].age) best = i;
        fw[best].age = 0.0f;
        fw[best].x = bolt_x;
        fw[best].y = WY0 + 26.0f + 34.0f * rng_f(vis->rng);
    }
    for (i = 0; i < 4; i++)
        fw[i].age = minf(1.0f, fw[i].age + dt * 0.9f);
    ear = maxf(0.0f, ear - dt * 3.0f);
    bolt = maxf(0.0f, bolt - dt);
    steam += (lev - steam) * minf(1.0f, dt * 2.0f);
    tail_ph = fractf(tail_ph + dt * (0.18f + 0.5f * lev));
    rec_ph = fractf(rec_ph + dt * 0.555f);       /* 33 and a third */
    spring(&lamp_a, &lamp_v, sway * 0.055f * sin_turns(t * (0.45f + 0.35f * sway)), 9.0f, dt);
    cur_c += (muffle - cur_c) * minf(1.0f, dt * 7.0f);

    /* the window: what is outside it first */
    if (mt < 1.0f)
        weather(vis->algo_prev, 1.0f - mt, vis);
    weather(vis->algo, mt, vis);
    {                                            /* the town: roofs, and a lit window for every pitch */
        f32 px = WX0, py = WY1 - 20.0f, x = WX0;
        g_mode(G_PAINT);
        for (i = 0; i < 10; i++) {
            f32 bw = 10.0f + 7.0f * hsh((uint32_t)i + 400u), top = WY1 - 18.0f - 36.0f * hsh((uint32_t)i + 420u);
            if (x + bw > WX1) bw = WX1 - x;
            g_rect(x, top, bw, WY1 - top, 0.0f);
            x += bw;
        }
        g_mode(G_MAX);
        x = WX0;
        for (i = 0; i < 10; i++) {
            f32 bw = 10.0f + 7.0f * hsh((uint32_t)i + 400u), top = WY1 - 18.0f - 36.0f * hsh((uint32_t)i + 420u), wy;
            int row = 0;
            if (x + bw > WX1) bw = WX1 - x;
            if (bw < 2.0f) break;
            if (i) g_line(px, py, x, py, 1.0f, 0.7f);
            g_line(x, py, x, top, 1.0f, 0.7f);
            if (hsh((uint32_t)i + 430u) > 0.6f)  /* an aerial */
                g_line(x + bw * 0.5f, top, x + bw * 0.5f, top - 7.0f, 1.0f, 0.6f);
            for (wy = top + 4.0f; wy < WY1 - 5.0f; wy += 7.0f, row++)
                for (j = 0; j < 2; j++) {
                    int pc = (i * 7 + row * 5 + j * 3) % 12;
                    f32 wx = x + 2.5f + (f32)j * (bw - 8.0f);
                    if (j && bw < 11.0f) break;
                    if (hsh((uint32_t)(i * 31 + row * 7 + j) + 440u) < 0.3f) continue;       /* nobody home */
                    g_rect(wx, wy, 3.0f, 3.5f, 0.1f + 0.9f * lit[pc]);
                }
            px = x; py = top;
            x += bw;
        }
        g_line(px, py, WX1, py, 1.0f, 0.7f);
    }
    if (mt < 1.0f)
        glass(vis->algo_prev, 1.0f - mt, vis);
    glass(vis->algo, mt, vis);
    /* MUFFLE: the curtains */
    {
        f32 cw = 9.0f + cur_c * (W * 0.5f - 7.0f);
        for (j = 0; j < 2; j++) {
            f32 x0 = j ? WX1 - cw : WX0, dir = j ? -1.0f : 1.0f, edge = j ? WX1 - cw : WX0 + cw;
            int folds = 2 + (int)(cw / 9.0f);
            g_mode(G_PAINT);
            g_rect(x0, WY0, cw, WY1 - WY0, 0.0f);
            g_mode(G_MAX);
            for (i = 0; i <= folds; i++) {
                f32 u = (f32)i / (f32)folds, x = (j ? WX1 : WX0) + dir * cw * u, sw = 1.5f * sin_turns(t * 0.11f + u * 0.7f + (f32)j * 0.3f) * u;
                g_line(x, WY0 + 1.0f, x + sw, WY1 - 5.0f - 3.0f * sin_turns(u * 1.5f), i == folds ? 1.6f : 1.0f, i == folds ? 0.9f : 0.4f + 0.2f * (f32)(i & 1));
            }
            (void)edge;
        }
    }
    /* the frame, the bars, the sill, the rod */
    {                                            /* lightning lights the frame up for a moment */
        f32 fl = vis->algo == W_STORM ? sat((bolt - 0.2f) * 6.0f) : 0.0f, fw2 = 2.0f + 3.0f * fl;
        g_line(WX0, WY0, WX1, WY0, fw2, 1.0f); g_line(WX0, WY0, WX0, WY1, fw2, 1.0f); g_line(WX1, WY0, WX1, WY1, fw2, 1.0f);
    }
    if (cur_c < 0.94f) {
        f32 cw = 9.0f + cur_c * (W * 0.5f - 7.0f);
        g_line(WX0 + cw, (WY0 + WY1) * 0.5f, WX1 - cw, (WY0 + WY1) * 0.5f, 1.4f, 0.85f);
        if (cur_c < 0.9f)
            g_line((WX0 + WX1) * 0.5f, WY0, (WX0 + WX1) * 0.5f, WY1, 1.4f, 0.85f);
    }
    g_line(WX0 - 9.0f, WY1 + 1.0f, WX1 + 8.0f, WY1 + 1.0f, 3.0f, 1.0f);
    g_line(WX0 - 6.0f, WY1 + 6.0f, WX1 + 5.0f, WY1 + 6.0f, 1.0f, 0.6f);
    g_line(WX0 - 10.0f, WY0 - 6.0f, WX1 + 10.0f, WY0 - 6.0f, 1.5f, 0.9f);
    g_disc(WX0 - 11.0f, WY0 - 6.0f, 2.5f, 0.9f); g_disc(WX1 + 11.0f, WY0 - 6.0f, 2.5f, 0.9f);

    /* a plant on the sill */
    pot(vis);
    /* the cat: sitting, looking out, back to you. Its ear turns to each note and its tail keeps time. */
    {
        f32 cx = WX0 + 36.0f, by = WY1 - 1.0f, tri[6], sw = sin_turns(tail_ph), xy[2 * 9];
        g_mode(G_PAINT);
        g_ellipse(cx, by - 15.0f, 15.0f, 17.0f, 0.0f, 0.0f);
        g_disc(cx, by - 35.0f, 10.5f, 0.0f);
        g_mode(G_MAX);
        g_ellipse(cx, by - 15.0f, 13.5f, 15.5f, 0.0f, 1.0f);
        g_disc(cx, by - 35.0f, 9.0f, 1.0f);
        for (i = -1; i <= 1; i += 2) {
            f32 fl = i > 0 ? ear * 3.0f : 0.0f;
            tri[0] = cx + (f32)i * 2.5f; tri[1] = by - 41.0f;
            tri[2] = cx + (f32)i * 9.5f; tri[3] = by - 39.0f;
            tri[4] = cx + (f32)i * (8.5f + fl); tri[5] = by - 50.0f + fl * 0.6f;
            g_fill(tri, 3, 1.0f);
        }
        for (i = 0; i < 9; i++) {                /* the tail hangs off the sill and swings from its root */
            f32 u = (f32)i / 8.0f;
            xy[2 * i] = cx - 10.0f - 3.0f * u + sw * 9.0f * u * u + 4.0f * sin_turns(tail_ph * 2.0f + u * 0.6f) * u * (0.3f + lev);
            xy[2 * i + 1] = by - 2.0f + 30.0f * u;
        }
        g_poly(xy, 9, 0, 4.2f, 1.0f);
    }

    /* the lamp, and the record under it */
    {
        f32 px = 48.0f, c = cos_turns(0.25f + lamp_a), s = sin_turns(0.25f + lamp_a), L = 52.0f, hx = px + c * L, hy = s * L, q[8];
        f32 rx = 34.0f, ry = 12.0f, rcx = 48.0f, rcy = 190.0f, wob = wow * 3.2f * sin_turns(rec_ph), tilt = wow * 0.1f * sin_turns(rec_ph + 0.25f);
        g_line(px, -2.0f, hx, hy, 1.0f, 0.9f);
        q[0] = hx - 5.0f; q[1] = hy; q[2] = hx + 5.0f; q[3] = hy; q[4] = hx + 17.0f; q[5] = hy + 15.0f; q[6] = hx - 17.0f; q[7] = hy + 15.0f;
        g_fill(q, 4, 1.0f);
        g_disc(hx, hy + 17.0f, 3.5f, 0.9f);
        g_line(hx - 17.0f, hy + 16.0f, rcx - 46.0f + (hx - px) * 1.6f, 176.0f, 1.0f, 0.2f);         /* the edge of the light */
        g_line(hx + 17.0f, hy + 16.0f, rcx + 46.0f + (hx - px) * 1.6f, 176.0f, 1.0f, 0.2f);
        for (i = 0; i < (int)(dust * (f32)NMOTE); i++) {        /* DUST: motes in the light */
            f32 u = fractf(hsh((uint32_t)i + 700u) + t * (0.012f + 0.02f * hsh((uint32_t)i + 710u))), wd = 14.0f + 36.0f * u;
            f32 mx = hx + (hx - px) * 1.6f * u + (hsh((uint32_t)i + 720u) - 0.5f) * 2.0f * wd + 5.0f * sin_turns(t * 0.11f + (f32)i * 0.37f);
            f32 my = hy + 20.0f + u * (150.0f - hy), tw = 0.5f + 0.5f * sin_turns(t * 0.5f + (f32)i * 0.41f);
            g_disc(mx, my, 0.7f + 0.7f * tw, (0.25f + 0.6f * tw) * sat(dust * 2.0f));
        }
        /* the deck */
        g_line(6.0f, 205.0f, 92.0f, 205.0f, 1.2f, 0.8f); g_line(6.0f, 205.0f, 6.0f, 214.0f, 1.2f, 0.8f); g_line(92.0f, 205.0f, 92.0f, 214.0f, 1.2f, 0.8f);
        g_line(6.0f, 214.0f, 92.0f, 214.0f, 1.2f, 0.8f);
        g_line(6.0f, 205.0f, 14.0f, 176.0f, 1.0f, 0.5f); g_line(92.0f, 205.0f, 86.0f, 176.0f, 1.0f, 0.5f); g_line(14.0f, 176.0f, 86.0f, 176.0f, 1.0f, 0.5f);
        g_mode(G_PAINT);
        g_ellipse(rcx, rcy + wob * 0.3f, rx + 1.5f, ry + 1.5f + absf(wob) * 0.3f, 0.0f, 0.0f);
        g_mode(G_MAX);
        for (i = 0; i < 5; i++) {                /* the record: grooves, and the wobble if it is warped */
            f32 k = 1.0f - (f32)i * 0.15f, yy = rcy + wob * k * 0.6f * sin_turns(rec_ph + (f32)i * 0.07f);
            g_ellipse(rcx, yy, rx * k, ry * k * (1.0f + tilt), i == 0 ? 1.8f : 1.0f, i == 0 ? 1.0f : 0.4f + 0.12f * (f32)(i & 1));
        }
        g_ellipse(rcx, rcy, rx * 0.3f, ry * 0.3f, 0.0f, 0.5f + 0.5f * lev);
        g_mode(G_PAINT);
        g_disc(rcx, rcy, 1.2f, 0.0f);
        g_mode(G_MAX);
        for (i = 0; i < 3; i++) {                /* the light going round on it */
            f32 an = rec_ph + (f32)i / 3.0f;
            g_line(rcx + cos_turns(an) * rx * 0.36f, rcy + sin_turns(an) * ry * 0.36f, rcx + cos_turns(an) * rx * 0.92f, rcy + sin_turns(an) * ry * 0.92f + wob * 0.5f, 1.0f, 0.5f);
        }
        {                                        /* the arm */
            f32 ax = 84.0f, ay = 180.0f, tx = rcx + rx * 0.62f, ty = rcy - ry * 0.35f + wob * 0.8f;
            g_disc(ax, ay, 3.2f, 1.0f);
            g_line(ax, ay, tx + 4.0f, ty - 5.0f, 1.8f, 1.0f);
            g_line(tx + 4.0f, ty - 5.0f, tx, ty, 3.0f, 1.0f);
        }
    }

    /* the desk under the window: a mug still steaming, a few books */
    {
        f32 mx = 152.0f, my = 198.0f, q[8];
        g_line(102.0f, 208.0f, 238.0f, 208.0f, 1.6f, 0.9f);
        q[0] = mx - 8.0f; q[1] = my - 12.0f; q[2] = mx + 8.0f; q[3] = my - 12.0f; q[4] = mx + 7.0f; q[5] = my + 9.0f; q[6] = mx - 7.0f; q[7] = my + 9.0f;
        g_fill(q, 4, 1.0f);
        g_arc(mx + 8.0f, my - 1.0f, 5.5f, 0.75f, 1.25f, 1.6f, 1.0f);
        g_mode(G_ADD);                           /* steam: breaths of it leaving the mug, swelling, wandering off and thinning to nothing */
        for (i = 0; i < 13; i++) {
            f32 h = hsh((uint32_t)i + 950u), u = fractf(h + t * (0.085f + 0.03f * hsh((uint32_t)i + 960u)) * (1.0f + 0.5f * steam)), hgt = 26.0f + 14.0f * steam;
            f32 x = mx + (h - 0.5f) * 5.0f + (1.0f + 8.0f * u) * sin_turns(t * 0.12f + u * 0.8f + (f32)(i & 1) * 0.5f) * (0.5f + 0.5f * u), y = my - 13.0f - hgt * u;
            g_glow(x, y, 2.4f + 6.5f * u, (0.42f + 0.4f * steam) * sat(u * 7.0f) * (1.0f - u) * (1.0f - u));
        }
        g_mode(G_MAX);
        for (i = 0; i < 4; i++) {                /* books, one of them not straight */
            f32 bx = 186.0f + (f32)i * 9.0f, lean = i == 3 ? 6.0f : 0.0f, hh = 24.0f + 6.0f * hsh((uint32_t)i + 900u);
            g_line(bx, 207.0f, bx + lean, 207.0f - hh, 1.2f, 0.9f); g_line(bx + 7.0f, 207.0f, bx + 7.0f + lean, 207.0f - hh, 1.2f, 0.9f);
            g_line(bx + lean, 207.0f - hh, bx + 7.0f + lean, 207.0f - hh, 1.2f, 0.9f);
            g_line(bx + 1.5f + lean * 0.2f, 201.0f, bx + 5.5f + lean * 0.2f, 201.0f, 1.0f, 0.6f);
        }
    }
}

const InstDef inst_cozy = {
    "COZY",
    {255, 176, 138},
    {"DUST", "WOW", "MUFFLE", "SWAY"},
    {"TINE", "REED", "FELT", "TAPE", "VIBES", "DUSK", "TOY", "RADIO"},
    {{35, 30, 35, 25}, 0, {0, 60, 100, 50}, {0, 55, 0, 0}, {8, 4, 30, 42}, {0, 3, 1, 60}, {0, 0, 0}},
    0.3f,
    cozy_block, cozy_note, cozy_render, cozy_post, cozy_draw,
};
