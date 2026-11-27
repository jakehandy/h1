/* h1 - BONK: the drum kit. Twelve drums, one on every note of the octave (the octave you play it
 * in tunes it), each of them a synthesised hit: a tone that drops, a burst of filtered noise, a
 * cluster of clashing squares, in different amounts. The picture is a composition of twelve
 * shapes, one a drum; hit one and it fills, jumps and throws a ring. The algorithm is the kit,
 * and it re-hangs the whole composition.
 *
 *   C kick  C# rim  D snare  D# clap  E low tom  F mid tom  F# hat  G high tom
 *   G# shaker  A cowbell  A# open hat  B crash
 *
 *   K1 TUNE    every drum up or down                   the shapes shrink or grow
 *   K2 DECAY   how long they ring                      how long a hit glows, how far its ring goes
 *   K3 PUNCH   the pitch drop, the click, the drive    how hard a shape jumps, how heavy its line
 *   K4 DIRT    noise, grit and crushed samples         grain eats the print, the edges shake
 */
#include "internal.h"
#include "dsp.h"

/* ---- sound ------------------------------------------------------------------------------ */
enum { KICK, RIM, SNARE, CLAP, TOML, TOMM, HAT, TOMH, SHAKE, COW, OHAT, CRASH, NDRUM };

typedef struct Drum {
    f32 f0, sweep, tsw, tdec, tone;              /* the tone: Hz, octaves of drop, its time, decay, level */
    f32 nlev, nfc, nq, ndec;                     /* the noise: level, filter Hz, Q, decay */
    f32 mlev, mdec;                              /* the metal: level, decay */
    uint8_t nmode;                               /* 0 low-pass, 1 band-pass, 2 high-pass */
} Drum;

static const Drum DRUMS[NDRUM] = {
    /* f0   sweep  tsw    tdec   tone  nlev  nfc    nq   ndec   mlev  mdec  mode */
    {48.0f, 2.2f, 0.030f, 0.34f, 1.0f, 0.10f, 2500, 0.7f, 0.012f, 0.0f, 0.0f, 0},     /* kick */
    {470.0f, 0.3f, 0.004f, 0.022f, 0.7f, 0.5f, 3200, 2.5f, 0.012f, 0.25f, 0.02f, 1},  /* rim */
    {186.0f, 0.7f, 0.014f, 0.10f, 0.6f, 0.9f, 4200, 0.8f, 0.15f, 0.0f, 0.0f, 1},      /* snare */
    {0.0f, 0.0f, 0.01f, 0.01f, 0.0f, 1.0f, 1300, 1.6f, 0.17f, 0.0f, 0.0f, 1},         /* clap */
    {88.0f, 0.9f, 0.05f, 0.30f, 1.0f, 0.12f, 900, 0.8f, 0.04f, 0.0f, 0.0f, 0},        /* low tom */
    {132.0f, 0.8f, 0.045f, 0.24f, 1.0f, 0.12f, 1200, 0.8f, 0.035f, 0.0f, 0.0f, 0},    /* mid tom */
    {0.0f, 0.0f, 0.01f, 0.01f, 0.0f, 0.25f, 9000, 0.8f, 0.03f, 0.9f, 0.038f, 2},      /* hat */
    {196.0f, 0.7f, 0.04f, 0.19f, 1.0f, 0.12f, 1700, 0.8f, 0.03f, 0.0f, 0.0f, 0},      /* high tom */
    {0.0f, 0.0f, 0.01f, 0.01f, 0.0f, 0.8f, 6800, 1.4f, 0.06f, 0.0f, 0.0f, 1},         /* shaker */
    {540.0f, 0.0f, 0.01f, 0.2f, 0.0f, 0.0f, 2000, 1.0f, 0.01f, 1.0f, 0.2f, 1},        /* cowbell: two of the squares only */
    {0.0f, 0.0f, 0.01f, 0.01f, 0.0f, 0.2f, 9000, 0.8f, 0.3f, 0.9f, 0.42f, 2},         /* open hat */
    {0.0f, 0.0f, 0.01f, 0.01f, 0.0f, 0.5f, 6000, 0.6f, 0.9f, 0.8f, 1.1f, 2},          /* crash */
};

