/* h1 - TILT: a drum machine that is a pinball table. Twelve drums, one on every note of the octave as on
 * BONK (the octave you play it in tunes it), each of them a small synthesised hit, and each of them a part
 * of the table: the table is a drum kit, and the balls on it are real ones, rolling down it and thrown about
 * by whatever is hit. The algorithm is the kit: it stands the drums somewhere else, paints the floor and
 * changes the ball.
 *
 *   C  kick      the flippers; a ball is served to them if none is there
 *   C# rim       the snare's hoop
 *   D  snare     the big drum in the middle: its head pops, and throws what is on it (the toms too)
 *   D# clap      both slingshots; a ball out of one, then the other
 *   E  F  G      the three toms
 *   F# hat       the plate on the wire spins, and stops soon; A# open hat: it spins on
 *   G# shaker    the beads in the cup jump
 *   A  cowbell   the bell swings
 *   B  crash     the plunger fires what waits in its lane round the top of the table; the cymbal rocks
 *
 * Every hit scores on the backbox, more the harder it is played. Hammered, the table TILTs.
 * The bulbs along the backbox, the arrows up the middle and the lights over the top lanes run in time.
 *
 *   K1 TUNE    every drum up or down                   the drums and the balls shrink or grow
 *   K2 DECAY   how long they ring                      how long a light glows, the tails behind the balls,
 *                                                      how far a hit's ring runs, how long the hat spins
 *   K3 BOUNCE  the pitch drop, then the hit comes      how hard the table throws a ball back, how high one
 *              back again and again, like a ball       hops, how far the drums' heads move with the beat
 *   K4 BITS    fewer samples, fewer levels             the picture falls into blocks, and into fewer shades
 */
#include "internal.h"
#include "dsp.h"

/* ---- sound ------------------------------------------------------------------------------ */
enum { KICK, RIM, SNARE, CLAP, TOML, TOMM, HAT, TOMH, SHAKE, COW, OHAT, CRASH, NDRUM };

typedef struct Drum {
    f32 f0, sweep, tsw, tdec, tone;              /* the tone: Hz, octaves of drop, its time, decay, level */
    f32 nlev, nfc, ndec;                         /* the noise: level, its clock or filter in Hz, decay */
    int8_t deg;                                  /* its note in a tuned kit, semitones over the kit's root */
    uint8_t nmode;                               /* 0 low-pass, 1 band-pass, 2 high-pass */
} Drum;

static const Drum DRUMS[NDRUM] = {
    /* f0    sweep  tsw     tdec   tone  nlev   nfc    ndec   deg mode */
    {55.0f, 2.4f, 0.035f, 0.22f, 1.0f, 0.0f, 2000, 0.010f, 0, 0},       /* kick */
    {880.0f, 0.2f, 0.004f, 0.03f, 0.7f, 0.25f, 6000, 0.010f, 24, 1},    /* rim */
    {196.0f, 0.6f, 0.015f, 0.08f, 0.5f, 0.9f, 5000, 0.13f, 12, 1},      /* snare */
    {392.0f, 0.0f, 0.010f, 0.05f, 0.0f, 1.0f, 1800, 0.15f, 15, 1},      /* clap */
    {98.0f, 0.9f, 0.05f, 0.25f, 1.0f, 0.05f, 900, 0.03f, 3, 0},         /* low tom */
    {147.0f, 0.8f, 0.045f, 0.20f, 1.0f, 0.05f, 1200, 0.03f, 7, 0},      /* mid tom */
    {1568.0f, 0.0f, 0.010f, 0.03f, 0.0f, 0.8f, 11000, 0.035f, 27, 2},   /* hat */
    {220.0f, 0.7f, 0.04f, 0.17f, 1.0f, 0.05f, 1700, 0.03f, 10, 0},      /* high tom */
    {1175.0f, 0.0f, 0.010f, 0.04f, 0.0f, 0.7f, 8000, 0.06f, 22, 1},     /* shaker */
    {587.0f, 0.0f, 0.010f, 0.16f, 0.9f, 0.0f, 2000, 0.01f, 19, 1},      /* cowbell */
    {1760.0f, 0.0f, 0.010f, 0.20f, 0.0f, 0.8f, 11000, 0.30f, 31, 2},    /* open hat */
    {2093.0f, 0.0f, 0.010f, 0.50f, 0.0f, 0.9f, 9000, 0.80f, 34, 2},     /* crash */
};

typedef struct Kit {
    f32 root;                                    /* > 0: a tuned kit. Every drum is a note over this, in Hz */
    f32 pitch, sweep, dec, noise;                /* multipliers */
    f32 fm;                                      /* how far a second sine bends the tone */
    f32 p2r, p2;                                 /* a partial above the tone: its ratio (the bender's too), its level */
    f32 alt;                                     /* how much tone the drums of noise are given */
    uint8_t wave;                                /* 0 sine, 1 a triangle in sixteen steps, 2 pulse, 3 saw */
    uint8_t lfsr;                                /* the noise is a shift register on a clock */
    uint8_t step;                                /* pitch and loudness move sixty times a second, loudness in sixteen steps */
    int8_t updown;                               /* -1: the pitch rises into the note instead */
} Kit;

static const Kit KITS[NALGO] = {
    /* root   pitch  sweep  dec    noise  fm    p2r    p2     alt   wave lfsr step updown */
    {0.0f, 1.0f, 1.0f, 0.8f, 1.0f, 0.0f, 2.0f, 0.0f, 0.0f, 1, 1, 1, 1},          /* 8BIT: a sound chip */
    {0.0f, 0.9f, 0.9f, 1.5f, 0.85f, 0.0f, 1.5f, 0.0f, 0.2f, 0, 0, 0, 1},         /* ANALOG: sines and hiss, long */
    {0.0f, 1.12f, 1.6f, 0.65f, 1.25f, 0.5f, 2.0f, 0.0f, 0.15f, 0, 0, 0, 1},      /* HOUSE: short and hard */
    {0.0f, 1.1f, 1.2f, 1.0f, 0.7f, 2.4f, 1.5f, 0.0f, 0.45f, 0, 0, 0, 1},         /* FM: one sine bends another */
    {0.0f, 2.4f, 3.2f, 0.9f, 0.6f, 0.0f, 2.0f, 0.0f, 0.3f, 3, 1, 0, 1},          /* ZAP: saws that dive */
    {0.0f, 1.6f, 0.4f, 0.3f, 1.1f, 3.5f, 3.37f, 0.3f, 0.6f, 2, 1, 0, -1},        /* GLITCH: never the same twice */
    {523.0f, 1.0f, 0.1f, 0.22f, 0.12f, 0.0f, 2.0f, 0.0f, 1.0f, 0, 0, 0, 1},      /* CLICK: tuned blips, hardly there */
    {0.0f, 0.85f, 2.2f, 1.5f, 1.2f, 1.0f, 1.0f, 0.0f, 0.3f, 0, 0, 0, 1},         /* RAVE: everything through the fuzz */
};

static struct {
    int algo;
    f32 tune, dec, bounce, bits, gap;
} K;
static uint32_t hat_gen;

static void tilt_block(const Blk *b)
{
    K.algo = b->algo & 7;
    K.tune = (b->k[0] - 0.5f) * 24.0f;
    K.dec = exp2fx((b->k[1] - 0.5f) * 5.0f);
    K.bounce = b->k[2];
    K.bits = b->k[3];
    K.gap = b->sr * 7.5f / clampf(b->bpm, 30.0f, 300.0f);      /* a thirty-second note, in samples */
}

static void tilt_note(Voice *v, const Blk *b)
{
    int type = v->note % 12, i;
    (void)b;
    if (!v->u[0]) v->u[0] = 0x91E4C0DEu + v->idx * 977u;
    v->s[8] = (f32)type;
    v->s[4] = v->s[5] = v->s[7] = 1.0f;          /* the tone's and the noise's loudness, the pitch drop */
    v->s[0] = v->s[1] = 0.0f;
    v->s[6] = maxf(0.0f, K.bounce - 0.3f) * 1.1f;        /* how loud it comes back the next time */
    v->s[9] = v->s[10] = v->s[11] = 0.0f;
    v->s[12] = 0.0f;
    v->s[13] = 1.0f;
    for (i = 0; i < 6; i++)
        v->ph[i] = 0.0f;
    v->u[1] = 0;
    v->q[0] = (uint32_t)K.gap;                   /* when */
    v->q[1] = (uint32_t)K.gap;                   /* and how long after that */
    v->q[2] = 0x4A31u + (uint32_t)type;          /* the shift register */
    v->q[3] = 0;
    if (type == HAT)
        hat_gen++;
    v->u[2] = hat_gen;
    v->pan = clampf(((f32)type - 5.5f) * 0.11f, -0.6f, 0.6f);
}

