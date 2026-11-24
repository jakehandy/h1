/* h1 - PIP: the chiptune instrument. Pulse, stepped triangle, wave RAM and shift-register
 * noise, and a creature made of fat pixels who sings them.
 *
 *   K1 SHAPE   pulse width (or the algorithm's own shape)   his mouth: wide pulse, wide mouth
 *   K2 WOBBLE  delayed vibrato                              his body sways like jelly
 *   K3 ZAP     pitch sweep into each note, down or up       how hard he hops, or ducks
 *   K4 CRUNCH  sample-rate and bit reduction                he is drawn in fewer, fatter pixels
 */
#include "internal.h"
#include "dsp.h"

/* ---- sound ------------------------------------------------------------------------------ */
static struct {
    f32 duty, vib, vibrate, zap, crunch, pwm;
    int algo, chord;
} B;
static f32 pwm_ph;
static f32 wt[4][32];
static int wt_ready;
static f32 cr_acc, cr_l, cr_r;

static const int8_t CHORDS[5][4] = {{0, 4, 7, 12}, {0, 3, 7, 12}, {0, 5, 7, 12}, {0, 7, 12, 19}, {0, 12, 24, 12}};

static void pip_block(const Blk *b)
{
    if (!wt_ready) {
        int i;
        for (i = 0; i < 32; i++) {
            f32 p = ((f32)i + 0.5f) / 32.0f;
            wt[0][i] = sin_turns(p);
            wt[1][i] = 0.55f * sin_turns(p) + 0.35f * sin_turns(2.0f * p) + 0.3f * sin_turns(3.0f * p);
            wt[2][i] = 0.5f * (2.0f * p - 1.0f) + 0.5f * sin_turns(4.0f * p);
            wt[3][i] = (p < 0.5f ? 0.6f : -0.6f) + 0.4f * sin_turns(3.0f * p);
        }
        wt_ready = 1;
    }
    B.algo = b->algo;
    B.duty = lerpf(0.5f, 0.06f, b->k[0]);
    B.vib = b->k[1] * b->k[1] * 0.9f;
    B.vibrate = 5.2f + b->k[1] * 2.2f;
    B.zap = (b->k[2] - 0.5f) * 2.0f;
    if (absf(B.zap) < 0.06f) B.zap = 0.0f;
    B.crunch = b->k[3];
    B.chord = iclamp((int)(b->k[0] * 4.99f), 0, 4);
    pwm_ph = fractf(pwm_ph + (0.12f + 5.0f * b->k[0] * b->k[0]) * b->step);
    B.pwm = 0.5f + 0.42f * tsin(pwm_ph);
}

static void pip_note(Voice *v, const Blk *b)
{
    (void)b;
    v->ph[2] = 0.0f;
    v->s[1] = 0.0f;
    if (!v->u[0]) v->u[0] = 1;
}