typedef struct Kit {
    f32 pitch, sweep, dec, noise, bright;        /* multipliers */
    f32 fm, crush;                               /* the tone bends itself; samples held */
    uint8_t wave;                                /* 0 sine, 1 hollow, 2 square */
    int8_t updown;                               /* -1: the pitch rises into the note instead */
} Kit;

static const Kit KITS[NALGO] = {
    {1.0f, 1.0f, 1.25f, 0.85f, 0.85f, 0.0f, 0.0f, 0, 1},      /* BOOM */
    {1.15f, 1.5f, 0.6f, 1.1f, 1.3f, 0.15f, 0.0f, 0, 1},       /* SNAP */
    {0.92f, 0.9f, 0.9f, 1.2f, 0.6f, 0.2f, 0.75f, 0, 1},       /* DUST */
    {1.9f, 0.25f, 0.5f, 0.2f, 0.7f, 0.0f, 0.0f, 1, 1},        /* WOOD */
    {1.45f, 0.4f, 1.0f, 0.5f, 1.35f, 2.0f, 0.0f, 0, 1},       /* TIN */
    {1.3f, 0.5f, 1.3f, 0.3f, 0.8f, 0.5f, 0.0f, 0, -1},        /* TABLA */
    {2.2f, 3.0f, 1.0f, 0.3f, 1.2f, 0.0f, 0.15f, 2, 1},        /* ZAP */
    {1.0f, 0.6f, 0.8f, 1.6f, 1.0f, 1.1f, 0.4f, 1, 1},         /* JUNK */
};

static struct {
    int algo;
    f32 tune, dec, punch, dirt;
} K;
static uint32_t hat_gen;
static const f32 METAL[6] = {1.0f, 1.4471f, 1.6170f, 1.9265f, 2.5028f, 2.6637f};

static void bonk_block(const Blk *b)
{
    K.algo = b->algo;
    K.tune = (b->k[0] - 0.5f) * 24.0f;
    K.dec = exp2fx((b->k[1] - 0.5f) * 5.4f);
    K.punch = b->k[2];
    K.dirt = b->k[3];
}

static void bonk_note(Voice *v, const Blk *b)
{
    int type = v->note % 12, i;
    (void)b;
    if (!v->u[0]) v->u[0] = 0xB0A7C0DEu + v->idx * 977u;
    v->s[8] = (f32)type;
    v->s[4] = v->s[5] = v->s[6] = v->s[7] = 1.0f;
    v->s[0] = v->s[1] = v->s[2] = v->s[3] = 0.0f;
    v->s[9] = 0.0f;
    v->s[11] = 0.0f;
    v->s[13] = 1.0f;
    for (i = 0; i < 6; i++)
        v->ph[i] = fractf(v->r1 * (f32)(i + 1) * 0.37f);
    v->u[1] = 0;
    if (type == HAT)
        hat_gen++;
    v->u[2] = hat_gen;
    v->pan = type == KICK || type == SNARE ? 0.0f : clampf(((f32)type - 5.5f) * 0.11f, -0.6f, 0.6f);
}