static void tilt_render(Voice *v, const Blk *b, f32 *out, int n)
{
    int type = (int)v->s[8], i;
    const Drum *d = &DRUMS[type >= 0 && type < NDRUM ? type : 0];
    const Kit *k = &KITS[K.algo];
    Svf fn;
    f32 oct = ((f32)v->note - 60.0f - (f32)type) * (1.0f / 12.0f);     /* which octave it was played in */
    f32 semis = K.tune + oct * (k->root > 0.0f ? 12.0f : 5.0f) + b->bend + (K.algo == 5 ? (v->r1 - 0.5f) * 14.0f : 0.0f);
    f32 f0 = (k->root > 0.0f ? k->root * exp2fx((f32)d->deg * (1.0f / 12.0f)) : d->f0 * k->pitch) * exp2fx(semis * (1.0f / 12.0f));
    f32 dec = K.dec * k->dec;
    f32 sweep = d->sweep * k->sweep * (0.35f + 1.3f * K.bounce) * (f32)k->updown;
    f32 kt = 1.0f - onepole_k(d->tdec * dec, b->sr), kn = 1.0f - onepole_k(d->ndec * dec, b->sr);
    f32 ks = 1.0f - onepole_k(d->tsw * (k->updown < 0 ? 3.0f : 1.0f), b->sr);
    f32 at = v->s[4], an = v->s[5], back = v->s[6], sw = v->s[7], ph = v->s[9], ph2 = v->s[10], nacc = v->s[11];
    f32 tone = k->root > 0.0f ? 1.0f : maxf(d->tone, k->alt), nlev = d->nlev * k->noise;
    f32 nclk = d->nfc * (0.8f + 0.4f * v->r2) * exp2fx(semis * (1.0f / 24.0f));
    f32 crush = clampf((k->step ? 0.25f : 0.0f) + 0.85f * K.bits, 0.0f, 0.95f), hstep = 1.0f - crush * 0.93f;
    f32 levels = 2.0f + 46.0f * (1.0f - K.bits) * (1.0f - K.bits), hold = v->s[12], hc = v->s[13];
    f32 duty = type == RIM ? 0.125f : (type == COW ? 0.25f : 0.5f);
    f32 la = v->s[2], lf = v->s[3], nb = 1.0f;
    uint32_t reg = v->q[2] | 1u, tick = v->q[3], per = (uint32_t)(b->sr * (1.0f / 60.0f));
    f32 drive = K.algo == 7 ? 7.0f : 1.4f, dnorm = K.algo == 7 ? 0.75f : 1.0f;
    int wave = k->wave, clap = type == CLAP;

    if (wave == 1 && (type == RIM || type == COW))
        wave = 2;                                /* 8BIT: the two that are not the triangle's */
    if (type == OHAT && v->u[2] != hat_gen)      /* a closed hat chokes the open one */
        kn = kt = 0.995f;
    if (type == SHAKE)
        kn = 1.0f - onepole_k(d->ndec * dec * 0.7f, b->sr);
    svf_set(&fn, k->root > 0.0f && type != HAT && type != OHAT && type != CRASH ? minf(f0 * 1.5f, 9000.0f) : minf(nclk, 14000.0f),
            k->root > 0.0f ? 3.0f : 0.9f, b->isr);
    if (per < 64u)
        per = 64u;

    for (i = 0; i < n; i++) {
        f32 x = 0.0f, y, f, lo, bp, nz;
        if (back > 0.04f && v->u[1] >= v->q[0]) {        /* BOUNCE: it lands again, sooner and softer each time */
            at = an = back;
            sw = 0.5f;
            back *= 0.62f;
            v->q[1] = (v->q[1] * 3u) >> 2;
            if (v->q[1] < 400u)
                back = 0.0f;
            v->q[0] += v->q[1];
            tick = per;
        }
        if (!k->step || ++tick >= per) {         /* 8BIT: the sound chip is told sixty times a second */
            tick = 0;
            la = k->step ? (f32)ifloor(at * 15.0f + 0.5f) * (1.0f / 15.0f) : at;
            lf = minf(f0 * exp2fx(sweep * sw), 11000.0f) * b->isr;
        }
        f = lf;
        if (tone > 0.0f) {
            ph += f;
            if (ph >= 1.0f) ph -= 1.0f;
            ph2 += f * k->p2r;
            if (ph2 >= 1.0f) ph2 -= (f32)(int)ph2;
            switch (wave) {
            case 1: y = ph < 0.5f ? ph * 4.0f - 1.0f : 3.0f - ph * 4.0f; y = (f32)ifloor(y * 7.5f + 0.5f) * (1.0f / 7.5f); break;
            case 2: y = pulse_bl(ph, f, duty) * 0.6f; break;
            case 3: y = saw_bl(ph, f) * 0.6f; break;
            default: y = tsin(ph + k->fm * 0.16f * la * tsin(ph2)); break;
            }
            x = tone * la * (y + k->p2 * la * tsin(ph2));
            at *= kt;
        }
        sw *= ks;
        if (nlev > 0.0f) {
            if (k->lfsr) {                       /* a shift register, moved on by its own clock */
                nacc += nclk * b->isr * (type == CRASH || type == SNARE ? 0.6f : 1.6f);
                if (nacc >= 1.0f) {
                    nacc -= 1.0f;
                    reg = (reg >> 1) | (((reg ^ (reg >> (type == COW || type == RIM ? 6 : 1))) & 1u) << 14);
                    nb = reg & 1u ? 1.0f : -1.0f;
                }
                nz = nb * 0.6f;
            } else {
                nz = noise_s(&v->u[0]);
                SVF_TICK(fn, &v->s[0], nz, lo, bp);
                nz = d->nmode == 0 ? lo : (d->nmode == 1 ? bp * 1.4f : nz - lo - bp * fn.k);
            }
            x += nlev * (k->step ? (f32)ifloor(an * 15.0f + 0.5f) * (1.0f / 15.0f) : an) * nz;
            an *= kn;
            if (clap && (v->u[1] == 420u || v->u[1] == 860u || v->u[1] == 1300u))
                an = 1.0f;
        }
        v->u[1]++;
        x = softclip(x * drive) * dnorm;
        if (crush > 0.01f) {                     /* BITS */
            hc += hstep;
            if (hc >= 1.0f) {
                hc -= 1.0f;
                hold = (f32)ifloor(x * levels + 0.5f) / levels;
            }
            x = hold;
        }
        out[i] = x;
    }
    v->s[2] = la; v->s[3] = lf;
    v->s[4] = at; v->s[5] = an; v->s[6] = back; v->s[7] = sw; v->s[9] = ph; v->s[10] = ph2; v->s[11] = nacc;
    v->s[12] = hold; v->s[13] = hc;
    v->q[2] = reg; v->q[3] = tick;
}

/* ---- picture ----------------------------------------------------------------------------
 * The table is 100 units across and 160 long, 0 at its far end, and it is seen from the player's end:
 * a unit is PS pixels wide where it lies (fewer far off), and what stands on the table stands straight
 * up on the screen. The balls are worked out flat on the table, as real ones, and drawn through that. */
#define PF 858.0f
#define PD 536.5f
#define PH 276.5f
#define PY0 (-392.4f)
#define SQ (PH / PF)                             /* a circle lying on the table: this much of PS flatter than it is wide */
#define NBALL 6
#define NSEG 48
#define GRAV 150.0f
#define FLIP_L 13.0f
#define BELL_X 76.0f
#define BELL_Y 80.0f
#define CYM_X 18.0f
#define CYM_Y 31.0f

enum { W_SOLID = 1, W_DEAD = 2, W_MIR = 4, W_CLOSE = 8, W_LOW = 16, W_KICK = 32, W_RAIL = 64, W_PEG = 128 };
#define WEND 255
/* walls: a byte of flags, then corners (x, y) up to WEND. W_MIR: and again mirrored about x = 46. W_PEG: every
 * corner is a post of its own */
static const uint8_t WALLS[] = {
    W_SOLID | W_DEAD | W_RAIL, 4, 160, 4, 42, 6, 32, 10, 22, 18, 14, 27, 8, 38, 4, 50, 3, 63, 4, 74, 8, 83, 14, 91, 22, 95, 32, 97, 42, 97, 160, WEND,
    W_SOLID | W_DEAD, 88, 160, 88, 44, 87, 35, 83, 28, 77, 21, WEND,                 /* the shooter lane's inner rail */
    W_SOLID, 4, 40, 11, 48, WEND,                                                    /* what turns a ball off the left rail */
    W_SOLID | W_MIR | W_LOW, 34, 11, 34, 19, WEND,                                   /* the four lanes at the top */
    W_SOLID | W_LOW, 46, 9, 46, 17, WEND,
    W_SOLID | W_MIR | W_LOW, 10, 108, 10, 128, 26, 137, WEND,                        /* the lane guides */
    W_SOLID | W_MIR | W_LOW | W_CLOSE, 16, 108, 16, 124, 25, 130, WEND,              /* the slingshots */
    WEND
};
/* what each kit adds to the table, kit after kit, each one's list ending at a WEND */
static const uint8_t KIT_WALLS[] = {
    W_SOLID | W_MIR | W_LOW, 4, 89, 13, 93, 13, 98, 21, 102, WEND, WEND,             /* 8BIT: stairs down both sides */
    W_SOLID | W_MIR | W_LOW, 4, 88, 15, 96, WEND, WEND,                              /* ANALOG: a fader either side */
    WEND,                                                                            /* HOUSE */
    W_SOLID | W_LOW, 36, 112, 41, 109, 46, 108, 51, 109, 56, 112, WEND, WEND,        /* FM: an arch over the flippers */
    W_SOLID | W_PEG, 30, 86, 46, 92, 62, 86, WEND, WEND,                             /* ZAP: three rods */
    W_SOLID | W_LOW, 6, 92, 14, 96, WEND, W_SOLID | W_LOW, 70, 97, 80, 92, WEND,     /* GLITCH: what is left of some rails */
    W_SOLID | W_LOW, 42, 84, 50, 87, WEND, WEND,
    WEND,                                                                            /* CLICK */
    WEND,                                                                            /* RAVE */
};
/* and what moves on it: the kind, where, how long or how far.
 * MV_STEP: a block that marches a step a beat. MV_NEEDLE: a meter's needle, over as far as the kit is loud.
 * MV_SLIDE: a puck going from side to side. MV_JUMP: a bar that is somewhere else after every note.
 * MV_PEND: a metronome's arm. MV_ROTOR: a bar turning, half a turn a beat */
enum { MV_NONE, MV_STEP, MV_SLIDE, MV_NEEDLE, MV_JUMP, MV_PEND, MV_ROTOR };
static const uint8_t MOVER[NALGO][4] = {
    {MV_STEP, 46, 90, 8}, {MV_NEEDLE, 46, 84, 15}, {MV_SLIDE, 46, 110, 16}, {MV_NONE, 0, 0, 0},
    {MV_NONE, 0, 0, 0}, {MV_JUMP, 46, 104, 6}, {MV_PEND, 46, 128, 17}, {MV_ROTOR, 46, 104, 9},
};
/* the drums' own shape in each kit: how wide the body is and how tall, in sixteenths of the drum's size.
 * 8BIT pads (boxes), ANALOG drums, HOUSE decks with a record turning, FM coils, ZAP rods with a ball on top,
 * GLITCH boxes of wire that will not stay put, CLICK pucks, RAVE bumpers with a cap */