static void pip_render(Voice *v, const Blk *b, f32 *out, int n)
{
    f32 pitch = v->pitch + b->bend, f, dt, ph = v->ph[0];
    int i;
    /* delayed vibrato, then the zap */
    v->ph[2] = fractf(v->ph[2] + B.vibrate * (f32)n * b->isr);
    pitch += B.vib * tsin(v->ph[2]) * sat((v->age - 0.12f) * 4.0f);
    if (B.zap > 0.0f)
        pitch += B.zap * 26.0f * exp2fx(-v->age * 30.0f);
    else if (B.zap < 0.0f)
        pitch += B.zap * 14.0f * exp2fx(-v->age * 20.0f);

    switch (B.algo) {
    case 3:                                      /* TRIAD: a chord, one note at a time, very fast */
        pitch += (f32)CHORDS[B.chord][(int)(v->age * 44.0f) & 3];
        break;
    case 4:                                      /* LEAP: an octave blip in front of the note */
        if (v->age < 0.032f) pitch += 12.0f;
        break;
    default: break;
    }
    f = mtof(pitch);
    if (f > 0.45f * b->sr) f = 0.45f * b->sr;
    dt = f * b->isr;

    switch (B.algo) {
    default:
    case 0:
    case 3:
    case 4: {
        f32 duty = B.algo == 0 ? B.duty : (B.algo == 3 ? 0.25f : lerpf(0.5f, 0.12f, b->k[0]));
        for (i = 0; i < n; i++) {
            out[i] = pulse_bl(ph, dt, duty);
            ph += dt;
            if (ph >= 1.0f) ph -= 1.0f;
        }
        break;
    }
    case 1: {                                    /* STAIRS: the 16-step triangle, skewed towards a saw */
        f32 pk = lerpf(0.5f, 0.95f, b->k[0]), ipk = 1.0f / pk, ifl = 1.0f / (1.0f - pk);
        for (i = 0; i < n; i++) {
            f32 x = ph < pk ? ph * ipk : (1.0f - ph) * ifl;
            out[i] = ((f32)(int)(x * 15.99f) * (1.0f / 7.5f) - 1.0f) * 1.25f;
            ph += dt;
            if (ph >= 1.0f) ph -= 1.0f;
        }
        break;
    }
    case 2:                                      /* PWM: the width sweeps by itself */
        for (i = 0; i < n; i++) {
            out[i] = pulse_bl(ph, dt, B.pwm);
            ph += dt;
            if (ph >= 1.0f) ph -= 1.0f;
        }
        break;
    case 5: {                                    /* WAVE: 32 steps of 4 bits, morphing through four shapes */
        f32 m = b->k[0] * 2.999f;
        int a = (int)m;
        f32 fr = m - (f32)a;
        for (i = 0; i < n; i++) {
            int j = (int)(ph * 32.0f) & 31;
            f32 x = wt[a][j] + (wt[a + 1][j] - wt[a][j]) * fr;
            out[i] = (f32)ifloor(x * 7.5f + 0.5f) * (1.0f / 7.5f) * 1.1f;
            ph += dt;
            if (ph >= 1.0f) ph -= 1.0f;
        }
        break;
    }
    case 6: {                                    /* NOISE: a shift register clocked by the key */
        f32 rate = minf(f * 8.0f * b->isr, 1.0f), acc = v->s[1];
        f32 amp = maxf(0.3f, exp2fx(-v->age * 9.0f)) * 1.1f;
        int shortm = b->k[0] >= 0.5f;
        uint32_t l = v->u[0];
        for (i = 0; i < n; i++) {
            acc += rate;
            if (acc >= 1.0f) {
                uint32_t bit = (l ^ (l >> (shortm ? 6 : 1))) & 1u;
                acc -= 1.0f;
                l = (l >> 1) | (bit << 14);
            }
            out[i] = (l & 1u) ? amp : -amp;
        }
        v->u[0] = l ? l : 1;
        v->s[1] = acc;
        break;
    }
    case 7: {                                    /* DUO: two pulses, detuned and then a fifth or an octave apart */
        f32 off = b->k[0] < 0.5f ? b->k[0] * 0.7f : (b->k[0] < 0.75f ? 7.0f : 12.0f);
        f32 dt2 = dt * exp2fx(off * (1.0f / 12.0f)), ph2 = v->ph[1];
        if (dt2 > 0.45f) dt2 = 0.45f;
        for (i = 0; i < n; i++) {
            out[i] = 0.6f * (pulse_bl(ph, dt, 0.25f) + pulse_bl(ph2, dt2, 0.5f));
            ph += dt;
            if (ph >= 1.0f) ph -= 1.0f;
            ph2 += dt2;
            if (ph2 >= 1.0f) ph2 -= 1.0f;
        }
        v->ph[1] = ph2;
        break;
    }
    }
    v->ph[0] = ph;
}