static void bonk_render(Voice *v, const Blk *b, f32 *out, int n)
{
    int type = (int)v->s[8], i, j;
    const Drum *d = &DRUMS[type >= 0 && type < NDRUM ? type : 0];
    const Kit *k = &KITS[K.algo];
    Svf fn, fm;
    f32 oct = ((f32)v->note - 60.0f - (f32)type) * (1.0f / 12.0f);     /* which octave it was played in */
    f32 semis = K.tune + oct * 5.0f + b->bend + (K.algo == 7 ? (v->r1 - 0.5f) * 9.0f : 0.0f);
    f32 ratio = k->pitch * exp2fx(semis * (1.0f / 12.0f));
    f32 dec = K.dec * k->dec * (type == KICK && K.algo == 0 ? 1.5f : 1.0f);
    f32 f0 = d->f0 * ratio, sweep = d->sweep * k->sweep * (0.35f + 1.3f * K.punch) * (f32)k->updown;
    f32 kt = 1.0f - onepole_k(d->tdec * dec, b->sr), kn = 1.0f - onepole_k(d->ndec * dec, b->sr);
    f32 km = 1.0f - onepole_k(d->mdec * dec, b->sr), ks = 1.0f - onepole_k(d->tsw * (k->updown < 0 ? 4.0f : 1.0f), b->sr);
    f32 at = v->s[4], an = v->s[5], am = v->s[6], sw = v->s[7], ph = v->s[9], fb = v->s[11];
    f32 nlev = d->nlev * k->noise * (1.0f + 0.9f * K.dirt), tone = d->tone, mlev = d->mlev;
    f32 mf = (type == COW ? d->f0 : 320.0f) * ratio * (type == CRASH ? 1.31f : 1.0f);
    f32 drive = 1.0f + 2.5f * K.punch + 3.0f * K.dirt, click = (type == KICK || type <= TOMH) ? 0.5f * K.punch : 0.0f;
    f32 crush = clampf(k->crush + 0.8f * K.dirt * K.dirt, 0.0f, 0.95f), hold = v->s[12], hc = v->s[13], hstep = 1.0f - crush * 0.93f;
    int nmet = type == COW ? 2 : 6, clap = type == CLAP;

    if (type == OHAT && v->u[2] != hat_gen)      /* a closed hat chokes the open one */
        km = 0.995f;
    if (K.algo == 7 && tone > 0.0f) {            /* JUNK: the tone is a clank of noise in a tight filter */
        nlev += 2.6f;
        tone *= 0.3f;
    }
    svf_set(&fn, (K.algo == 7 && d->tone > 0.0f ? f0 * 2.0f : d->nfc * k->bright * (0.7f + 0.6f * v->r2)), K.algo == 7 ? 9.0f : d->nq, b->isr);
    svf_set(&fm, minf(5200.0f * k->bright, 15000.0f), 0.8f, b->isr);

    for (i = 0; i < n; i++) {
        f32 x = 0.0f, nz, lo, bp, f;
        if (tone > 0.0f) {
            f = minf(f0 * exp2fx(sweep * sw), 12000.0f) * b->isr;
            ph += f;
            if (ph >= 1.0f) ph -= 1.0f;
            fb = tsin(ph + fb * k->fm * 0.32f + (k->fm > 1.0f ? 0.3f * tsin(ph * 2.41f) * at : 0.0f));
            switch (k->wave) {
            case 1: x = fb * (0.6f + 0.4f * absf(fb)); break;
            case 2: x = softclip(fb * 5.0f) * 0.7f; break;
            default: x = fb; break;
            }
            x *= tone * at;
            if (click > 0.0f && v->u[1] < 96u)
                x += click * (1.0f - (f32)v->u[1] * (1.0f / 96.0f));
            sw *= ks;
            at *= kt;
        }
        if (nlev > 0.0f) {
            nz = noise_s(&v->u[0]);
            SVF_TICK(fn, &v->s[0], nz, lo, bp);
            x += nlev * an * (d->nmode == 0 ? lo : (d->nmode == 1 ? bp * 1.4f : nz - lo - bp * fn.k));
            an *= kn;
            if (clap && (v->u[1] == 420u || v->u[1] == 860u || v->u[1] == 1300u))
                an = 1.0f;                       /* three hands, then the room */
        }
        if (mlev > 0.0f) {
            f32 m = 0.0f;
            for (j = 0; j < nmet; j++) {
                f32 p = v->ph[j] + mf * METAL[j] * b->isr;
                if (p >= 1.0f) p -= (f32)(int)p;
                v->ph[j] = p;
                m += p < 0.5f ? 1.0f : -1.0f;
            }
            m *= nmet == 2 ? 0.5f : 0.22f;
            SVF_TICK(fm, &v->s[2], m, lo, bp);
            x += mlev * am * (type == COW ? bp * 1.3f : m - lo - bp * fm.k);
            am *= km;
        }
        v->u[1]++;
        x = softclip(x * drive) * (1.0f / (0.55f + 0.45f * drive));
        if (crush > 0.01f) {                     /* fewer samples, the cheap way */
            hc += hstep;
            if (hc >= 1.0f) {
                hc -= 1.0f;
                hold = (f32)ifloor(x * 24.0f + 0.5f) * (1.0f / 24.0f);
            }
            x = hold;
        }
        out[i] = x;
    }
    v->s[4] = at; v->s[5] = an; v->s[6] = am; v->s[7] = sw; v->s[9] = ph; v->s[11] = fb; v->s[12] = hold; v->s[13] = hc;
}