static const uint8_t FORM[NALGO][2] = {{16, 14}, {16, 16}, {16, 5}, {0, 22}, {2, 30}, {16, 16}, {16, 3}, {7, 20}};

/* the four drums that are bumpers: snare, low tom, mid tom, high tom. x, y, twice the radius, height */
static const uint8_t DT[4] = {SNARE, TOML, TOMM, TOMH};
static const uint8_t DRUM_AT[4][4] = {{46, 70, 17, 5}, {27, 52, 14, 8}, {65, 50, 12, 7}, {46, 33, 10, 6}};   /* (x, y: see KIT_AT) */

/* where each kit stands them (the same four), so that every kit is its own table */
static const uint8_t KIT_AT[NALGO][4][2] = {
    {{46, 70}, {27, 52}, {65, 50}, {46, 33}},    /* 8BIT: a diamond */
    {{46, 54}, {29, 72}, {63, 72}, {46, 36}},    /* ANALOG: the snare in the middle of the three */
    {{46, 72}, {26, 48}, {66, 48}, {46, 40}},    /* HOUSE: three in a row over it */
    {{56, 72}, {30, 42}, {60, 48}, {35, 60}},    /* FM: a zigzag */
    {{46, 44}, {31, 60}, {61, 60}, {46, 76}},    /* ZAP */
    {{36, 68}, {31, 46}, {62, 44}, {60, 66}},    /* GLITCH: a square, not quite */
    {{52, 76}, {28, 60}, {46, 50}, {64, 40}},    /* CLICK: a stair */
    {{46, 58}, {26, 44}, {68, 44}, {46, 76}},    /* RAVE */
};

/* what springs back to rest: the drums' heads, the shaker's beads, the slingshots' bands, the cymbal, the
 * cowbell, the plunger, the cabinet (rocked). Hz, damping in hundredths, how far it may go in tenths */
enum { POP = 0, BEAD = 4, SL = 8, CYM = 10, BELL, PLUNG, LEAN, NSPR };
static const uint8_t SPR[NSPR][3] = {
    {26, 25, 10}, {26, 25, 10}, {26, 25, 10}, {26, 25, 10}, {13, 20, 255}, {13, 20, 255}, {13, 20, 255}, {13, 20, 255},
    {30, 35, 15}, {30, 35, 15}, {17, 10, 10}, {20, 12, 6}, {22, 30, 10}, {9, 12, 90},
};

typedef struct Ball {
    f32 x, y, vx, vy, z, vz;
    f32 tx[3], ty[3];                            /* where it was on the screen a frame ago, and before */
    uint8_t on, drawn;
} Ball;
static Ball ball[NBALL];
static uint8_t seg[NSEG][5];                     /* the walls as strokes: ax, ay, bx, by, flags */
static int nseg;
static uint32_t seen_serial[NVOICE];
static f32 beat_prev;
static f32 PX, PY, PS;                           /* the last point put through pj */
static struct {
    f32 e[NDRUM];                                /* each drum's light */
    f32 fa, ft;                                  /* the flippers: 0 down .. 1 up, and how much longer they are held up */
    f32 s[NSPR], sv[NSPR];                       /* what springs back: where each is (see SPR), and how fast */
    f32 dx[4], dy[4], dvx[4], dvy[4];            /* where the drums stand, and how they are moving there */
    f32 spin, spinw, spink;                      /* the hat: turns, turns a second, its drag */
    f32 heat, tilt;                              /* how hard it has been played, TILT's seconds left */
    f32 feed, chase, bump, flash;
    f32 ax, ay, bx, by, mw, mphi, mr, vu;        /* what moves: from a to b, turning at mw about a, mr thick; the meter */
    int mt, kit;                                 /* its kind; the kit the walls are of, + 1 */
    uint32_t score;                              /* in tens */
    uint32_t seed;
    int side;
} M;

static void pj(f32 x, f32 y, f32 z)
{
    PS = PF / (PD - y);
    PY = PY0 + (PH - z) * PS;
    PX = 120.0f + (x - 50.0f) * PS + M.s[LEAN] * (238.0f - PY) * (1.0f / 188.0f);
}

/* a stroke between two points of the table, w wide at the near end */
SMALL static void tline(f32 x0, f32 y0, f32 z0, f32 x1, f32 y1, f32 z1, f32 w, f32 a)
{
    f32 ax, ay, as;
    pj(x0, y0, z0);
    ax = PX; ay = PY; as = PS;
    pj(x1, y1, z1);
    g_line(ax, ay, PX, PY, w * (as + PS) * 0.22f, a);
}

/* and one that lies on the floor */
SMALL static void fline(f32 x0, f32 y0, f32 x1, f32 y1, f32 w, f32 a)
{
    tline(x0, y0, 0.0f, x1, y1, 0.0f, w, a);
}

/* An oval. w <= 0: filled. (Cheaper than g_ellipse by far: a path of strokes, or an eight-cornered fill
 * that the oval's own outline, drawn after it, rounds off.) */
static const int8_t CIRC[24][2] = {{127, 0}, {123, 33}, {110, 63}, {90, 90}, {64, 110}, {33, 123}, {0, 127}, {-33, 123}, {-63, 110}, {-90, 90}, {-110, 63}, {-123, 33}, {-127, 0}, {-123, -33}, {-110, -63}, {-90, -90}, {-64, -110}, {-33, -123}, {0, -127}, {33, -123}, {64, -110}, {90, -90}, {110, -64}, {123, -33}};
SMALL static void oval(f32 X, f32 Y, f32 rx, f32 ry, f32 w, f32 a)
{
    f32 xy[48];
    int step = w <= 0.0f || rx < 6.0f ? 3 : (rx < 16.0f ? 2 : 1), n = 0, k;
    rx *= w <= 0.0f ? 1.04f / 127.0f : 1.0f / 127.0f;
    ry *= w <= 0.0f ? 1.04f / 127.0f : 1.0f / 127.0f;
    for (k = 0; k < 24; k += step, n++) {
        xy[2 * n] = X + (f32)CIRC[k][0] * rx;
        xy[2 * n + 1] = Y + (f32)CIRC[k][1] * ry;
    }
    if (w <= 0.0f)
        g_fill(xy, 8, a);
    else
        g_poly(xy, n, 1, w, a);
}

/* a circle lying flat on the table, r across; w <= 0: filled */
SMALL static void tell(f32 x, f32 y, f32 z, f32 r, f32 w, f32 a)
{
    pj(x, y, z);
    oval(PX, PY, r * PS, r * PS * PS * SQ, w, a);
}

/* ---- the balls ---- */
/* The ball against a stroke from a to b, rad away from it (a point when a is b). The stroke turns about a at
 * w radians a second (a flipper). Returns how fast the ball came into it, 0 when it did not. */
SMALL static f32 bump(Ball *b, f32 ax, f32 ay, f32 bx, f32 by, f32 rad, f32 rest, f32 w)
{
    f32 dx = bx - ax, dy = by - ay, qx = b->x - ax, qy = b->y - ay;
    f32 t = sat((qx * dx + qy * dy) / (dx * dx + dy * dy + 1e-6f));
    f32 nx = qx - dx * t, ny = qy - dy * t, d2 = nx * nx + ny * ny, id, vn;
    if (d2 >= rad * rad || d2 < 1e-6f)
        return 0.0f;
    id = rsqrtf(d2);
    nx *= id; ny *= id;
    b->x += nx * (rad - d2 * id);
    b->y += ny * (rad - d2 * id);
    vn = (b->vx + w * qy) * nx + (b->vy - w * qx) * ny;
    if (vn >= 0.0f)
        return 0.0f;
    b->vx -= (1.0f + rest) * vn * nx;
    b->vy -= (1.0f + rest) * vn * ny;
    return -vn;
}

SMALL static Ball *ball_new(void)
{
    Ball *b = &ball[0];
    int i;
    for (i = 0; i < NBALL; i++) {
        if (!ball[i].on) {
            b = &ball[i];
            break;
        }
        if (ball[i].x < 88.0f && (b->x >= 88.0f || ball[i].y > b->y))
            b = &ball[i];                        /* none free: the one furthest down the table goes */
    }
    b->z = b->vz = b->vx = b->vy = 0.0f;
    b->on = 1;
    b->drawn = 2;                                /* (no tail yet) */
    return b;
}

/* Something on the table throws: the balls that are at it fly off, and if none was there one comes out of it.
 * dir: which way, in turns; < 0: any */
SMALL static void throw(f32 cx, f32 cy, f32 R, f32 speed, f32 dir, uint32_t *rng)
{
    Ball *b;
    f32 c, s;
    int i, any = 0;
    for (i = 0; i < NBALL; i++) {
        f32 dx, dy, d2, k;
        b = &ball[i];
        dx = b->x - cx; dy = b->y - cy;
        d2 = dx * dx + dy * dy;
        if (!b->on || d2 > (R + 6.0f) * (R + 6.0f))
            continue;
        k = speed * rsqrtf(d2 + 0.01f);
        b->vx = dx * k; b->vy = dy * k;
        b->vz = speed * 0.25f;
        any = 1;
    }
    if (any)
        return;
    if (dir < 0.0f)
        dir = rng_f(rng);
    c = cos_turns(dir); s = sin_turns(dir);
    b = ball_new();
    b->x = cx + c * (R + 3.0f); b->y = cy + s * (R + 3.0f);
    b->vx = c * speed; b->vy = s * speed;
    b->vz = speed * 0.25f;
}