static void pip_post(const Blk *b, f32 *l, f32 *r, int n)
{
    f32 c = B.crunch, hold, lv;
    int i;
    (void)b;
    if (c < 0.03f)
        return;
    hold = 1.0f + c * c * 26.0f;
    lv = lerpf(40.0f, 2.5f, c);
    for (i = 0; i < n; i++) {
        cr_acc += 1.0f;
        if (cr_acc >= hold) {
            cr_acc -= hold;
            cr_l = (f32)ifloor(l[i] * lv + 0.5f) / lv;
            cr_r = (f32)ifloor(r[i] * lv + 0.5f) / lv;
        }
        l[i] = cr_l;
        r[i] = cr_r;
    }
}

/* ---- picture ---------------------------------------------------------------------------- */
/* PIP himself: 12 x 12 fat pixels. Two rows of headgear and one of feet change with the
 * algorithm; the nine rows between are the body, with eyes and a mouth cut out of it. */
static const char *const CORE[9] = {
    "...XXXXXX...",
    ".XXXXXXXXXX.",
    "XXXXXXXXXXXX",
    "XXXXXXXXXXXX",
    "XXXXXXXXXXXX",
    "XXXXXXXXXXXX",
    "XXXXXXXXXXXX",
    "XXXXXXXXXXXX",
    ".XXXXXXXXXX.",
};
static const char *const HEAD[NALGO][2] = {
    {"......XX....", ".....XX....."},
    {".....XX.....", "....XXXX...."},
    {"...X....X...", "...X....X..."},
    {"...X.XX.X...", "...XXXXXX..."},
    {"...XX..XX...", "...XX..XX..."},
    {"...X.X.X.X..", "....X.X.X..."},
    {"............", "............"},
    {"..XX....XX..", "...XX..XX..."},
};
static const char *const FEET[NALGO] = {
    "..XX....XX..", "..XXX..XXX..", ".XX......XX.", "..X..XX..X..",
    "...XX..XX...", ".X.X.XX.X.X.", "..XX....XX..", "..XX.XX.XX..",
};

#define NBIT 28
static struct { f32 x, y, vx, vy, life; } bits[NBIT];
static f32 hop, hopv, str, strv, mouth_o, blink, look_x, look_v, wob_ph, zflash;
static uint32_t seen;
static int dance;
/* (hidden) a run of eight notes, each higher than the last and none of them late: one up */
static f32 run_p, run_t, up_t;
static int run_n;
static uint32_t run_seen;

static uint32_t hash2(int x, int y) { uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u; h = (h ^ (h >> 13)) * 1274126177u; return h ^ (h >> 16); }

static void sprite(int algo, uint8_t m[12][12], const Vis *vis, int mouth_w, int open, int blinking, int look, int up, int face)
{
    int x, y, mx0, ey;
    for (y = 0; y < 12; y++) {
        const char *row = y < 2 ? HEAD[algo][y] : (y < 11 ? CORE[y - 2] : FEET[algo]);
        for (x = 0; x < 12; x++)
            m[y][x] = row[x] == 'X';
    }
    if (algo == 6)                               /* NOISE wears static */
        for (y = 0; y < 2; y++)
            for (x = 2; x < 10; x++)
                m[y][x] = (hash2(x + (int)(vis->t * 24.0f) * 13, y) & 3u) == 0;
    if (dance && vis->level > 0.02f) {           /* the feet shuffle with the notes */
        for (x = 0; x < 11; x++)
            m[11][x] = m[11][x + 1];
        m[11][11] = 0;
    }
    if (!face)
        return;
    /* eyes: two tall slots that look where the last note was */
    ey = up ? 4 : 5;
    if (!blinking) {
        m[ey][3 + look] = 0;
        m[ey][8 + look] = 0;
    }
    m[ey + 1][3 + look] = 0;
    m[ey + 1][8 + look] = 0;
    /* mouth */
    mx0 = 6 - mouth_w / 2;
    for (x = 0; x < mouth_w; x++) {
        m[8][mx0 + x] = 0;
        if (open)
            m[9][mx0 + x] = 0;
    }
}