/* ---- picture ---------------------------------------------------------------------------- */
typedef struct Shape {
    f32 x, y, sz, rot, vx, vy, vs, vr;           /* where it hangs, on springs */
    f32 e, pop, popv, ring, fall, fallv;         /* a hit: glow, the jump, the ring going out, JUNK's drop */
} Shape;
static Shape sh[NDRUM];
static uint32_t seen_serial[NVOICE];
static uint32_t grain_seed = 1;
static f32 grain_t, jit[NDRUM][2];

/* the home composition (BOOM): x, y, size, rotation in turns */
static const f32 HOME[NDRUM][4] = {
    {80, 152, 44, 0},          /* kick: the big disc */
    {26, 104, 11, 0.02f},      /* rim: a cross */
    {172, 92, 34, 0.02f},      /* snare: a triangle */
    {58, 54, 27, -0.07f},      /* clap: three bars */
    {34, 212, 17, 0.04f},      /* low tom: a square */
    {126, 42, 15, 0},          /* mid tom: a target */
    {198, 38, 17, 0},          /* hat: nine dots */
    {214, 132, 9, 0},          /* high tom: a ring */
    {150, 222, 30, 0},         /* shaker: a zigzag */
    {128, 100, 15, -0.03f},    /* cowbell: a trapezoid */
    {132, 142, 82, 0.1f},      /* open hat: a long arc */
    {196, 190, 24, 0},         /* crash: a burst */
};
static const uint8_t ORDER[NDRUM] = {OHAT, KICK, SNARE, CLAP, COW, TOML, TOMM, TOMH, HAT, SHAKE, RIM, CRASH};

static const f32 NEST[NDRUM] = {42, 92, 64, 17, 30, 29, 52, 21, 76, 24, 100, 11};

static f32 hashf(uint32_t i)
{
    i = i * 747796405u + 2891336453u;
    i = ((i >> ((i >> 28) + 4u)) ^ i) * 277803737u;
    return (f32)((i ^ (i >> 22)) >> 8) * (1.0f / 16777216.0f);
}

static APART void layout(int algo, int i, f32 t, f32 *o)
{
    f32 a;
    o[0] = HOME[i][0]; o[1] = HOME[i][1]; o[2] = HOME[i][2]; o[3] = HOME[i][3];
    switch (algo) {
    default:
        break;
    case 1:                                      /* SNAP: a strict grid */
        o[0] = 42.0f + (f32)(i % 4) * 52.0f;
        o[1] = 66.0f + (f32)(i / 4) * 56.0f;
        o[2] = i == OHAT ? 19.0f : (i == KICK ? 20.0f : 15.0f);
        o[3] = 0.0f;
        break;
    case 2:                                      /* DUST: thrown about, small, turning */
        o[0] = 28.0f + hashf((uint32_t)i * 7u + 1u) * 184.0f;
        o[1] = 44.0f + hashf((uint32_t)i * 7u + 2u) * 168.0f;
        o[2] = HOME[i][2] * 0.62f;
        o[3] = hashf((uint32_t)i * 7u + 3u) + t * 0.02f * (i & 1 ? 1.0f : -1.0f);
        break;
    case 3:                                      /* WOOD: two totems */
        o[0] = (i & 1) ? 158.0f : 82.0f;
        o[1] = 40.0f + (f32)(i / 2) * 33.0f;
        o[2] = 12.0f + 5.0f * hashf((uint32_t)i + 40u);
        o[3] = 0.0f;
        break;
    case 4:                                      /* TIN: a dial, turning */
        a = (f32)i / 12.0f + t * 0.025f;
        o[0] = 120.0f + 84.0f * cos_turns(a);
        o[1] = 124.0f + 84.0f * sin_turns(a);
        o[2] = 13.0f;
        o[3] = a + 0.25f;
        break;
    case 5:                                      /* TABLA: everything on one centre, nested */
        o[0] = 120.0f;
        o[1] = 124.0f;
        o[2] = NEST[i];
        o[3] = (f32)i * 0.03f + t * 0.014f * (i & 1 ? 1.0f : -1.0f);
        break;
    case 6:                                      /* ZAP: a slanted rank */
        o[0] = 22.0f + (f32)i * 17.8f;
        o[1] = 212.0f - (f32)i * 15.5f;
        o[2] = 12.0f + (f32)(i % 3) * 3.0f;
        o[3] = -0.11f;
        break;
    case 7:                                      /* JUNK: a heap on the floor */
        o[2] = 12.0f + 14.0f * hashf((uint32_t)i + 90u);
        o[0] = 26.0f + (f32)((i * 5) % 12) * 17.0f;
        o[1] = 226.0f - o[2] - (f32)(i % 3) * 26.0f * hashf((uint32_t)i + 70u);
        o[3] = hashf((uint32_t)i + 80u) * 0.3f - 0.15f;
        break;
    }
}