SMALL static void physics(f32 dt, f32 rad, f32 size, f32 rest, f32 hop)
{
    int n = iclamp((int)(dt * 300.0f) + 1, 1, 12), s, i, j;
    f32 h = dt / (f32)n;
    for (s = 0; s < n; s++) {
        f32 was = M.fa, w, c, sn, yp = 151.0f + 5.0f * M.s[PLUNG];
        M.fa = sat(M.fa + (M.ft > 0.0f ? 30.0f : -11.0f) * h);
        M.ft -= h;
        w = (M.fa - was) / h * -1.05f;
        c = cos_turns(0.08f - 0.167f * M.fa) * FLIP_L;
        sn = sin_turns(0.08f - 0.167f * M.fa) * FLIP_L;
        for (i = 0; i < NBALL; i++) {
            Ball *b = &ball[i];
            f32 v, hitv = 0.0f, sp2;
            int ix, iy;
            if (!b->on)
                continue;
            b->vy += GRAV * h;
            b->vx -= b->vx * 0.3f * h;
            b->vy -= b->vy * 0.3f * h;
            sp2 = b->vx * b->vx + b->vy * b->vy;
            if (sp2 > 420.0f * 420.0f) {
                v = 420.0f * rsqrtf(sp2);
                b->vx *= v; b->vy *= v;
            }
            b->x += b->vx * h;
            b->y += b->vy * h;
            ix = (int)clampf(b->x, -50.0f, 300.0f);
            iy = (int)clampf(b->y, -50.0f, 300.0f);
            for (j = 0; j < nseg; j++) {
                const uint8_t *g = seg[j];
                if (ix + 5 < imin(g[0], g[2]) || ix - 5 > imax(g[0], g[2]) || iy + 5 < imin(g[1], g[3]) || iy - 5 > imax(g[1], g[3]))
                    continue;
                v = bump(b, (f32)g[0], (f32)g[1], (f32)g[2], (f32)g[3], rad, g[4] & W_DEAD ? 0.12f : rest, 0.0f);
                if (v > 25.0f && (g[4] & W_KICK)) {      /* a slingshot kicks what runs into it */
                    b->vx *= 1.5f; b->vy *= 1.5f;
                    M.sv[SL + (g[0] > 46)] += 14.0f;
                }
                hitv = maxf(hitv, v);
            }
            for (j = 0; j < 4; j++) {            /* the drums */
                v = bump(b, M.dx[j], M.dy[j], M.dx[j], M.dy[j], rad + (f32)DRUM_AT[j][2] * 0.5f * size, rest, 0.0f);
                if (v > 0.0f) {
                    M.sv[POP + j] += v * 0.06f;
                    b->vx *= 1.12f; b->vy *= 1.12f;
                }
                hitv = maxf(hitv, v);
            }
            v = bump(b, BELL_X, BELL_Y, BELL_X, BELL_Y, rad + 4.0f, rest, 0.0f);
            if (v > 0.0f)
                M.sv[BELL] += (b->vx > 0.0f ? -0.03f : 0.03f) * v;
            hitv = maxf(hitv, v);
            hitv = maxf(hitv, bump(b, 75.0f, 26.0f, 75.0f, 26.0f, rad + 5.5f, rest, 0.0f));
            bump(b, CYM_X, CYM_Y, CYM_X, CYM_Y, rad + 1.2f, rest, 0.0f);
            bump(b, 11.0f, 82.0f, 11.0f, 82.0f, rad + 1.0f, rest, 0.0f);
            bump(b, 23.0f, 82.0f, 23.0f, 82.0f, rad + 1.0f, rest, 0.0f);
            if (absf(b->x - 17.0f) < 5.0f && absf(b->y - 82.0f) < 1.5f) {      /* through the hat: it spins */
                M.spinw += b->vy * 0.012f;
                M.spink = 2.5f;
            }
            hitv = maxf(hitv, bump(b, 28.0f, 138.0f, 28.0f + c, 138.0f + sn, rad + 1.5f, 0.45f, w));
            hitv = maxf(hitv, bump(b, 64.0f, 138.0f, 64.0f - c, 138.0f + sn, rad + 1.5f, 0.45f, -w));
            bump(b, 88.0f, yp, 97.0f, yp, rad, 0.2f, 0.0f);
            if (M.mt) {
                hitv = maxf(hitv, bump(b, M.ax, M.ay, M.bx, M.by, rad + M.mr, rest, M.mw));
                if (M.mt == MV_ROTOR)
                    hitv = maxf(hitv, bump(b, M.ax, M.ay, 2.0f * M.ax - M.bx, 2.0f * M.ay - M.by, rad + M.mr, rest, M.mw));
            }
            if (hitv > 45.0f && b->z <= 0.0f)
                b->vz = hitv * hop;
            b->vz -= 420.0f * h;
            b->z += b->vz * h;
            if (b->z < 0.0f)
                b->z = b->vz = 0.0f;
            if (b->x < 88.0f && b->vx * b->vx + b->vy * b->vy < 16.0f) {      /* wedged somewhere: knocked loose */
                M.side ^= 1;
                b->vx = M.side ? 45.0f : -45.0f;
                b->vy = -45.0f;
                b->vz = 35.0f;
            }
            if (b->y > 157.0f || b->y < -20.0f || !(b->x > -20.0f && b->x < 120.0f))
                b->on = 0;
        }
    }
}

/* ---- a note ---- */
SMALL static void hit(int type, f32 v, uint32_t *rng)
{
    static const uint8_t PTS[NDRUM] = {1, 5, 50, 25, 10, 20, 1, 30, 2, 15, 3, 100};
    f32 s = 0.45f + 0.55f * v, sp = 75.0f + 95.0f * v;
    int d = -1, i;
    M.e[type] = s;
    M.score = (M.score + PTS[type] * (uint32_t)(1 + (int)(v * 3.9f))) % 1000000u;
    M.bump = 1.0f;
    M.heat += type == HAT || type == OHAT || type == SHAKE ? s * 0.3f : s;
    switch (type) {
    case KICK:
        M.ft = 0.11f;
        for (i = 0; i < NBALL; i++)
            if (ball[i].on && ball[i].y > 122.0f && ball[i].x > 22.0f && ball[i].x < 70.0f)
                break;
        if (i == NBALL) {                        /* nothing for the flippers to hit: one is served */
            M.side ^= 1;
            throw(M.side ? 56.0f : 36.0f, 131.0f, 0.0f, sp + 25.0f, M.side ? 0.72f : 0.78f, rng);
        }
        break;
    case SNARE: d = 0; break;
    case TOML: d = 1; break;
    case TOMM: d = 2; break;
    case TOMH: d = 3; break;
    case CLAP:
        M.sv[SL] += 40.0f * s;
        M.sv[SL + 1] += 40.0f * s;
        M.side ^= 1;
        throw(M.side ? 71.5f : 20.5f, 119.0f, 0.0f, sp, M.side ? 0.562f : 0.938f, rng);
        break;
    case HAT:
        M.spinw = 7.0f + 5.0f * v;
        M.spink = 7.0f;
        break;
    case OHAT:
        M.spinw = 11.0f + 7.0f * v;
        M.spink = 1.3f;
        break;
    case SHAKE:
        for (i = 0; i < 4; i++)
            M.sv[BEAD + i] = (25.0f + 60.0f * rng_f(rng)) * s;
        break;
    case COW:
        M.sv[BELL] += (rng_next(rng) & 1u ? 7.0f : -7.0f) * s;
        throw(BELL_X, BELL_Y, 4.0f, sp * 0.8f, -1.0f, rng);
        break;
    case CRASH:
        M.sv[PLUNG] = -60.0f;
        M.sv[CYM] += 16.0f * s;
        M.flash = 1.0f;
        M.feed = 0.6f;
        for (i = 0; i < NBALL; i++)
            if (ball[i].on && ball[i].x > 88.0f && ball[i].y > 110.0f) {
                ball[i].vx = 0.0f;
                ball[i].vy = -(330.0f + 80.0f * v);
            }
        break;
    default: break;
    }
    if (d >= 0) {
        M.sv[POP + d] += 10.0f + 26.0f * s;
        throw(M.dx[d], M.dy[d], (f32)DRUM_AT[d][2] * 0.5f, sp, -1.0f, rng);
    }
}

/* ---- drawing ---- */
/* a plate in front of what is behind it: lit e inside, its edge w wide */
SMALL static void plate(f32 X, f32 Y, f32 rx, f32 ry, f32 e, f32 w, f32 a)
{
    g_mode(G_PAINT);
    oval(X, Y, rx + 1.5f, ry + 1.5f, 0.0f, 0.0f);
    g_mode(G_MAX);
    if (e > 0.02f)
        oval(X, Y, rx, ry, 0.0f, e);
    oval(X, Y, rx, ry, w, a);
}

/* the side of something round that stands on the table, from Yb up to Yt, as light as shade; its edges a */
SMALL static void shell(f32 X, f32 Yb, f32 Yt, f32 rx, f32 ry, f32 shade, f32 a)
{
    plate(X, Yb, rx, ry, shade, 1.3f, 0.6f);
    g_mode(G_PAINT);
    g_rect(X - rx - 1.5f, Yt, 2.0f * rx + 3.0f, Yb - Yt, 0.0f);
    g_rect(X - rx, Yt, 2.0f * rx, Yb - Yt, shade);
    g_mode(G_MAX);
    g_line(X - rx, Yt, X - rx, Yb, 1.3f, a);
    g_line(X + rx, Yt, X + rx, Yb, 1.3f, a);
}