static APART void one_up(const Vis *vis)
{
    int i;
    if (vis->hand != run_seen) {
        run_seen = vis->hand;
        if (run_t > 3.0f)                        /* (ready again once the playing has stopped) */
            run_n = 0;
        if (run_n >= 0)
            run_n = vis->hand_pitch > run_p && run_t < 0.5f ? run_n + 1 : 1;
        run_p = vis->hand_pitch;
        run_t = 0.0f;
        if (run_n == 8 && EGGS_ON) {
            run_n = -1;
            up_t = 1.0f;
            hopv = -620.0f;
            eng_cue(CUE_PIP);
        }
    }
    run_t += vis->dt;
    if (up_t > 0.0f) {                           /* one up: it goes up from his head, and he throws everything he has */
        f32 u = 1.0f - up_t;
        up_t -= vis->dt * 0.45f;
        g_text(206.0f, 44.0f - 34.0f * ease_out(u), "1UP", 15.0f, 2.6f, sat(up_t * 4.0f), 1);
        for (i = 0; i < NBIT && u < 0.3f; i++)
            if (bits[i].life <= 0.0f) {
                bits[i].x = 120.0f + 60.0f * rng_s(vis->rng);
                bits[i].y = 130.0f;
                bits[i].vx = 190.0f * rng_s(vis->rng);
                bits[i].vy = -(160.0f + 220.0f * rng_f(vis->rng));
                bits[i].life = 1.0f;
            }
    }
}