/* one shape at (x, y), size s, turned by r; fill 0..1 floods it, a is the line */
static APART void shape(int type, f32 x, f32 y, f32 s, f32 r, f32 fill, f32 a, f32 w)
{
    f32 c = cos_turns(r), sn = sin_turns(r), xy[16];
    int i;
#define PX(u, v) (x + ((u) * c - (v) * sn) * s)
#define PY(u, v) (y + ((u) * sn + (v) * c) * s)
    switch (type) {
    case KICK:
        if (fill > 0.02f) g_disc(x, y, s, fill);
        g_ring(x, y, s, w, a);
        break;
    case RIM:
        g_line(PX(-1, 0), PY(-1, 0), PX(1, 0), PY(1, 0), w + 1.0f + 3.0f * fill, a);
        g_line(PX(0, -1), PY(0, -1), PX(0, 1), PY(0, 1), w + 1.0f + 3.0f * fill, a);
        break;
    case SNARE:
        xy[0] = PX(0, -1.0f); xy[1] = PY(0, -1.0f);
        xy[2] = PX(0.92f, 0.72f); xy[3] = PY(0.92f, 0.72f);
        xy[4] = PX(-0.92f, 0.72f); xy[5] = PY(-0.92f, 0.72f);
        if (fill > 0.02f) g_fill(xy, 3, fill);
        g_poly(xy, 3, 1, w, a);
        break;
    case CLAP:
        for (i = -1; i <= 1; i++) {
            f32 v = (f32)i * 0.62f;
            xy[0] = PX(-1, v - 0.17f); xy[1] = PY(-1, v - 0.17f);
            xy[2] = PX(1, v - 0.17f); xy[3] = PY(1, v - 0.17f);
            xy[4] = PX(1, v + 0.17f); xy[5] = PY(1, v + 0.17f);
            xy[6] = PX(-1, v + 0.17f); xy[7] = PY(-1, v + 0.17f);
            if (fill > 0.02f) g_fill(xy, 4, fill);
            g_poly(xy, 4, 1, w * 0.8f, a);
        }
        break;
    case TOML:
        xy[0] = PX(-1, -1); xy[1] = PY(-1, -1);
        xy[2] = PX(1, -1); xy[3] = PY(1, -1);
        xy[4] = PX(1, 1); xy[5] = PY(1, 1);
        xy[6] = PX(-1, 1); xy[7] = PY(-1, 1);
        if (fill > 0.02f) g_fill(xy, 4, fill);
        g_poly(xy, 4, 1, w, a);
        break;
    case TOMM:
        g_ring(x, y, s, w, a);
        g_disc(x, y, s * (0.34f + 0.5f * fill), maxf(a * 0.9f, fill));
        break;
    case HAT:
        for (i = 0; i < 9; i++) {
            f32 u = (f32)(i % 3 - 1) * 0.8f, v = (f32)(i / 3 - 1) * 0.8f;
            g_disc(PX(u, v), PY(u, v), s * (0.13f + 0.2f * fill) + w * 0.3f, a);
        }
        break;
    case TOMH:
        g_ring(x, y, s, w + 1.5f + 4.0f * fill, a);
        break;
    case SHAKE:
        for (i = 0; i < 7; i++) {
            f32 u = -1.0f + (f32)i / 3.0f, v = (i & 1 ? 0.3f : -0.3f) * (1.0f + 1.6f * fill);
            xy[2 * i] = PX(u, v);
            xy[2 * i + 1] = PY(u, v);
        }
        g_poly(xy, 7, 0, w, a);
        break;
    case COW:
        xy[0] = PX(-0.55f, -1); xy[1] = PY(-0.55f, -1);
        xy[2] = PX(0.55f, -1); xy[3] = PY(0.55f, -1);
        xy[4] = PX(1, 1); xy[5] = PY(1, 1);
        xy[6] = PX(-1, 1); xy[7] = PY(-1, 1);
        if (fill > 0.02f) g_fill(xy, 4, fill);
        g_poly(xy, 4, 1, w, a);
        break;
    case OHAT:
        g_arc(x, y, s, r + 0.02f, r + 0.6f + 0.3f * fill, w * 0.7f + 2.0f * fill, a);
        g_disc(x + s * cos_turns(r + 0.02f), y + s * sin_turns(r + 0.02f), 3.5f + 3.0f * fill, a);
        break;
    default:                                     /* CRASH */
        for (i = 0; i < 10; i++) {
            f32 an = (f32)i / 10.0f + r, l0 = 0.3f - 0.2f * fill, l1 = (i & 1 ? 0.7f : 1.0f) * (1.0f + 0.5f * fill);
            g_line(x + cos_turns(an) * s * l0, y + sin_turns(an) * s * l0, x + cos_turns(an) * s * l1, y + sin_turns(an) * s * l1, w * 0.8f, a);
        }
        break;
    }
#undef PX
#undef PY
}