/* a box standing on the table, r from its middle to a side, h tall, its top lit e. wire: only its edges */
SMALL static void box(f32 x, f32 y, f32 r, f32 h, f32 e, f32 shade, int wire)
{
    static const uint8_t FACE[4][4] = {{0, 3, 7, 4}, {1, 2, 6, 5}, {3, 2, 6, 7}, {4, 5, 6, 7}};      /* two sides, the front, the top */
    f32 p[16], q[8];
    int f, j;
    for (j = 0; j < 8; j++) {                    /* the floor's corners, far left, far right, near right, near left; then the top's */
        pj(x + ((j + 1) & 2 ? r : -r), y + (j & 2 ? r : -r), j & 4 ? h : 0.0f);
        p[2 * j] = PX; p[2 * j + 1] = PY;
    }
    for (f = 0; f < 4; f++) {
        for (j = 0; j < 4; j++) {
            q[2 * j] = p[2 * FACE[f][j]];
            q[2 * j + 1] = p[2 * FACE[f][j] + 1];
        }
        if (!wire) {
            g_mode(G_PAINT);
            g_fill(q, 4, f == 3 ? 0.0f : (f == 2 ? shade : shade * 0.5f));
        }
        g_mode(G_MAX);
        if (f == 3 && e > 0.02f)
            g_fill(q, 4, e);
        g_poly(q, 4, 1, 1.3f, f == 3 ? 0.9f : 0.6f);
    }
}

/* a drum standing on the table, in its kit's shape. e: its head's light. rim: its hoop's */
SMALL static void draw_drum(int i, int algo, f32 size, f32 e, f32 rim, f32 pulse, f32 t)
{
    f32 r = (f32)DRUM_AT[i][2] * 0.5f * size, sr = (f32)FORM[algo][0] * (1.0f / 16.0f), X, Yb, Yt, rx, ry, k;
    f32 h = (f32)(DRUM_AT[i][3] * FORM[algo][1]) * (1.0f / 16.0f) * (1.0f - 0.22f * M.s[POP + i]);
    f32 shade = 0.12f + 0.3f * rim, edge = 0.6f + 0.4f * rim;
    int j;
    if (algo == 0 || algo == 5) {
        box(M.dx[i] + (algo == 5 ? rng_s(&M.seed) * 4.0f * e : 0.0f), M.dy[i], r * 0.8f, h, e, shade, algo == 5);
        return;
    }
    pj(M.dx[i], M.dy[i], 0.0f);
    X = PX; Yb = PY;
    rx = r * PS; ry = rx * PS * SQ;
    Yt = Yb - h * PS;
    if (sr < 1.0f)                               /* narrower than what a ball runs into: that is drawn on the floor */
        oval(X, Yb, rx, ry, 1.0f, 0.35f + 0.5f * e);
    if (sr > 0.0f)
        shell(X, Yb, Yt, rx * sr, ry * sr, shade, edge);
    if (algo == 1)
        for (j = -1; j <= 1; j++) {              /* the lugs */
            f32 u = (f32)j * 0.56f, yo = ry * sqrtfx(1.0f - u * u);
            g_line(X + rx * u, Yt + yo + 2.5f, X + rx * u, Yb + yo - 2.5f, 1.0f, 0.4f + 0.6f * rim);
        }
    if (algo == 3)                               /* a coil: turns of it, swaying when it is hit */
        for (j = 1; j < 3; j++)
            oval(X + (j == 1 ? 2.5f : -2.5f) * M.s[POP + i], Yb + (Yt - Yb) * (f32)j * (1.0f / 3.0f), rx, ry, 1.2f, 0.5f);
    if (algo == 4) {                             /* a rod: the ball on top of it */
        g_mode(G_PAINT);
        g_disc(X, Yt, rx * 0.34f + 1.5f, 0.0f);
        g_mode(G_MAX);
        g_disc(X, Yt, rx * 0.34f, 0.25f + 0.75f * e);
        g_ring(X, Yt, rx * 0.34f, 1.2f, 0.9f);
        return;
    }
    if (algo == 7)                               /* a cap: its underside */
        plate(X, Yt + 2.5f, rx, ry, 0.0f, 1.3f, 0.6f);
    plate(X, Yt, rx, ry, e, 1.5f + 2.0f * rim, 0.85f);
    k = clampf(0.55f + 0.2f * pulse - 0.3f * M.s[POP + i], 0.15f, 0.9f);
    if (algo == 2) {                             /* a record: its label, and the light on it going round */
        g_line(X + cos_turns(t * 0.55f) * rx * 0.35f, Yt + sin_turns(t * 0.55f) * ry * 0.35f, X + cos_turns(t * 0.55f) * rx * 0.9f, Yt + sin_turns(t * 0.55f) * ry * 0.9f, 1.3f, 0.7f);
        k = 0.3f;
    }
    if (algo == 6) {                             /* a puck: a mark to aim at */
        g_line(X - rx * 0.6f, Yt, X + rx * 0.6f, Yt, 1.0f, 0.6f);
        g_line(X, Yt - ry * 0.6f, X, Yt + ry * 0.6f, 1.0f, 0.6f);
    }
    oval(X, Yt, rx * k, ry * k, 1.0f, 0.45f);
}

/* the burst round something that was hit: n rays, further out and fainter as e runs down */
SMALL static void rays(f32 X, f32 Y, f32 rx, f32 ry, f32 e, int n)
{
    int k;
    if (e < 0.04f)
        return;
    for (k = 0; k < n; k++) {
        f32 a = ((f32)k + 0.5f) / (f32)n, c = cos_turns(a), s = sin_turns(a), r0 = 1.3f + (1.0f - e) * 0.9f, r1 = r0 + 0.5f * e;
        g_line(X + c * rx * r0, Y + s * ry * r0, X + c * rx * r1, Y + s * ry * r1, 1.3f, e);
    }
}

/* a light set in the table's floor */
SMALL static void lamp(f32 x, f32 y, f32 r, f32 e)
{
    f32 rx;
    pj(x, y, 0.0f);
    rx = r * PS;
    if (e > 0.03f)
        oval(PX, PY, rx, rx * PS * SQ, 0.0f, e);
    oval(PX, PY, rx, rx * PS * SQ, 1.0f, 0.3f);
}

SMALL static void draw_ball(Ball *b, f32 rad, int tails, int algo, f32 t)
{
    f32 r;
    int g;
    if (b->z > 0.4f) {                           /* off the table: where it will come down */
        pj(b->x, b->y, 0.0f);
        oval(PX, PY, rad * PS * 0.8f, rad * PS * PS * SQ * 0.8f, 1.0f, 0.35f);
    }
    pj(b->x, b->y, b->z + rad);
    r = rad * PS * 0.92f;
    if (b->drawn == 2)
        for (g = 0; g < 3; g++) {
            b->tx[g] = PX; b->ty[g] = PY;
        }
    if (algo == 0) {                             /* 8BIT: everything on its grid */
        PX = (f32)(ifloor(PX * 0.5f) * 2);
        PY = (f32)(ifloor(PY * 0.5f) * 2);
    }
    for (g = tails - 1; g >= 0; g--)
        g_disc(b->tx[g], b->ty[g], r * (0.8f - 0.18f * (f32)g), 0.42f - 0.12f * (f32)g);
    g_mode(G_PAINT);
    g_disc(PX, PY, r + 1.5f, 0.0f);
    g_mode(G_MAX);
    switch (algo) {
    case 0:                                      /* 8BIT: a block */
        g_rect(PX - r, PY - r, 2.0f * r, 2.0f * r, 1.0f);
        g_mode(G_PAINT);
        g_rect(PX, PY, r, r, 0.45f);
        break;
    case 3:                                      /* FM: a ring, and a moon going round it */
        g_ring(PX, PY, r * 0.8f, r * 0.42f, 1.0f);
        g_disc(PX + cos_turns(t * 2.0f) * r * 1.7f, PY + sin_turns(t * 2.0f) * r * 0.8f, r * 0.3f, 1.0f);
        break;
    case 4:                                      /* ZAP: a spark */
    case 5:                                      /* GLITCH: a splinter, never the same one */
        for (g = 0; g < 3; g++) {
            f32 a = (algo == 4 ? t * 0.9f : (f32)(rng_next(&M.seed) & 255u) * (1.0f / 256.0f)) + (f32)g * (1.0f / 3.0f);
            f32 l = r * (algo == 4 ? 1.5f : 0.6f + (f32)(rng_next(&M.seed) & 15u) * 0.08f);
            g_line(PX - cos_turns(a) * l, PY - sin_turns(a) * l, PX + cos_turns(a) * l, PY + sin_turns(a) * l, algo == 4 ? 1.4f : 2.4f, 1.0f);
        }
        g_disc(PX, PY, r * 0.5f, 1.0f);
        break;
    case 6:                                      /* CLICK: a dot in a ring */
        g_ring(PX, PY, r, 1.0f, 0.7f);
        g_disc(PX, PY, r * 0.4f, 1.0f);
        break;
    case 7:                                      /* RAVE: a light */
        g_glow(PX, PY, r * 2.6f, 0.9f);
        g_disc(PX, PY, r * 0.6f, 1.0f);
        break;
    default:                                     /* a steel ball; HOUSE: a mirror ball */
        g_disc(PX, PY, r, 1.0f);
        g_mode(G_PAINT);
        if (algo == 2) {
            g_line(PX - r, PY, PX + r, PY, 1.0f, 0.0f);
            g_line(PX - r * 0.5f, PY - r, PX - r * 0.5f, PY + r, 1.0f, 0.0f);
            g_line(PX + r * 0.5f, PY - r, PX + r * 0.5f, PY + r, 1.0f, 0.0f);
        } else
            g_disc(PX + r * 0.3f, PY + r * 0.34f, r * 0.5f, 0.42f);
        break;
    }
    g_mode(G_MAX);
    b->tx[2] = b->tx[1]; b->ty[2] = b->ty[1];
    b->tx[1] = b->tx[0]; b->ty[1] = b->ty[0];
    b->tx[0] = PX; b->ty[0] = PY;
    b->drawn = 1;
}