static SMALL void pip_draw(const Vis *vis)
{
    uint8_t ma[12][12], mb[12][12];
    f32 dt = vis->dt, cs = 14.0f, sx, sy, x0, y0, lvl = 0.0f, zap = (vis->k[2] - 0.5f) * 2.0f, wob = vis->k[1];
    int x, y, i, res, bs, mouth_w, open, blinking, lookr;

    if (vis->fresh) {
        hop = hopv = str = strv = mouth_o = zflash = up_t = 0.0f;
        run_n = 0;
        run_seen = vis->hand;
        seen = vis->notes;
        for (i = 0; i < NBIT; i++)
            bits[i].life = 0.0f;
    }
    for (i = 0; i < NVOICE; i++)
        if (vis->v[i].on && vis->v[i].env > lvl)
            lvl = vis->v[i].env;

    /* a new note: hop (or duck), stretch, spit a few bits */
    if (vis->notes != seen) {
        f32 kick = 150.0f + absf(zap) * 260.0f;
        seen = vis->notes;
        zflash = 1.0f;
        hopv = zap < -0.06f ? kick * 0.8f : -kick;
        strv += 3.5f;
        dance ^= 1;
        for (i = 0, x = 0; i < NBIT && x < 4; i++)
            if (bits[i].life <= 0.0f) {
                f32 dir = (x & 1) ? 1.0f : -1.0f;
                bits[i].x = 120.0f + dir * 34.0f;
                bits[i].y = 140.0f;
                bits[i].vx = dir * (50.0f + 110.0f * rng_f(vis->rng));
                bits[i].vy = -(90.0f + 150.0f * rng_f(vis->rng));
                bits[i].life = 1.0f;
                x++;
            }
    }
    spring_z(&hop, &hopv, 0.0f, 16.0f, 0.42f, dt);
    spring_z(&str, &strv, 0.0f, 22.0f, 0.35f, dt);
    mouth_o += (lvl - mouth_o) * minf(1.0f, dt * 30.0f);
    spring(&look_x, &look_v, vis->last_pitch > 67.0f ? 1.0f : (vis->last_pitch < 57.0f ? -1.0f : 0.0f), 14.0f, dt);

    blink -= dt;
    if (blink < -0.12f)
        blink = 1.6f + 2.8f * rng_f(vis->rng);
    blinking = blink < 0.0f && mouth_o < 0.2f;
    lookr = look_x > 0.5f ? 1 : (look_x < -0.5f ? -1 : 0);
    mouth_w = vis->algo == 0 || vis->algo == 2 ? 2 * iclamp((int)((1.0f - vis->k[0]) * 2.99f) + 1, 1, 3)
                                               : 2 * iclamp((int)(vis->k[0] * 2.99f) + 1, 1, 3);
    open = mouth_o > 0.35f;

    /* CRUNCH: 12, 6, 4 or 3 pixels across */
    res = vis->k[3] < 0.22f ? 12 : (vis->k[3] < 0.5f ? 6 : (vis->k[3] < 0.8f ? 4 : 3));
    bs = 12 / res;
    sprite(vis->algo, ma, vis, mouth_w, open, blinking, lookr, mouth_o > 0.5f, res == 12);
    if (vis->algo_t < 1.0f) {                    /* an algorithm change: the pixels flip over, a few at a time */
        sprite(vis->algo_prev, mb, vis, mouth_w, open, blinking, lookr, mouth_o > 0.5f, res == 12);
        for (y = 0; y < 12; y++)
            for (x = 0; x < 12; x++)
                if ((f32)(hash2(x, y) & 255u) > vis->algo_t * 255.0f)
                    ma[y][x] = mb[y][x];
    }

    /* the sway keeps its own clock, so turning WOBBLE changes its speed without a jump */
    wob_ph = fractf(wob_ph + dt * (0.8f + wob));
    zflash = maxf(0.0f, zflash - dt * 3.0f);
    /* ZAP: wound up tall on his toes, or squat and ready to duck */
    sy = 1.0f + str * 0.5f + zap * 0.13f;
    sx = 1.0f - str * 0.3f - zap * 0.07f;
    x0 = 120.0f - 6.0f * cs * sx;
    y0 = 20.0f + hop + (12.0f * cs) * (1.0f - sy) + 2.5f * sin_turns(vis->t * 0.6f);

    for (y = 0; y < 12; y += bs) {
        f32 shear = wob * 10.0f * sin_turns(wob_ph + (f32)y * 0.09f);
        for (x = 0; x < 12; x += bs) {
            int on = 0, u, w;
            for (u = 0; u < bs; u++)
                for (w = 0; w < bs; w++)
                    on += ma[y + u][x + w];
            if (on * 2 < bs * bs || !on)
                continue;
            g_rect(x0 + (f32)x * cs * sx + shear + 1.0f, y0 + (f32)y * cs * sy + 1.0f,
                   (f32)bs * cs * sx - 2.0f, (f32)bs * cs * sy - 2.0f, y >= 11 ? 0.7f : 1.0f);
        }
    }

    if (res < 12) {                              /* in fat pixels the face is cut back in, one fat pixel per feature */
        f32 fb = (f32)bs, ew = bs == 2 ? 2.0f : fb * 0.5f, eh = (bs == 2 ? 2.0f : fb * 0.6f) * (blinking ? 0.3f : 1.0f);
        f32 eyc = ((f32)((mouth_o > 0.5f ? 5 : 6) / bs) + 0.5f) * fb, myc = ((f32)(9 / bs) + 0.5f) * fb;
        f32 mw = maxf((f32)mouth_w, fb * 0.75f), mh = fb * (open ? 0.7f : 0.3f);
        f32 shear = wob * 10.0f * sin_turns(wob_ph + eyc * 0.09f);
        int e;
        if (myc - mh * 0.5f < eyc + eh * 0.5f + 0.5f)
            myc = eyc + eh * 0.5f + 0.5f + mh * 0.5f;
        g_mode(G_PAINT);
        for (e = 0; e < 2; e++) {
            f32 exc = ((f32)((int)((e ? 8.5f : 3.5f) + (f32)lookr) / bs) + 0.5f) * fb;
            g_rect(x0 + (exc - ew * 0.5f) * cs * sx + shear, y0 + (eyc - eh * 0.5f) * cs * sy, ew * cs * sx, eh * cs * sy, 0.0f);
        }
        g_rect(x0 + (6.0f - mw * 0.5f) * cs * sx + shear, y0 + (myc - mh * 0.5f) * cs * sy, mw * cs * sx, mh * cs * sy, 0.0f);
        g_mode(G_MAX);
    }

    /* ZAP: arrows either side of him, as many as it is strong, that fire off with each note */
    if (absf(zap) >= 0.06f) {
        f32 dir = zap > 0.0f ? -1.0f : 1.0f, fly = (1.0f - zflash) * (1.0f - zflash), a0 = 0.4f + 0.6f * zflash;
        int nz = iclamp((int)(absf(zap) * 4.0f) + 1, 1, 4), side;
        for (side = 0; side < 2; side++)
            for (i = 0; i < nz; i++) {
                f32 cx = side ? 224.0f : 16.0f, cy = 112.0f - dir * ((f32)i - 0.5f * (f32)(nz - 1)) * 16.0f;
                cy += dir * (zflash > 0.0f ? (fly - 1.0f) * 22.0f : 0.0f) + dir * 2.0f * sin_turns(vis->t * 1.2f + (f32)i * 0.17f);
                g_rect(cx - 2.0f, cy + dir * 4.0f - 2.0f, 4.0f, 4.0f, a0);
                g_rect(cx - 6.0f, cy - 2.0f, 4.0f, 4.0f, a0);
                g_rect(cx + 2.0f, cy - 2.0f, 4.0f, 4.0f, a0);
                g_rect(cx - 10.0f, cy - dir * 4.0f - 2.0f, 4.0f, 4.0f, a0 * 0.8f);
                g_rect(cx + 6.0f, cy - dir * 4.0f - 2.0f, 4.0f, 4.0f, a0 * 0.8f);
            }
    }

    one_up(vis);

    /* bits */
    for (i = 0; i < NBIT; i++) {
        if (bits[i].life <= 0.0f)
            continue;
        bits[i].vy += 520.0f * dt;
        bits[i].x += bits[i].vx * dt;
        bits[i].y += bits[i].vy * dt;
        bits[i].life -= dt * 0.9f;
        if (bits[i].y > 200.0f || bits[i].x < -8.0f || bits[i].x > 248.0f)
            bits[i].life = 0.0f;
        else
            g_rect(bits[i].x - 3.0f, bits[i].y - 3.0f, 7.0f, 7.0f, sat(bits[i].life * 1.6f));
    }

    /* the wave he is singing, in steps, running off both edges */
    {
        f32 step = 6.0f + (f32)(12 / res - 1) * 3.0f, py = 0.0f, gain = 70.0f;
        int start = 0, span = 300, nstep = (int)(252.0f / step) + 1, have = 0;
        for (i = 1; i < 160; i++) {              /* a rising zero crossing to hang the picture on */
            f32 a = vis->scope[(vis->scope_pos + i - 1) & (SCOPE_N - 1)], c = vis->scope[(vis->scope_pos + i) & (SCOPE_N - 1)];
            if (a < 0.0f && c >= 0.0f) {
                start = i;
                break;
            }
        }
        for (i = 0; i < nstep; i++) {
            int idx = (vis->scope_pos + start + i * span / nstep) & (SCOPE_N - 1);
            f32 v = clampf(vis->scope[idx] * gain, -15.0f, 15.0f);
            f32 yy = 219.0f - (f32)ifloor(v / 3.0f + 0.5f) * 3.0f, xx = -6.0f + (f32)i * step;
            f32 a = vis->level > 0.004f ? 1.0f : 0.4f;
            if (have)
                g_line(xx, py, xx, yy, 2.0f, a);
            g_line(xx, yy, xx + step, yy, 2.0f, a);
            py = yy;
            have = 1;
        }
    }
}

const InstDef inst_pip = {
    "PIP",
    {200, 255, 74},
    {"SHAPE", "WOBBLE", "ZAP", "CRUNCH"},
    {"PULSE", "STAIRS", "PWM", "TRIAD", "LEAP", "WAVE", "NOISE", "DUO"},
    {{35, 20, 50, 0}, 0, {0, 30, 70, 22}, {0, 55, 0, 0}, {22, 3, 12, 50}, {0, 3, 1, 60}, {0, 0, 0}},
    0.2f,
    pip_block, pip_note, pip_render, pip_post, pip_draw,
};