static SMALL void bonk_draw(const Vis *vis)
{
    f32 dt = vis->dt, t = vis->t, tune = vis->k[0], decay = vis->k[1], punch = vis->k[2], dirt = vis->k[3];
    f32 scale = 1.28f - 0.56f * tune, glow_k = 1.0f / (0.12f + 0.9f * decay * decay), ring_k = 1.0f / (0.3f + 0.9f * decay);
    f32 lw = 1.6f + 1.8f * punch;
    int i, j, algo = vis->algo;

    if (vis->fresh) {
        for (i = 0; i < NDRUM; i++) {
            f32 o[4];
            layout(algo, i, t, o);
            sh[i].x = o[0]; sh[i].y = o[1]; sh[i].sz = o[2]; sh[i].rot = o[3];
            sh[i].vx = sh[i].vy = sh[i].vs = sh[i].vr = 0.0f;
            sh[i].e = sh[i].pop = sh[i].popv = sh[i].fall = sh[i].fallv = 0.0f;
            sh[i].ring = 1.0f;
        }
        for (i = 0; i < NVOICE; i++)
            seen_serial[i] = vis->v[i].serial;
    }
    for (i = 0; i < NVOICE; i++)
        if (vis->v[i].serial != seen_serial[i]) {
            Shape *s = &sh[vis->v[i].note % 12];
            seen_serial[i] = vis->v[i].serial;
            s->e = 1.0f;
            s->popv += 9.0f + 22.0f * punch;
            s->ring = 0.0f;
            s->fallv = -(150.0f + 260.0f * punch);
        }
    grain_t += dt;
    if (grain_t > 0.085f) {                      /* the grain and the shake move at twelve a second, like a flick book */
        grain_t = 0.0f;
        grain_seed = rng_next(vis->rng) | 1u;
        for (i = 0; i < NDRUM; i++) {
            jit[i][0] = rng_s(vis->rng);
            jit[i][1] = rng_s(vis->rng);
        }
    }

    for (j = 0; j < NDRUM; j++) {
        Shape *s;
        f32 o[4], bob, x, y, sz, a;
        i = ORDER[j];
        s = &sh[i];
        layout(algo, i, t, o);
        spring(&s->x, &s->vx, o[0], 9.0f, dt);
        spring(&s->y, &s->vy, o[1], 9.0f, dt);
        spring(&s->sz, &s->vs, o[2], 9.0f, dt);
        spring(&s->rot, &s->vr, o[3], 9.0f, dt);
        spring_z(&s->pop, &s->popv, 0.0f, 24.0f, 0.22f, dt);
        s->e = maxf(0.0f, s->e - dt * glow_k * (0.3f + s->e));
        s->ring = minf(1.0f, s->ring + dt * ring_k);
        if (algo == 7) {                         /* JUNK: a hit throws it up, and it comes down again */
            s->fallv += 900.0f * dt;
            s->fall += s->fallv * dt;
            if (s->fall > 0.0f) {
                s->fall = 0.0f;
                s->fallv = s->fallv > 60.0f ? -s->fallv * 0.42f : 0.0f;
            }
        } else
            s->fall = s->fallv = 0.0f;
        bob = sin_turns(t * (0.11f + 0.013f * (f32)i) + (f32)i * 0.31f);
        if (i == OHAT && algo == 0)
            s->rot = o[3] + t * 0.012f;          /* the long arc keeps turning */
        x = s->x + jit[i][0] * dirt * 2.2f + (algo == 6 ? s->pop * 26.0f : 0.0f);
        y = s->y + bob * 2.2f + jit[i][1] * dirt * 2.2f + s->fall - (algo == 6 ? s->pop * 22.0f : 0.0f);
        sz = s->sz * (i == OHAT || algo == 5 ? 0.75f + 0.25f * scale : scale) * (1.0f + s->pop * (i == OHAT || algo == 5 ? 0.25f : 0.8f));
        sz *= 1.0f + 0.025f * (1.0f - vis->beat) * (1.0f - vis->beat) * (j & 1 ? 1.0f : -1.0f);      /* a breath on the beat */
        a = 0.62f + 0.38f * sat(s->e * 3.0f);
        if (sz < 1.0f)
            sz = 1.0f;
        if (s->ring < 1.0f) {                    /* the ring: the shape's own outline, going out */
            f32 rr = ease_out(s->ring), ra = (1.0f - s->ring) * 0.7f;
            shape(i, x, y, sz * (1.0f + rr * (0.5f + 1.0f * decay) * (i == OHAT ? 0.3f : 1.0f)), s->rot + bob * 0.006f, 0.0f, ra, 1.3f);
            if (algo == 6)                       /* ZAP: and a streak behind it */
                g_line(x - 60.0f * (1.0f - s->ring), y + 52.0f * (1.0f - s->ring), x - 14.0f, y + 12.0f, 1.5f, ra);
        }
        {                                        /* DECAY: every shape trails outlines of itself, more the longer it rings */
            f32 gh = decay * 3.0f;
            int g;
            for (g = 1; g <= 3; g++) {
                f32 ga = sat(gh - (f32)(g - 1)) * (0.42f - 0.09f * (f32)g) * (0.7f + 0.6f * sat(s->e * 2.0f));
                if (ga > 0.02f)
                    shape(i, x, y, sz * (1.0f + (0.16f + 0.05f * s->e) * (f32)g * (i == OHAT ? 0.3f : 1.0f)), s->rot + bob * 0.006f, 0.0f, ga, 1.0f);
            }
        }
        shape(i, x, y, sz, s->rot + bob * 0.006f + (i == RIM || i == CRASH ? s->pop * 0.2f : 0.0f), s->e, a, lw);
    }
    if (algo == 7)
        g_line(0.0f, 229.0f, 240.0f, 229.0f, 1.5f, 0.45f);
    if (algo == 3) {                             /* WOOD: the poles the totems hang on */
        g_line(82.0f, 14.0f, 82.0f, 232.0f, 1.2f, 0.3f);
        g_line(158.0f, 14.0f, 158.0f, 232.0f, 1.2f, 0.3f);
    }

    if (dirt > 0.02f) {                          /* DIRT: grain eats holes in the print */
        uint32_t s = grain_seed;
        int n = (int)(dirt * dirt * 150.0f);
        g_mode(G_PAINT);
        for (i = 0; i < n; i++) {
            f32 gx = rng_f(&s) * 240.0f, gy = rng_f(&s) * 240.0f;
            g_disc(gx, gy, 0.8f + rng_f(&s) * 2.0f * dirt, 0.0f);
        }
        g_mode(G_MAX);
        for (i = 0; i < n / 2; i++) {            /* and leaves specks where there was none */
            f32 gx = rng_f(&s) * 240.0f, gy = rng_f(&s) * 240.0f;
            g_disc(gx, gy, 0.5f + rng_f(&s) * 0.9f, 0.25f + 0.4f * rng_f(&s));
        }
    }
}

const InstDef inst_bonk = {
    "BONK",
    {255, 255, 255},
    {"TUNE", "DECAY", "PUNCH", "DIRT"},
    {"BOOM", "SNAP", "DUST", "WOOD", "TIN", "TABLA", "ZAP", "JUNK"},
    {{50, 50, 45, 5}, 0, {0, 50, 100, 75}, {0, 55, 0, 0}, {0, 3, 12, 50}, {0, 3, 1, 60}, {0, 0, 0}},
    0.55f,
    bonk_block, bonk_note, bonk_render, 0, bonk_draw,
};