/* What is painted on the table's floor: the kit's own. */
SMALL static void floor_art(const Vis *vis, int algo, f32 size)
{
    f32 sx = M.dx[0], sy = M.dy[0], x, y;
    int i, j;
    switch (algo) {
    case 0:                                      /* 8BIT: squared paper */
        for (i = 0; i < 13; i++)
            fline(i < 3 ? 15.0f : 5.0f, 20.0f + 10.0f * (f32)i, i < 3 ? 80.0f : 87.0f, 20.0f + 10.0f * (f32)i, 1.0f, 0.13f);
        for (i = 0; i < 7; i++)
            fline(16.0f + 10.0f * (f32)i, 14.0f, 16.0f + 10.0f * (f32)i, 150.0f, 1.0f, 0.13f);
        break;
    case 1:                                      /* ANALOG: a dial round every drum */
        for (i = 0; i < 4; i++) {
            f32 r = (f32)DRUM_AT[i][2] * 0.5f * size + 4.0f;
            tell(M.dx[i], M.dy[i], 0.0f, r, 1.0f, 0.2f);
            for (j = 0; j < 24; j += 2)
                fline(M.dx[i] + (f32)CIRC[j][0] * (r + 1.5f) * (1.0f / 127.0f), M.dy[i] + (f32)CIRC[j][1] * (r + 1.5f) * (1.0f / 127.0f), M.dx[i] + (f32)CIRC[j][0] * (r + 4.0f) * (1.0f / 127.0f), M.dy[i] + (f32)CIRC[j][1] * (r + 4.0f) * (1.0f / 127.0f), 1.0f, 0.2f);
        }
        break;
    case 2:                                      /* HOUSE: a dance floor, the tile of the beat lit */
        for (i = 0; i < 4; i++) {
            fline(16.0f + 20.0f * (f32)i, 26.0f, 16.0f + 20.0f * (f32)i, 86.0f, 1.0f, 0.18f);
            fline(16.0f, 26.0f + 20.0f * (f32)i, 76.0f, 26.0f + 20.0f * (f32)i, 1.0f, 0.18f);
        }
        i = (int)M.chase & 3;
        x = i == 1 || i == 2 ? 56.0f : 16.0f;
        y = i >= 2 ? 66.0f : 26.0f;
        for (j = 2; j < 20; j += 3)              /* (hatched: a fill would hide what rolls over it) */
            fline(x + (f32)j, y + 1.0f, x + (f32)j, y + 19.0f, 1.0f, 0.16f + 0.3f * (1.0f - vis->beat));
        break;
    case 3:                                      /* FM: rings from two drums, crossing */
        for (i = 0; i < 5; i++) {
            tell(sx, sy, 0.0f, 12.0f + 6.0f * (f32)i, 1.0f, 0.15f);
            tell(M.dx[3], M.dy[3], 0.0f, 9.0f + 6.0f * (f32)i, 1.0f, 0.15f);
        }
        break;
    case 4:                                      /* ZAP: arcs from the snare to the toms, alive while either rings */
        for (i = 1; i < 4; i++) {
            f32 e = maxf(M.e[SNARE], M.e[DT[i]]), ux = (M.dx[i] - sx) * 0.2f, uy = (M.dy[i] - sy) * 0.2f, px = sx, py = sy;
            for (j = 1; j <= 5; j++) {
                f32 k = j == 5 ? 0.0f : ((j & 1) ? 0.35f : -0.35f) * (e > 0.05f ? rng_s(&M.seed) * 2.0f : 1.0f);
                x = sx + ux * (f32)j - uy * k;
                y = sy + uy * (f32)j + ux * k;
                fline(px, py, x, y, 1.2f + e, 0.16f + 0.84f * e);
                px = x; py = y;
            }
        }
        break;
    case 5: {                                    /* GLITCH: scraps of scan line, somewhere else at every note */
        uint32_t h = vis->notes * 2654435761u + 77u;
        for (i = 0; i < 12; i++) {
            x = 8.0f + (f32)(rng_next(&h) % 60u);
            y = 22.0f + (f32)(rng_next(&h) % 120u);
            fline(x, y, x + 4.0f + (f32)(rng_next(&h) % 16u), y, 1.0f + (f32)(h & 2u), 0.22f);
        }
        break;
    }
    case 6:                                      /* CLICK: register marks */
        for (i = 0; i < 4; i++)
            for (j = 0; j < 6; j++) {
                x = 16.0f + 20.0f * (f32)i;
                y = 24.0f + 22.0f * (f32)j;
                fline(x - 1.5f, y, x + 1.5f, y, 1.0f, 0.3f);
                fline(x, y - 2.0f, x, y + 2.0f, 1.0f, 0.3f);
            }
        break;
    default:                                     /* RAVE: a sunburst from the snare, every other ray on every other beat */
        for (j = 0; j < 24; j += 2) {
            int lit = ((j >> 1) ^ (int)M.chase) & 1;
            f32 r1 = lit ? 34.0f : 24.0f, c = (f32)CIRC[j][0] * (1.0f / 127.0f), sn = (f32)CIRC[j][1] * (1.0f / 127.0f);
            fline(sx + c * 13.0f, sy + sn * 13.0f, sx + c * r1, sy + sn * r1, 1.4f, lit ? 0.2f + 0.5f * (1.0f - vis->beat) * (1.0f - vis->beat) : 0.14f);
        }
        break;
    }
}

/* the backbox: the score, the bulbs round it, the plumb bob that knows how hard the table is played */
SMALL static void backbox(const Vis *vis, f32 tail)
{
    static const f32 BOX[8] = {28.0f, 2.0f, 212.0f, 2.0f, 212.0f, 46.0f, 28.0f, 46.0f};
    static const f32 WIN[8] = {62.0f, 13.0f, 180.0f, 13.0f, 180.0f, 41.0f, 62.0f, 41.0f};
    char s[8];
    uint32_t v = M.score;
    f32 sw;
    int i, n = 0;
    g_origin(M.s[LEAN] * 1.13f, 0.0f);
    g_poly(BOX, 4, 1, 1.6f, 0.7f);
    g_poly(WIN, 4, 1, 1.0f, 0.35f);
    for (i = 0; i < 13; i++) {                   /* the bulbs: a light runs along them in time */
        f32 ph = fractf(M.chase * 0.5f - (f32)i * (1.0f / 13.0f)), e = maxf(M.flash, sat(1.0f - ph * tail));
        g_disc(36.0f + (f32)i * 14.0f, 7.5f, 1.2f + 1.0f * e, 0.25f + 0.75f * e);
    }
    s[6] = '0';
    s[7] = 0;
    for (i = 5; i >= 0; i--) {
        s[i] = (char)('0' + v % 10u);
        v /= 10u;
    }
    if (M.tilt > 0.0f) {
        if (fractf(M.tilt * 5.0f) > 0.4f)
            g_text(121.0f, 18.0f, "TILT", 18.0f, 2.6f, 1.0f, 1);
    } else {
        f32 x0 = 176.0f - g_text_w(s, 16.0f);
        char z[8];
        for (i = 0; s[i] == '0' && i < 6; i++)
            z[i] = '0';
        z[i] = 0;
        g_text(x0, 19.0f, z, 16.0f, 1.3f, 0.22f, 0);
        g_text(176.0f, 19.0f, s + i, 16.0f, 1.8f + 0.8f * M.bump, 1.0f, 2);
    }
    /* the bob */
    sw = sin_turns(vis->t * 1.3f) * minf(M.heat * 0.04f + (M.tilt > 0.0f ? 1.0f : 0.0f), 1.0f);
    g_ring(45.0f, 31.0f, 8.0f, 1.0f, 0.4f);
    g_line(45.0f, 13.0f, 45.0f + sw * 8.0f, 31.0f, 1.0f, 0.5f);
    g_disc(45.0f + sw * 8.0f, 31.0f, 2.4f, 1.0f);
    /* the balls in play */
    for (i = 0; i < NBALL; i++)
        n += ball[i].on;
    for (i = 0; i < NBALL; i++)
        g_disc(190.0f + (f32)(i % 2) * 11.0f, 18.0f + (f32)(i / 2) * 9.0f, i < n ? 2.6f : 1.2f, i < n ? 1.0f : 0.3f);
    g_origin(0.0f, 0.0f);
}

/* BITS: the picture falls into blocks q pixels across, and into fewer shades */
SMALL static void mosaic(int q)
{
    int bx, by, x, y, sh = q + 1;
    for (by = 0; by < FB_H; by += q) {
        G_POLL();
        for (bx = 0; bx < FB_W; bx += q) {
            int v = 0, x1 = imin(bx + q, FB_W), y1 = imin(by + q, FB_H);
            for (y = by; y < y1; y++)
                for (x = bx; x < x1; x++)
                    v = imax(v, g_fb[y * FB_W + x]);
            v = (v >> sh) * 255 / (255 >> sh);
            for (y = by; y < y1; y++)
                for (x = bx; x < x1; x++)
                    g_fb[y * FB_W + x] = (uint8_t)v;
        }
    }
}

/* A list of walls into strokes (keep 0: only gone through). Returns where the list ended. */
SMALL static const uint8_t *walls(const uint8_t *wp, int keep)
{
    while (*wp != WEND) {
        int fl = *wp++, np = 0, m, k;
        while (wp[np * 2] != WEND)
            np++;
        for (m = 0; keep && m <= ((fl & W_MIR) ? 1 : 0); m++)
            for (k = 0; k < np - ((fl & (W_CLOSE | W_PEG)) ? 0 : 1) && nseg < NSEG; k++) {
                const uint8_t *pa = wp + k * 2, *pb = fl & W_PEG ? pa : wp + ((k + 1) % np) * 2;
                seg[nseg][0] = (uint8_t)(m ? 92 - pa[0] : pa[0]); seg[nseg][1] = pa[1];
                seg[nseg][2] = (uint8_t)(m ? 92 - pb[0] : pb[0]); seg[nseg][3] = pb[1];
                seg[nseg++][4] = (uint8_t)(fl | ((fl & W_CLOSE) && k == np - 1 ? W_KICK : 0));   /* (the side that closes a slingshot kicks) */
            }
        wp += np * 2 + 1;
    }
    return wp + 1;
}

SMALL static void tilt_draw(const Vis *vis)
{
    f32 dt = vis->dt, tune = vis->k[0], decay = vis->k[1], bounce = vis->k[2], bits = vis->k[3];
    f32 size = 1.25f - 0.5f * tune, rad = 2.9f - 1.1f * tune, glow_k = 1.0f / (0.1f + 0.8f * decay * decay);
    f32 pulse = (1.0f - vis->beat) * (1.0f - vis->beat), tail = 8.0f - 6.6f * decay, c, sn, yp, strobe;
    int algo = vis->algo & 7, i, j, tails = (int)(decay * 3.4f), q = 1 + (int)(maxf(bits - 0.2f, 0.0f) * 5.6f), done = 0;
    const uint8_t *wp;

    if (vis->fresh) {
        for (i = 0; i < (int)sizeof M; i++)
            ((uint8_t *)&M)[i] = 0;
        for (i = 0; i < NBALL; i++)
            ball[i].on = 0;
        for (i = 0; i < NVOICE; i++)
            seen_serial[i] = vis->v[i].serial;
        for (i = 0; i < 4; i++) {
            M.dx[i] = (f32)KIT_AT[algo][i][0];
            M.dy[i] = (f32)KIT_AT[algo][i][1];
        }
        beat_prev = vis->beat;
        M.seed = 0x7117u;
    }

    if (M.kit != algo + 1) {                     /* the table's walls, and this kit's own */
        M.kit = algo + 1;
        nseg = 0;
        walls(WALLS, 1);
        for (wp = KIT_WALLS, i = 0; i <= algo; i++)
            wp = walls(wp, i == algo);
    }

    /* the notes */
    for (i = 0; i < NVOICE; i++)
        if (vis->v[i].serial != seen_serial[i]) {
            seen_serial[i] = vis->v[i].serial;
            {
                f32 heat = M.heat;
                hit(vis->v[i].note % 12, (f32)vis->v[i].vel * (1.0f / 127.0f), vis->rng);
                if (vis->v[i].seq)               /* (a loop going round plays the table, and does not tilt it) */
                    M.heat = heat;
            }
        }

    /* time */
    {
        f32 db = vis->beat - beat_prev;
        if (db < 0.0f) {                         /* the beat: what waits on the plunger hops */
            db += 1.0f;
            for (i = 0; i < NBALL; i++)
                if (ball[i].on && ball[i].x > 88.0f && ball[i].y > 140.0f && ball[i].z <= 0.0f)
                    ball[i].vz = 20.0f + 90.0f * bounce;
        }
        beat_prev = vis->beat;
        M.chase += db;
        if (M.chase >= 64.0f)
            M.chase -= 64.0f;
    }
    {                                            /* what moves on this kit's table */
        const uint8_t *mv = MOVER[algo];
        uint32_t hh = vis->notes * 2654435761u;
        f32 phi = 0.0f, len = (f32)mv[3], d;
        static const int8_t ST[8] = {0, 1, 2, 1, 0, -1, -2, -1};
        M.mt = mv[0];
        M.ax = (f32)mv[1]; M.ay = (f32)mv[2];
        M.mr = 1.2f;
        M.vu += (sat(vis->level * 2.5f) - M.vu) * minf(1.0f, 12.0f * dt);
        switch (M.mt) {
        case MV_STEP: M.ax += len * (f32)ST[(int)M.chase & 7]; break;
        case MV_SLIDE: M.ax += len * sin_turns(M.chase * 0.25f); break;
        case MV_NEEDLE: phi = 0.41f - 0.32f * M.vu; break;
        case MV_JUMP:
            M.ax += (f32)(hh >> 4 & 31u) - 16.0f;
            M.ay += (f32)(hh >> 12 & 15u) - 8.0f;
            phi = (f32)(hh >> 20 & 255u) * (1.0f / 256.0f);
            M.mphi = phi;
            break;
        case MV_PEND: phi = -0.25f + 0.085f * sin_turns(M.chase * 0.5f); break;
        default: phi = M.chase * 0.5f; break;
        }
        if (M.mt <= MV_SLIDE) {                  /* (a block or a puck: round, and not a bar) */
            len = 0.0f;
            M.mr = 3.0f;
            M.mphi = phi;
        }
        d = phi - M.mphi;
        M.mw = (d - (f32)ifloor(d + 0.5f)) * 6.2832f / maxf(dt, 0.002f);
        M.mphi = phi;
        M.bx = M.ax + cos_turns(phi) * len;
        M.by = M.ay + sin_turns(phi) * len;
    }
    for (i = 0; i < NDRUM; i++)
        M.e[i] = maxf(0.0f, M.e[i] - dt * glow_k * (0.3f + M.e[i]));
    for (i = 0; i < 4; i++) {
        spring(&M.dx[i], &M.dvx[i], (f32)KIT_AT[algo][i][0], 8.0f, dt);     /* another kit: the drums change places */
        spring(&M.dy[i], &M.dvy[i], (f32)KIT_AT[algo][i][1], 8.0f, dt);
    }
    for (i = 0; i < NSPR; i++) {
        f32 lim = (f32)SPR[i][2] * 0.1f;
        spring_z(&M.s[i], &M.sv[i], 0.0f, (f32)SPR[i][0], (f32)SPR[i][1] * 0.01f, dt);
        M.s[i] = clampf(M.s[i], -lim, lim);
    }
    M.spin += M.spinw * dt;
    M.spinw -= M.spinw * minf(1.0f, (M.spink / (0.4f + 1.6f * decay) + 0.4f) * dt);
    M.spin = fractf(M.spin + 0.5f) - 0.5f;
    if (absf(M.spinw) < 1.5f)                    /* nearly still: it hangs flat again */
        M.spin -= M.spin * minf(1.0f, 7.0f * dt);
    M.bump = maxf(0.0f, M.bump - dt * 5.0f);
    M.flash = maxf(0.0f, M.flash - dt * glow_k * 2.0f);
    M.heat = maxf(0.0f, M.heat - M.heat * 0.9f * dt);
    if (M.tilt > 0.0f)
        M.tilt -= dt;
    else if (M.heat > 24.0f && EGGS_ON) {        /* hammered: TILT */
        M.tilt = 1.5f;
        M.heat = 0.0f;
        eng_cue(CUE_TILT);
        M.sv[LEAN] = rng_next(vis->rng) & 1u ? 150.0f : -150.0f;
        for (i = 0; i < NBALL; i++)
            ball[i].vx += M.sv[LEAN];
    }
    M.feed -= dt;
    if (M.feed < 0.0f) {                         /* a ball for the plunger, when the lane is empty */
        Ball *b;
        M.feed = 0.4f;
        for (i = 0, j = 0; i < NBALL; i++)
            j |= ball[i].on && ball[i].x > 88.0f;
        if (!j) {
            b = ball_new();
            b->x = 92.5f;
            b->y = 146.0f;
        }
    }
    physics(dt, rad, size, 0.35f + 0.5f * bounce, 0.12f + 0.5f * bounce);

    strobe = M.tilt > 0.0f && fractf(M.tilt * 6.0f) > 0.5f ? 1.0f : 0.0f;
    backbox(vis, tail);

    /* the floor: what is painted on it and the lights set in it */
    floor_art(vis, algo, size);
    for (i = 0; i < 4; i++) {                    /* the rings that run out from a drum that was hit */
        f32 e = M.e[DT[i]];
        if (e > 0.03f)
            tell(M.dx[i], M.dy[i], 0.0f, (f32)DRUM_AT[i][2] * 0.5f * size + 2.0f + (1.0f - e) * (7.0f + 9.0f * decay), 1.4f, e * 0.6f);
    }
    for (i = 0; i < NDRUM; i++) {                /* twelve lights in a bow, a note each */
        f32 a = 0.065f + (f32)i * (0.37f / 11.0f);
        lamp(46.0f - 22.0f * cos_turns(a), 84.0f + 19.0f * sin_turns(a), 1.5f, maxf(M.e[i], strobe));
    }
    for (i = 0; i < 4; i++)                      /* a light over each lane at the top: the beats of the bar */
        lamp(28.0f + 12.0f * (f32)i, 15.0f - (i == 1 || i == 2 ? 2.0f : 0.0f), 2.0f,
             maxf(maxf(M.flash, strobe), (((int)M.chase) & 3) == i ? pulse : 0.0f));
    lamp(46.0f, 133.0f, 3.4f, maxf(M.e[KICK], strobe));      /* the kick's, between the flippers */
    for (i = 0; i < 10; i++) {                   /* arrows up the middle, a light running up them in time */
        f32 x = i < 5 ? 37.0f : 55.0f, y = 124.0f - (f32)(i % 5) * 4.5f;
        f32 ph = fractf(M.chase - (f32)(i % 5) * 0.2f), e = maxf(maxf(M.flash, strobe), sat(1.0f - ph * tail * 0.6f));
        fline(x - 1.6f, y + 1.2f, x, y - 1.2f, 1.3f, 0.25f + 0.75f * e);
        fline(x + 1.6f, y + 1.2f, x, y - 1.2f, 1.3f, 0.25f + 0.75f * e);
    }

    /* the walls */
    for (j = 0; j < nseg; j++) {
        const uint8_t *g = seg[j];
        f32 z = g[4] & W_LOW ? 2.0f : 4.0f;
        if (g[4] & W_KICK)
            continue;
        if (g[4] & W_PEG) {
            tline((f32)g[0], (f32)g[1], 0.0f, (f32)g[0], (f32)g[1], 6.0f, 1.6f, 0.8f);
            tell((f32)g[0], (f32)g[1], 6.0f, 1.3f, 0.0f, 0.4f + 0.6f * maxf(M.e[CLAP], M.e[SNARE]));
            continue;
        }
        fline((f32)g[0], (f32)g[1], (f32)g[2], (f32)g[3], 1.0f, 0.3f);
        tline((f32)g[0], (f32)g[1], z, (f32)g[2], (f32)g[3], z, 1.8f, 0.8f);
        if (g[4] & W_LOW)                        /* a post where a low rail begins */
            tell((f32)g[0], (f32)g[1], 2.0f, 1.2f, 0.0f, 1.0f);
        if (g[4] & W_RAIL)                       /* the cabinet's own side: the outer edge of its wood */
            tline(50.5f + ((f32)g[0] - 50.5f) * 1.07f, 42.0f + ((f32)g[1] - 42.0f) * (g[1] < 42 ? 1.09f : 1.0f), z,
                  50.5f + ((f32)g[2] - 50.5f) * 1.07f, 42.0f + ((f32)g[3] - 42.0f) * (g[3] < 42 ? 1.09f : 1.0f), z, 1.6f, 0.55f);
    }
    /* what moves */
    if (M.mt == MV_STEP)
        box(M.ax, M.ay, 2.6f, 3.0f, pulse, 0.3f, 0);
    else if (M.mt == MV_SLIDE) {
        fline(30.0f, M.ay, 62.0f, M.ay, 1.0f, 0.3f);
        tell(M.ax, M.ay, 2.0f, 3.0f, 0.0f, 0.3f + 0.7f * pulse);
        tell(M.ax, M.ay, 2.0f, 3.0f, 1.3f, 1.0f);
    } else if (M.mt) {
        tline(M.ax, M.ay, 2.0f, M.bx, M.by, 2.0f, M.mt == MV_NEEDLE ? 1.6f : 2.6f, 1.0f);
        if (M.mt == MV_ROTOR)
            tline(M.ax, M.ay, 2.0f, 2.0f * M.ax - M.bx, 2.0f * M.ay - M.by, 2.0f, 2.6f, 1.0f);
        if (M.mt == MV_PEND)
            tell(M.ax + (M.bx - M.ax) * 0.55f, M.ay + (M.by - M.ay) * 0.55f, 2.0f, 2.2f, 0.0f, 1.0f);
        tell(M.ax, M.ay, 2.0f, 1.5f, 0.0f, 1.0f);
    }
    /* the slingshots' bands and lights */
    for (i = 0; i < 2; i++) {
        f32 mx = 20.5f + 2.6f * M.s[SL + i], my = 119.0f - 1.1f * M.s[SL + i], e = maxf(M.e[CLAP], strobe);
        if (i) mx = 92.0f - mx;
        tline(i ? 76.0f : 16.0f, 108.0f, 2.0f, mx, my, 2.0f, 2.2f, 1.0f);
        tline(i ? 67.0f : 25.0f, 130.0f, 2.0f, mx, my, 2.0f, 2.2f, 1.0f);
        lamp(i ? 73.0f : 19.0f, 120.0f, 1.8f, e);
    }
    /* the flippers */
    c = cos_turns(0.08f - 0.167f * M.fa) * FLIP_L;
    sn = sin_turns(0.08f - 0.167f * M.fa) * FLIP_L;
    for (i = 0; i < 2; i++) {
        f32 ax, ay, as;
        pj(i ? 64.0f : 28.0f, 138.0f, 1.5f);
        ax = PX; ay = PY; as = PS;
        pj(i ? 64.0f - c : 28.0f + c, 138.0f + sn, 1.5f);
        g_beam(ax, ay, PX, PY, 5.2f * as, 2.6f * as, 1.0f, 1.0f, 0.0f);
        g_mode(G_PAINT);
        g_disc(ax, ay, 1.1f * as, 0.0f);
        g_mode(G_MAX);
    }
    /* the plunger */
    yp = 151.0f + 5.0f * M.s[PLUNG];
    tline(88.5f, yp, 1.0f, 96.5f, yp, 1.0f, 2.4f, 0.9f);
    tline(92.5f, yp, 1.0f, 92.5f, 166.0f, 1.0f, 1.6f, 0.6f);
    /* the apron */
    fline(4.0f, 149.0f, 38.0f, 160.0f, 1.4f, 0.5f);
    fline(88.0f, 149.0f, 54.0f, 160.0f, 1.4f, 0.5f);

    /* what stands on the table, and the balls among it, from the far end to the near */
    for (i = 0; i < NBALL; i++)
        ball[i].drawn &= 2;
    for (;;) {
        f32 y = 1e9f, X, Y, s, e;
        int k = -1;
        G_POLL();
        for (j = 0; j < 8; j++) {
            f32 oy = j < 4 ? M.dy[j] : (j == 4 ? CYM_Y : (j == 5 ? 26.0f : (j == 6 ? BELL_Y : 82.0f)));
            if (!(done & (1 << j)) && oy < y) {
                y = oy;
                k = j;
            }
        }
        for (i = 0; i < NBALL; i++)
            if (ball[i].on && !(ball[i].drawn & 1) && ball[i].y < y)
                draw_ball(&ball[i], rad, tails, algo, vis->t);
        if (k < 0)
            break;
        done |= 1 << k;
        switch (k) {
        case 0: case 1: case 2: case 3: {
            e = maxf(M.e[DT[k]], strobe);
            draw_drum(k, algo, size, e, k ? 0.0f : M.e[RIM], pulse * bounce, vis->t);
            pj(M.dx[k], M.dy[k], (f32)(DRUM_AT[k][3] * FORM[algo][1]) * (1.0f / 16.0f));
            rays(PX, PY, (f32)DRUM_AT[k][2] * 0.5f * size * PS, (f32)DRUM_AT[k][2] * 0.5f * size * PS * PS * SQ + 3.0f, maxf(M.e[DT[k]], k ? 0.0f : M.e[RIM]), 10);
            break;
        }
        case 4: {                                /* the crash: a cymbal on its stand */
            f32 rx, ry;
            e = maxf(M.e[CRASH], strobe);
            tline(CYM_X, CYM_Y, 0.0f, CYM_X, CYM_Y, 11.0f, 1.6f, 0.6f);
            pj(CYM_X + 1.5f * M.s[CYM], CYM_Y, 11.0f);
            rx = 10.5f * PS; ry = rx * (0.2f + 0.2f * absf(M.s[CYM]));
            plate(PX, PY, rx, ry, e * 0.85f, 1.4f, 0.9f);
            oval(PX, PY, rx * 0.55f, ry * 0.55f, 1.0f, 0.4f);
            oval(PX, PY - 1.0f, rx * 0.18f, ry * 0.3f + 0.6f, 0.0f, 0.9f);
            rays(PX, PY, rx, ry + 4.0f, M.e[CRASH], 14);
            break;
        }
        case 5: {                                /* the shaker: beads in a cup */
            static const int8_t BD[4][2] = {{-25, -8}, {22, -12}, {-6, 14}, {12, 6}};
            pj(75.0f, 26.0f, 0.0f);
            X = PX; Y = PY; s = PS;
            e = maxf(M.e[SHAKE], strobe);
            shell(X, Y, Y - 5.0f * s, 5.5f * s, 5.5f * s * s * SQ, 0.0f, 0.6f);
            oval(X, Y - 5.0f * s, 5.5f * s, 5.5f * s * s * SQ, 1.2f, 0.8f);
            for (j = 0; j < 4; j++) {
                pj(75.0f + (f32)BD[j][0] * 0.1f, 26.0f + (f32)BD[j][1] * 0.1f, 2.0f + absf(M.s[BEAD + j]));
                g_disc(PX, PY, 1.9f, 0.5f + 0.5f * e);
            }
            break;
        }
        case 6: {                                /* the cowbell, hung from its top */
            f32 xy[8], ph = M.s[BELL];
            pj(BELL_X, BELL_Y, 0.0f);
            s = PS;
            X = PX; Y = PY - 10.0f * s;
            e = maxf(M.e[COW], strobe);
            g_line(X, Y, X, Y - 2.0f * s, 1.4f, 0.6f);
            g_line(X - 3.0f * s, Y - 2.0f * s, X + 3.0f * s, Y - 2.0f * s, 1.4f, 0.6f);
            xy[0] = X - 2.3f * s; xy[1] = Y;
            xy[2] = X + 2.3f * s; xy[3] = Y;
            xy[4] = X + 4.0f * s - 8.5f * s * ph; xy[5] = Y + 8.5f * s + 4.0f * s * ph;
            xy[6] = X - 4.0f * s - 8.5f * s * ph; xy[7] = Y + 8.5f * s - 4.0f * s * ph;
            g_mode(G_PAINT);
            g_fill(xy, 4, 0.0f);
            g_mode(G_MAX);
            if (e > 0.02f)
                g_fill(xy, 4, e);
            g_poly(xy, 4, 1, 1.5f, 0.9f);
            rays(X - 4.0f * s * ph, Y + 5.0f * s, 5.0f * s, 5.0f * s, M.e[COW], 8);
            break;
        }
        default: {                               /* the hat: a plate that spins on a wire between two posts */
            f32 cs = cos_turns(M.spin), rx, ry;
            e = maxf(maxf(M.e[HAT], M.e[OHAT]), strobe);
            tline(11.0f, 82.0f, 0.0f, 11.0f, 82.0f, 10.0f, 1.6f, 0.7f);
            tline(23.0f, 82.0f, 0.0f, 23.0f, 82.0f, 10.0f, 1.6f, 0.7f);
            tline(11.0f, 82.0f, 6.5f, 23.0f, 82.0f, 6.5f, 1.0f, 0.5f);
            pj(17.0f, 82.0f, 6.5f);
            rx = 4.6f * PS; ry = maxf(4.2f * PS * absf(cs), 0.8f);
            plate(PX, PY, rx, ry, cs > 0.0f ? 0.35f + 0.65f * e : 0.0f, 1.4f, 0.9f);
            break;
        }
        }
    }

    if (q > 1)
        mosaic(iclamp(q, 2, 5));
}

const InstDef inst_tilt = {
    "TILT",
    {255, 150, 48},
    {"TUNE", "DECAY", "BOUNCE", "BITS"},
    {"8BIT", "ANALOG", "HOUSE", "FM", "ZAP", "GLITCH", "CLICK", "RAVE"},
    {{50, 50, 30, 15}, 0, {0, 50, 100, 75}, {0, 55, 0, 0}, {0, 3, 10, 50}, {0, 3, 1, 60}, {0, 0, 0}},
    0.55f,
    tilt_block, tilt_note, tilt_render, 0, tilt_draw,
};
