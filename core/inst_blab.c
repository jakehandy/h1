/* h1 - BLAB: it talks. The keys sing words. KNOB 1 picks a phrase, and every key you press sings the
 * next syllable of it at that key's pitch: the consonants in front of the vowel go off as the key goes
 * down, the vowel holds for as long as it is held, and the consonants after it go when the keys are let
 * go. Press a chord and the whole chord sings the syllable, as a vocoder does: every voice is a carrier,
 * and one mouth shapes them all.
 *
 * The mouth is a formant synthesiser: a glottal buzz (the voices) and a breath (noise) through four
 * resonators in a row, and a hiss of its own for S, SH, F and the bursts of P, T, K. ROBOT puts a
 * sixteen-band vocoder on top: the mouth is the voice it listens to, the chord is its carrier. The picture
 * is a voiceprint: the formants as they are sung, written across the screen, with the words above.
 *
 * Tuned by ear's stand-in: every phrase rendered at two pitches and put through a speech recogniser
 * (Whisper small.en). The numbers below won that; BLAB_PRE and the rest can be set from the compiler line.
 *
 *   K1 WORDS   which phrase it sings                    the words change
 *   K2 MOUTH   a giant's mouth to a chipmunk's          the voiceprint moves down and up
 *   K3 DRAWL   snappy to lazy consonants and glides     the ribbons slur
 *   K4 AIR     breath in the voice                      the print fills with grain
 */
#include "internal.h"
#include "dsp.h"
#ifndef BLAB_PRE
#define BLAB_PRE 0.93f           /* the lips: how much of the +6 dB an octave (0 none, 1 all) */
#endif
#ifndef BLAB_SC
#define BLAB_SC 1.1f             /* every formant, against the table's (a man's): a little higher sings clearer */
#endif
#ifndef BLAB_RMIX
#define BLAB_RMIX 0.6f           /* ROBOT: the mouth under the vocoder, so the words stay words */
#endif
#ifndef BLAB_CHZ
#define BLAB_CHZ 10000.0f        /* CHIP: its sample rate */
#endif
#ifndef BLAB_CQ
#define BLAB_CQ 128.0f           /* and its steps */
#endif

/* ---- phones ----------------------------------------------------------------------------- */
enum { K_VOW, K_SEMI, K_NAS, K_FRIC, K_CLOS, K_BURST, K_ASP };

typedef struct Ph {
    uint8_t f1, f2, f3;          /* formants: F1 in 10 Hz, F2 and F3 in 20 Hz */
    uint8_t b1, b2, b3;          /* their bandwidths in 10 Hz */
    uint8_t av, ah, af;          /* voicing, breath through the formants, hiss: 0..16 */
    uint8_t fc, fq;              /* the hiss: centre in 100 Hz, Q in quarters */
    uint8_t dur;                 /* how long, in 4 ms (0: it holds, the vowel) */
    uint8_t kind;
} Ph;

enum {
    IY, IH, EH, AE, AA, AO, UH, UW, AH, ER, AX, OH, EYs, IYg, UWg,
    W_, Y_, R_, L_, M_, N_, NG, F_, V_, TH, DH, S_, Z_, SH, ZH, HH,
    CL, VCL, PB, TB, KB, AS, NPH
};

static const Ph PH[NPH] = {
    /*        f1   f2   f3   b1  b2  b3  av ah af  fc  fq dur kind */
    [IY]  = { 31, 101, 148,  5, 10, 15, 16, 0, 0,  0,  0, 0, K_VOW},
    [IH]  = { 40,  90, 128,  5, 10, 14, 16, 0, 0,  0,  0, 0, K_VOW},
    [EH]  = { 53,  84, 125,  6,  9, 20, 16, 0, 0,  0,  0, 0, K_VOW},
    [AE]  = { 64,  83, 122,  7, 15, 32, 16, 0, 0,  0,  0, 0, K_VOW},
    [AA]  = { 72,  56, 128, 10,  7, 16, 16, 0, 0,  0,  0, 0, K_VOW},
    [AO]  = { 60,  45, 128,  9, 10,  8, 16, 0, 0,  0,  0, 0, K_VOW},
    [UH]  = { 45,  55, 118,  8, 10,  8, 16, 0, 0,  0,  0, 0, K_VOW},
    [UW]  = { 31,  47, 112,  6, 10, 14, 16, 0, 0,  0,  0, 0, K_VOW},
    [AH]  = { 62,  61, 128,  8,  5, 14, 16, 0, 0,  0,  0, 0, K_VOW},
    [ER]  = { 47,  64,  77, 10,  6, 11, 16, 0, 0,  0,  0, 0, K_VOW},
    [AX]  = { 55,  63, 124,  8,  5, 14, 16, 0, 0,  0,  0, 0, K_VOW},
    [OH]  = { 54,  50, 115,  8,  7,  7, 16, 0, 0,  0,  0, 0, K_VOW},
    [EYs] = { 48,  86, 126,  6,  9, 20, 16, 0, 0,  0,  0, 0, K_VOW},
    [IYg] = { 33,  96, 140,  5, 15, 30, 15, 0, 0,  0,  0, 22, K_SEMI},   /* the glide at the end of AY, EY, OY */
    [UWg] = { 34,  45, 115,  6, 10, 14, 15, 0, 0,  0,  0, 22, K_SEMI},   /* and of OW, AW */
    [W_]  = { 29,  31, 108,  5,  8,  6, 13, 0, 0,  0,  0, 14, K_SEMI},
    [Y_]  = { 26, 104, 151,  4, 25, 50, 13, 0, 0,  0,  0, 12, K_SEMI},
    [R_]  = { 31,  53,  69,  7, 10, 12, 13, 0, 0,  0,  0, 16, K_SEMI},
    [L_]  = { 31,  53, 144,  5, 10, 28, 12, 0, 0,  0,  0, 15, K_SEMI},
    [M_]  = { 25,  64, 107, 10, 20, 20,  9, 0, 0,  0,  0, 18, K_NAS},
    [N_]  = { 25,  67, 124, 10, 30, 30,  9, 0, 0,  0,  0, 17, K_NAS},
    [NG]  = { 25, 100, 145, 10, 30, 30,  9, 0, 0,  0,  0, 18, K_NAS},
    [F_]  = { 34,  55, 104,  2, 10, 15,  0, 2, 8, 50,  2, 25, K_FRIC},
    [V_]  = { 22,  55, 104,  6, 10, 15, 10, 0, 5, 50,  2, 16, K_FRIC},
    [TH]  = { 32,  65, 127,  2, 10, 15,  0, 2, 7, 60,  2, 25, K_FRIC},
    [DH]  = { 27,  65, 127,  6, 10, 15, 10, 0, 4, 60,  2, 12, K_FRIC},
    [S_]  = { 32,  70, 127,  2, 10, 15,  0, 0, 16, 72, 14, 30, K_FRIC},
    [Z_]  = { 24,  70, 127,  6, 10, 15, 10, 0, 11, 72, 14, 20, K_FRIC},
    [SH]  = { 30,  92, 137,  2, 10, 15,  0, 0, 16, 30, 6, 30, K_FRIC},
    [ZH]  = { 30,  92, 137,  6, 10, 15, 10, 0, 11, 30, 6, 20, K_FRIC},
    [HH]  = {  0,   0,   0, 30, 30, 30,  0, 12, 1, 30, 1, 14, K_ASP},     /* the formants of what comes next */
    [CL]  = {  0,   0,   0,  0,  0,  0,  0, 0, 0,  0,  0, 12, K_CLOS},   /* a closure: the mouth shut */
    [VCL] = { 20,   0,   0, 10,  0,  0,  4, 0, 0,  0,  0, 10, K_CLOS},   /* shut, the voice going on behind it */
    [PB]  = { 40,  55, 108,  6, 10, 15,  0, 0, 9, 12,  1,  3, K_BURST},
    [TB]  = { 40,  85, 130,  6, 10, 15,  0, 0, 14, 45, 3,  3, K_BURST},
    [KB]  = { 30,  95, 142,  6, 10, 15,  0, 0, 12, 22, 4,  4, K_BURST},
    [AS]  = {  0,   0,   0, 20, 20, 30,  0, 11, 0,  0,  0, 9, K_ASP},     /* the breath after P, T, K */
};

/* ---- phrases ---------------------------------------------------------------------------- *
 * Each is what the screen shows, its syllables split by '-' and words by ' ', and how it is said:
 * ARPAbet-like phones split the same way, by '-' and '/'. The parser turns them into phones once. */
typedef struct Phrase { const char *show, *say; } Phrase;
static const Phrase PHRASE[] = {
    {"HEL-LO", "HH EH - L OW"},
    {"HAN-DY ONE", "HH AE N - D IY / W AH N"},
    {"I AM A RO-BOT", "AY / AE M / AX / R OW - B AA T"},
    {"DO RE MI FA SO LA TI", "D OW / R EY / M IY / F AA / S OW / L AA / T IY"},
    {"ONE TWO THREE FOUR", "W AH N / T UW / TH R IY / F AO R"},
    {"TURN IT UP", "T ER N / IH T / AH P"},
    {"MORE BASS", "M AO R / B EY S"},
    {"OH YEAH", "OW / Y EH"},
    {"BEEP BOOP", "B IY P / B UW P"},
    {"PLAY IT A-GAIN", "P L EY / IH T / AX - G EH N"},
    {"I LOVE YOU", "AY / L AH V / Y UW"},
    {"GOOD NIGHT", "G UH D / N AY T"},
    {"LA LA LA LA", "L AA / L AA / L AA / L AA"},
    {"SPACE IS DEEP", "S P EY S / IH Z / D IY P"},
    {"WE COME IN PEACE", "W IY / K AH M / IH N / P IY S"},
    {"COM-PU-TER", "K AX M - P Y UW - T ER"},
    {"SYN-THE-SI-ZER", "S IH N - TH AX - S AY - Z ER"},
    {"YES NO MAY-BE", "Y EH S / N OW / M EY - B IY"},
    {"ZE-RO ONE ZE-RO ONE", "Z IY - R OW / W AH N / Z IY - R OW / W AH N"},
    {"BYE BYE", "B AY / B AY"},
};
#define NPHRASE ((int)(sizeof PHRASE / sizeof PHRASE[0]))
#define NSYL 12                  /* syllables a phrase, at most */
#define NSP 10                   /* phones a syllable, at most */

typedef struct Syl {
    uint8_t n, nuc;              /* phones, and which is the vowel */
    uint8_t ph[NSP];
    uint8_t word;                /* 1: the first syllable of a word */
} Syl;

static Syl syl[NPHRASE][NSYL];
static uint8_t nsyl[NPHRASE];

/* a token to phones: a vowel, a diphthong (vowel + glide), or a consonant (stops as closure, burst, breath) */
static int tok(const char *s, int n, uint8_t *out, int *is_vowel)
{
    static const struct { char t[3]; uint8_t p[3]; uint8_t v; } T[] = {
        {"IY", {IY}, 1}, {"IH", {IH}, 1}, {"EH", {EH}, 1}, {"AE", {AE}, 1}, {"AA", {AA}, 1}, {"AO", {AO}, 1},
        {"UH", {UH}, 1}, {"UW", {UW}, 1}, {"AH", {AH}, 1}, {"ER", {ER}, 1}, {"AX", {AX}, 1},
        {"EY", {EYs, IYg}, 2}, {"AY", {AA, IYg}, 2}, {"OY", {AO, IYg}, 2}, {"OW", {OH, UWg}, 2}, {"AW", {AA, UWg}, 2},
        {"W", {W_}, 0}, {"Y", {Y_}, 0}, {"R", {R_}, 0}, {"L", {L_}, 0}, {"M", {M_}, 0}, {"N", {N_}, 0},
        {"NG", {NG}, 0}, {"F", {F_}, 0}, {"V", {V_}, 0}, {"TH", {TH}, 0}, {"DH", {DH}, 0}, {"S", {S_}, 0},
        {"Z", {Z_}, 0}, {"SH", {SH}, 0}, {"ZH", {ZH}, 0}, {"HH", {HH}, 0},
        {"P", {CL, PB, AS}, 0}, {"T", {CL, TB, AS}, 0}, {"K", {CL, KB, AS}, 0},
        {"B", {VCL, PB}, 0}, {"D", {VCL, TB}, 0}, {"G", {VCL, KB}, 0},
        {"CH", {CL, TB, SH}, 0}, {"JH", {VCL, TB, ZH}, 0},
    };
    int i, j;
    for (i = 0; i < (int)(sizeof T / sizeof T[0]); i++) {
        if ((int)(T[i].t[1] ? 2 : 1) != n || T[i].t[0] != s[0] || (n == 2 && T[i].t[1] != s[1]))
            continue;
        *is_vowel = T[i].v;
        for (j = 0; j < 3 && (j == 0 || T[i].p[j]); j++)
            out[j] = T[i].p[j];
        return j;
    }
    *is_vowel = 0;
    return 0;
}

static APART void parse(void)
{
    int p, i;
    for (p = 0; p < NPHRASE; p++) {
        const char *s = PHRASE[p].say;
        Syl *y = &syl[p][0];
        int ns = 0, word = 1;
        y->n = 0; y->nuc = 255; y->word = 1;
        while (*s) {
            if (*s == ' ') { s++; continue; }
            if (*s == '-' || *s == '/') {
                word = *s == '/';
                if (ns < NSYL - 1) {
                    y = &syl[p][++ns];
                    y->n = 0; y->nuc = 255; y->word = (uint8_t)word;
                }
                s++;
                continue;
            }
            for (i = 0; s[i] && s[i] != ' ' && s[i] != '-' && s[i] != '/'; i++)
                ;
            {
                uint8_t ph[3];
                int v, k = tok(s, i, ph, &v), j;
                for (j = 0; j < k && y->n < NSP; j++) {
                    if (v && j == 0 && y->nuc == 255)
                        y->nuc = y->n;
                    y->ph[y->n++] = ph[j];
                }
            }
            s += i;
        }
        nsyl[p] = (uint8_t)(ns + 1);
        for (i = 0; i <= ns; i++)
            if (syl[p][i].nuc == 255)
                syl[p][i].nuc = 0;
    }
}

/* ---- the mouth -------------------------------------------------------------------------- */
#define NQ 24                    /* phones queued */
#define NB 16                    /* ROBOT's bands */
typedef struct Res { f32 a, b, c, y1, y2; } Res;

static struct {
    int algo, me;
    int phrase, at;              /* the phrase, and the syllable the next key sings */
    int shown;                   /* the syllable sounding (for the picture), -1: none yet */
    uint8_t q[NQ];               /* phones to say */
    uint16_t qd[NQ];             /* each one's control blocks, 0: held while a key is */
    int qn, qi;                  /* how many are queued, and the one sounding */
    int qleft;                   /* control blocks the one sounding has left */
    int coda_from, coda_n;       /* the syllable sounding: where its ending is, and how long */
    int coda_done;
    int gate;                    /* a key of ours is down */
    uint32_t since_on;           /* control blocks since the last syllable began */
    uint32_t quiet;              /* control blocks with no key down */
    int fresh_note;
    /* where the mouth is, eased, and where it is going */
    f32 f[4], bw[4], av, ah, af, fc, fq;
    f32 tf[4], tbw[4], tav, tah, taf, tfc, tfq;
    f32 scale, drawl, air;
    Res r[5];
    f32 rad;                     /* the lips: the last sample, for the +6 dB an octave of the mouth's opening */
    Svf fr; f32 frs[2];
    Svf band[NB]; f32 bs[NB][2], bsn[NB][2], bsa[NB][2], bg[NB], bgn[NB], env[NB], eg[NB], mprev, yprev;
    Svf band_a[NB];
    f32 band_sr;
    int half;
    Svf chip_lp; f32 chip_s[2];
    f32 ha[NB];                  /* the vocoder's band levels, for the picture */
    uint32_t rng;
    f32 lp, crush_ph, crush_hold, lev;
    f32 vib;
} M;

static const f32 BANDF[NB] = {180, 237, 312, 411, 541, 712, 937, 1233, 1623, 2136, 2812, 3700, 4870, 6000, 7000, 8000};
static const f32 BANDI[NB] = {5.6f, 4.2f, 3.2f, 2.4f, 1.85f, 1.4f, 1.07f, 0.81f, 0.62f, 0.47f, 0.36f, 0.27f, 0.21f, 0.17f, 0.14f, 0.125f};
static const f32 BANDW[NB] = {0.5f, 0.6f, 0.7f, 0.8f, 0.9f, 1.0f, 1.1f, 1.2f, 1.4f, 1.6f, 1.8f, 2.0f, 2.2f, 2.4f, 2.4f, 2.4f};   /* a saw's top made up for */

static f32 lg2(f32 x)                            /* log2, near enough for an ear or an eye */
{
    union { f32 f; int32_t i; } u;
    f32 m;
    u.f = maxf(x, 1e-6f);
    m = (f32)((u.i >> 23) & 255) - 127.0f;
    u.i = (u.i & 0x007FFFFF) | 0x3F800000;
    return m + (u.f - 1.0f) * (1.3466f - 0.3466f * (u.f - 1.0f));
}

static void res_set(Res *r, f32 f, f32 bw, f32 isr)
{
    f32 e = exp2fx(-1.442695f * H1_PI * bw * isr);
    r->c = -e * e;
    r->b = 2.0f * e * cos_turns(f * isr);
    r->a = 1.0f - r->b - r->c;
}

static void queue(int ph, int blocks)
{
    if (M.qn < NQ) {
        M.q[M.qn] = (uint8_t)ph;
        M.qd[M.qn] = (uint16_t)blocks;
        M.qn++;
    }
}

static int ph_blocks(int ph, f32 sr)
{
    f32 ms = (f32)PH[ph].dur * 4.0f * (0.6f + 1.2f * M.drawl);
    return imax(1, (int)(ms * 0.001f * sr / (f32)CTRL));
}

/* take a phone up: where the mouth goes for it */
static void target(int ph)
{
    const Ph *p = &PH[ph];
    int i;
    if (p->kind == K_ASP || p->kind == K_CLOS) {      /* breath and closures take the formants of what comes next */
        for (i = M.qi + 1; i < M.qn; i++)
            if (PH[M.q[i]].kind != K_ASP && PH[M.q[i]].kind != K_CLOS && PH[M.q[i]].kind != K_BURST)
                break;
        if (i < M.qn) {
            const Ph *n = &PH[M.q[i]];
            M.tf[0] = (f32)n->f1 * 10.0f;
            M.tf[1] = (f32)n->f2 * 20.0f;
            M.tf[2] = (f32)n->f3 * 20.0f;
        }
        if (p->kind == K_CLOS && p->f1)
            M.tf[0] = (f32)p->f1 * 10.0f;
    } else {
        M.tf[0] = (f32)p->f1 * 10.0f;
        M.tf[1] = (f32)p->f2 * 20.0f;
        M.tf[2] = (f32)p->f3 * 20.0f;
    }
    if (p->b1) {
        M.tbw[0] = (f32)p->b1 * 10.0f;
        M.tbw[1] = (f32)p->b2 * 10.0f;
        M.tbw[2] = (f32)p->b3 * 10.0f;
    }
    M.tav = (f32)p->av * (1.0f / 16.0f);
    M.tah = (f32)p->ah * (1.0f / 16.0f);
    M.taf = (f32)p->af * (1.0f / 16.0f);
    if (p->fc) {
        M.tfc = (f32)p->fc * 100.0f;
        M.tfq = (f32)p->fq * 0.25f;
    }
    if (p->kind == K_BURST) {                    /* a burst is sudden: no easing in */
        M.af = M.taf;
        M.fc = M.tfc;
        M.fq = M.tfq;
    }
    if (p->kind == K_CLOS)
        M.av = minf(M.av, 0.3f);
}

static void start_phone(int i, f32 sr)
{
    M.qi = i;
    if (i >= M.qn) {                             /* nothing more to say: the mouth closes */
        M.tav = 0.0f; M.tah = 0.0f; M.taf = 0.0f;
        M.qleft = 0;
        return;
    }
    target(M.q[i]);
    M.qleft = M.qd[i] ? M.qd[i] : -1;
    (void)sr;
}

/* a key went down: the next syllable starts */
static void syllable(f32 sr)
{
    const Syl *y = &syl[M.phrase][M.at % nsyl[M.phrase]];
    int i, k;
    /* what is left of the last syllable is cut short: its ending, quickly, then this one */
    k = 0;
    M.qn = 0;
    for (i = 0; i < y->n; i++) {
        int ph = y->ph[i], d;
        if (i < y->nuc) {
            d = ph_blocks(ph, sr);
            if (k == 0 && PH[ph].kind == K_CLOS)
                d = imax(1, d / 4);              /* a word that starts shut: barely, so the note is not late */
            else
                d = imax(1, d * 3 / 4);
            queue(ph, d);
            k++;
        } else if (i == y->nuc)
            queue(ph, 0);
        else
            queue(ph, ph_blocks(ph, sr));
    }
    M.coda_from = y->nuc + 1;
    M.coda_n = y->n - y->nuc - 1;
    M.coda_done = 0;
    M.shown = M.at % nsyl[M.phrase];
    M.at = (M.at + 1) % nsyl[M.phrase];
    M.since_on = 0;
    start_phone(0, sr);
    if (PH[M.q[0]].kind == K_VOW || PH[M.q[0]].kind == K_SEMI || PH[M.q[0]].kind == K_NAS)
        for (i = 0; i < 3; i++)                  /* a syllable that starts open starts there, not on a glide */
            M.f[i] = M.tf[i];
}

/* ---- the voices: the carriers ----------------------------------------------------------- */
static void blab_block(const Blk *b)
{
    int i;
    if (!nsyl[0]) {                              /* the first time: the phrases into phones */
        parse();
        M.shown = -1;
        M.tbw[0] = 60.0f; M.tbw[1] = 90.0f; M.tbw[2] = 150.0f;
    }
    M.algo = b->algo;
    i = (int)(b->k[0] * (f32)NPHRASE * 0.999f);
    if (i != M.phrase) {                         /* a new phrase starts at its beginning */
        M.phrase = iclamp(i, 0, NPHRASE - 1);
        M.at = 0;
    }
    M.scale = BLAB_SC * exp2fx((b->k[1] - 0.5f) * 0.9f);
    M.drawl = b->k[2];
    M.air = b->k[3];
    M.vib += b->step * (M.algo == 6 ? 5.6f : 4.8f);
    if (M.vib > 1.0f) M.vib -= 1.0f;
}

static void blab_note(Voice *v, const Blk *b)
{
    (void)b;
    if (!v->u[0]) v->u[0] = 0x2B1AB00Du + v->idx * 7919u;
    if (v->env < 0.001f) {
        int i;
        for (i = 0; i < 6; i++)
            v->ph[i] = rng_f(&v->u[0]);
    }
    M.fresh_note = 1;
}

static void blab_render(Voice *v, const Blk *b, f32 *out, int n)
{
    f32 vib = 0.0f, f, d, d2, d3, x;
    int i, a = M.algo;
    if (a == 1 || a == 6)                        /* CHOIR and DIVA sing with vibrato, after a moment */
        vib = tsin(M.vib) * sat(v->age * 1.5f - 0.3f) * (a == 6 ? 0.32f : 0.12f);
    f = mtof(v->pitch + b->bend + vib + (a == 5 ? -12.0f : 0.0f)) * b->isr;
    d = minf(f, 0.45f);
    d2 = minf(f * 1.0035f, 0.45f);
    d3 = minf(f * 0.9965f, 0.45f);
    for (i = 0; i < n; i++) {
        switch (a) {
        case 1:                                  /* CHOIR: three saws apart */
            x = 0.5f * (saw_bl(v->ph[0], d) + saw_bl(v->ph[1], d2) + saw_bl(v->ph[2], d3));
            v->ph[1] += d2; if (v->ph[1] >= 1.0f) v->ph[1] -= 1.0f;
            v->ph[2] += d3; if (v->ph[2] >= 1.0f) v->ph[2] -= 1.0f;
            break;
        case 2:                                  /* TALK: a hollow square, as a tube in the mouth */
            x = pulse_bl(v->ph[0], d, 0.32f);
            break;
        case 3:                                  /* WHISP: breath, with only a ghost of the note in it */
            x = 1.4f * noise_s(&v->u[0]) + 0.2f * saw_bl(v->ph[0], d);
            break;
        case 4:                                  /* CHIP: a speech chip's buzz */
            x = v->ph[0] < 0.25f ? 1.0f : -0.33f;
            break;
        case 5:                                  /* MONK: a saw an octave down and its fifth above */
            x = saw_bl(v->ph[0], d) + 0.4f * saw_bl(v->ph[1], minf(f * 1.5f, 0.45f));
            v->ph[1] += minf(f * 1.5f, 0.45f); if (v->ph[1] >= 1.0f) v->ph[1] -= 1.0f;
            break;
        default:
            x = saw_bl(v->ph[0], d);
        }
        v->ph[0] += d;
        if (v->ph[0] >= 1.0f) v->ph[0] -= 1.0f;
        out[i] = x;
    }
}

/* ---- the mouth over the mix ------------------------------------------------------------- */
static void mouth_step(const Blk *b)
{
    f32 sr = b->sr, isr = b->isr;
    int i, gate = 0;
    if (!M.me)
        for (i = 0; i < NINST; i++)
            if (insts[i]->post == inst_blab.post)
                M.me = i + 1;
    for (i = 0; i < NVOICE; i++)
        if (voices[i].stage && voices[i].gate && PART_INST(voices[i].part) == M.me - 1)
            gate = 1;
    M.since_on++;
    if (M.fresh_note) {
        M.fresh_note = 0;
        if (M.since_on > 2 || M.shown < 0 || M.qn == 0)   /* (notes of a chord sing one syllable) */
            syllable(sr);
    }
    if (gate)
        M.quiet = 0;
    else if (++M.quiet > (uint32_t)(2.5f * sr / (f32)CTRL) && M.at) {
        M.at = 0;                                /* a long rest: from the top of the phrase again */
        M.shown = -1;
    }
    if (M.gate && !gate && M.qleft < 0)          /* let go on the vowel: on to its ending */
        start_phone(M.qi + 1, sr);
    M.gate = gate;
    if (M.qleft > 0 && --M.qleft == 0) {
        if (M.qi + 1 < M.qn && M.qd[M.qi + 1] == 0 && !gate)
            start_phone(M.qi + 2, sr);           /* the keys went up before the vowel came: past it */
        else
            start_phone(M.qi + 1, sr);
    }

    {
        /* ease: the formants glide (DRAWL), the sources come and go quickly */
        f32 kf = onepole_k(0.010f + 0.045f * M.drawl, sr / (f32)CTRL), ka = onepole_k(0.004f, sr / (f32)CTRL);
        f32 sc = M.scale * (M.algo == 5 ? 0.86f : M.algo == 6 ? 1.16f : M.algo == 7 ? 1.45f : 1.0f);
        f32 bwk = M.algo == 2 ? 0.6f : M.algo == 5 ? 0.45f : 1.0f;
        for (i = 0; i < 3; i++) {
            if (M.tf[i] > 0.0f)
                M.f[i] += (M.tf[i] - M.f[i]) * kf;
            M.bw[i] += (M.tbw[i] - M.bw[i]) * kf;
        }
        M.av += (M.tav - M.av) * ka;
        M.ah += (M.tah - M.ah) * ka;
        M.af += (M.taf - M.af) * (M.taf > M.af ? ka : ka * 0.6f);
        M.fc += (M.tfc - M.fc) * ka;
        M.fq += (M.tfq - M.fq) * ka;
        {
            f32 q[3];
            for (i = 0; i < 3; i++)                  /* CHIP: the formants move in steps, as a cheap chip's do */
                q[i] = M.algo == 4 ? (f32)(int)(M.f[i] * (1.0f / 60.0f) + 0.5f) * 60.0f : M.f[i];
            res_set(&M.r[0], clampf(q[0] * sc, 150.0f, 1400.0f), maxf(40.0f, M.bw[0] * bwk), isr);
            res_set(&M.r[1], clampf(q[1] * sc, 400.0f, 3600.0f), maxf(40.0f, M.bw[1] * bwk), isr);
            res_set(&M.r[2], clampf(q[2] * sc, 1200.0f, 5000.0f), maxf(60.0f, M.bw[2] * bwk), isr);
        }
        res_set(&M.r[3], clampf(3500.0f * sc, 2500.0f, 6000.0f), 250.0f, isr);
        res_set(&M.r[4], clampf(4500.0f * sc, 3500.0f, 7500.0f), 300.0f, isr);
        svf_set(&M.fr, M.fc * (0.85f + 0.15f * sc), maxf(0.5f, M.fq), isr);
        if (M.algo == 0) {
            /* ROBOT: the mouth's shape read off at every band, for the bank to put on the carrier */
            for (i = 0; i < NB; i++) {
                f32 g = 0.02f, fz;
                int j;
                for (j = 0; j < 3; j++) {                  /* a bump an octave and a bit wide at each formant */
                    f32 d = (lg2(BANDF[i]) - lg2(maxf(100.0f, M.f[j] * sc))) * 2.6f;
                    g += (j == 0 ? 1.0f : j == 1 ? 0.8f : 0.55f) * exp2fx(-d * d);
                }
                fz = BANDF[i] / maxf(200.0f, M.fc);       /* the hiss: a bump around its centre */
                fz = fz > 1.0f ? 1.0f / fz : fz;
                fz = fz * fz * fz;
                M.bg[i] = g;
                M.bgn[i] = fz;
            }
        }
    }
}

static void blab_post(const Blk *b, f32 *l, f32 *r, int n)
{
    int i, j, a;
    f32 air, av, ah, af, lev = M.lev;
    mouth_step(b);
    a = M.algo;
    air = M.air * M.air;
    av = M.av;
    ah = M.ah + air * 0.5f * av;
    af = M.af;
    if (!M.rng) M.rng = 0x51EEB0B5u;
    if (a == 4)
        svf_set(&M.chip_lp, 4200.0f, 0.8f, b->isr);
    if (a == 0 && M.band_sr != b->sr) {         /* ROBOT's bands stand still: set once */
        M.band_sr = b->sr;
        for (j = 0; j < NB; j++) {
            svf_set(&M.band[j], BANDF[j], 5.0f, b->isr);
            svf_set(&M.band_a[j], BANDF[j], 5.0f, 2.0f * b->isr);
        }
    }
    for (i = 0; i < n; i++) {
        f32 c = 0.5f * (l[i] + r[i]), nz = noise_s(&M.rng), x, y, lp, bp, hiss;
        lev += (absf(c) - lev) * 0.002f;
        x = c * av + nz * ah * lev * 2.5f;
        for (j = 0; j < 5; j++) {
            f32 o = M.r[j].a * x + M.r[j].b * M.r[j].y1 + M.r[j].c * M.r[j].y2;
            M.r[j].y2 = M.r[j].y1;
            M.r[j].y1 = o;
            x = o;
        }
        SVF_TICK(M.fr, M.frs, nz, lp, bp);
        hiss = bp * M.fr.k * af * 0.16f;
        y = (x - BLAB_PRE * M.rad) * 1.8f;
        M.rad = x;
        if (a == 0) {
            /* ROBOT: a vocoder. The mouth is the voice it listens to: each of sixteen bands of it follows how
             * loud that band is, and lets that much of the same band of the carrier (the chord) through. The
             * hiss goes straight past, as a vocoder's sibilance does. */
            f32 out = 0.0f;
            if (M.half ^= 1) {                   /* it listens at half the rate: a loudness needs no more */
                f32 m = 0.5f * (y + M.yprev) - M.mprev;    /* (tilted up, so the low end does not spill into every band) */
                M.mprev = 0.5f * (y + M.yprev);
                for (j = 0; j < NB; j++) {
                    f32 e = M.env[j], am;
                    SVF_TICK(M.band_a[j], M.bsn[j], m, lp, bp);
                    bp *= M.band_a[j].k;
                    SVF_TICK(M.band_a[j], M.bsa[j], bp, lp, am);   /* (two poles twice: steep enough to hear a formant) */
                    am = absf(am * M.band_a[j].k) * BANDI[j];
                    e += (am - e) * (am > e ? 0.04f : 0.008f);
                    M.env[j] = e;
                    M.eg[j] = e * sqrtfx(e) * BANDW[j];       /* (e^1.5: the bands told apart more) */
                }
            }
            M.yprev = y;
            for (j = 0; j < NB; j++) {
                SVF_TICK(M.band[j], M.bs[j], c, lp, bp);
                out += bp * M.band[j].k * M.eg[j];
            }
            y = BLAB_RMIX * y + (1.0f - BLAB_RMIX) * out * 1300.0f;   /* (the mouth under it, so the words stay words) */
        } else if (a == 4) {                     /* CHIP: ten kilohertz, six bits */
            M.crush_ph += BLAB_CHZ * b->isr;
            if (M.crush_ph >= 1.0f) {
                M.crush_ph -= 1.0f;
                M.crush_hold = (f32)(int)((y + hiss) * BLAB_CQ) * (1.0f / BLAB_CQ);
            }
            SVF_TICK(M.chip_lp, M.chip_s, M.crush_hold, lp, bp);
            y = lp - hiss;                       /* (and smoothed after, as the chips' own filters did) */
        }
        y += hiss;
        if (a == 7)                              /* ALIEN: the voice rung against itself */
            y *= 0.7f + 0.6f * tsin(M.vib * 9.0f + (f32)i * 0.0f);
        y = 0.48f * softclip(y * 2.2f);         /* (its own soft ceiling, under the output limiter's) */
        l[i] = r[i] = y;
    }
    M.lev = lev;
    if (absf(M.lev) < 1e-9f) M.lev = 0.0f;
    for (j = 0; j < 5; j++)
        if (absf(M.r[j].y1) < 1e-12f) M.r[j].y1 = M.r[j].y2 = 0.0f;
}

/* ---- picture ---------------------------------------------------------------------------- *
 * A voiceprint: time runs right to left, pitch of the mouth up the screen, and the formants are written
 * as they are sung, so you see the vowels. The words sit above it: the phrase, and the syllable sounding
 * large. With nothing played the next vowel waits at the right edge, breathing, so MOUTH moves it, AIR
 * hazes it and WORDS changes what it will be. */
#define NH 80                    /* the print's columns, three pixels each */
#define PY0 96.0f                /* the print: from this row */
#define PY1 236.0f               /* to this */
static struct {
    uint8_t fy[NH][3], a[NH], h[NH], hy[NH], band[NH][NB];
    int head;
    f32 acc, pop, popv, wob;
    int seen;
    int inited;
} P;

static f32 fy(f32 hz)                            /* a frequency to a row: 120 Hz at the bottom, 5 kHz at the top */
{
    return PY1 - (lg2(maxf(hz, 60.0f)) - 6.907f) * ((PY1 - PY0) / 5.38f);
}

static void column(const Vis *vis, f32 sc, f32 lev)
{
    int j;
    uint8_t *c;
    P.head = (P.head + 1) % NH;
    for (j = 0; j < 3; j++)
        P.fy[P.head][j] = (uint8_t)clampf(fy(M.f[j] * sc), 0.0f, 255.0f);
    P.a[P.head] = (uint8_t)(sat(M.av * lev) * 255.0f);
    P.h[P.head] = (uint8_t)(sat(M.af * 1.2f + M.ah * lev * 1.6f) * 255.0f);
    P.hy[P.head] = (uint8_t)clampf(fy(M.fc > 200.0f ? M.fc : 3000.0f), 0.0f, 255.0f);
    c = P.band[P.head];
    for (j = 0; j < NB; j++) {                   /* ROBOT's rows: the mouth read off at its bands */
        f32 g = 0.02f;
        int k;
        for (k = 0; k < 3; k++) {
            f32 d = (lg2(BANDF[j]) - lg2(maxf(100.0f, M.f[k] * sc))) * 2.6f;
            g += (k == 0 ? 1.0f : k == 1 ? 0.8f : 0.55f) * exp2fx(-d * d);
        }
        g = g * M.av * lev + (M.af + M.ah * lev) * (j > 8 ? 0.6f : 0.15f);
        c[j] = (uint8_t)(sat(g) * 255.0f);
    }
    (void)vis;
}

/* a stretch of ribbon from x0 to x1: soft (bright on its axis, nothing at its edges) or crisp */
static void ribbon(f32 x0, f32 y0, f32 x1, f32 y1, f32 w0, f32 w1, f32 a, int crisp)
{
    f32 q[8];
    q[0] = x0; q[1] = y0 - 0.5f * w0; q[2] = x1 + 0.6f; q[3] = y1 - 0.5f * w1;
    q[4] = x1 + 0.6f; q[5] = y1; q[6] = x0; q[7] = y0;
    if (a < 0.01f)
        return;
    if (crisp) {
        q[5] = y1 + 0.5f * w1; q[7] = y0 + 0.5f * w0;
        g_fill(q, 4, a);
        return;
    }
    g_fillv(q, 4, minf(q[1], q[3]), 0.0f, maxf(y0, y1), a);
    q[1] = y0 + 0.5f * w0; q[3] = y1 + 0.5f * w1;
    g_fillv(q, 4, minf(y0, y1), a, maxf(q[1], q[3]), 0.0f);
}

static void grain(f32 x, f32 y, f32 spread, int n, f32 a, uint32_t *s)
{
    int g;
    for (g = 0; g < n; g++)
        g_rect(x + rng_f(s) * 3.0f, y + rng_s(s) * spread, 1.0f, 1.0f, a * (0.35f + 0.65f * rng_f(s)));
}

static SMALL void blab_draw(const Vis *vis)
{
    f32 sc = M.scale * (M.algo == 5 ? 0.86f : M.algo == 6 ? 1.16f : M.algo == 7 ? 1.45f : 1.0f);
    f32 lev = sat(vis->level * 4.0f), air = vis->k[3], t = vis->t, dt = vis->dt, mid = 0.5f * (PY0 + PY1);
    int i, j, k, ph = M.phrase, cur = M.shown, style = vis->algo_t < 0.5f ? vis->algo_prev : vis->algo;
    f32 fade_in = absf(2.0f * vis->algo_t - 1.0f);
    if (!P.inited || vis->fresh) {
        uint8_t *z = (uint8_t *)&P;
        for (i = 0; i < (int)sizeof P; i++) z[i] = 0;
        P.inited = 1;
        P.seen = cur;
    }
    if (cur != P.seen) {                         /* a new syllable: the big word jumps */
        P.seen = cur;
        P.popv += 3.0f;
    }
    spring_z(&P.pop, &P.popv, 0.0f, 18.0f, 0.35f, dt);
    P.wob += dt * (style == 6 ? 5.6f : 0.0f);
    P.acc += dt * (34.0f + 40.0f * (1.0f - vis->k[2]));       /* snappy scrolls fast, a drawl slow */
    k = 0;
    while (P.acc >= 1.0f && k++ < 6) {
        P.acc -= 1.0f;
        column(vis, sc, lev);
    }
    if (P.acc >= 1.0f) P.acc = 0.0f;

    g_clear();
    g_mode(G_MAX);
    g_dim(0.35f + 0.65f * fade_in);

    /* the next vowel, ruled faintly across the print: MOUTH moves the rules, AIR frays them */
    {
        const Syl *y = &syl[ph][M.at % (nsyl[ph] ? nsyl[ph] : 1)];
        const Ph *v = &PH[y->ph[y->nuc]];
        f32 nf[3], quiet = 1.0f - sat(lev * 4.0f);
        nf[0] = (f32)v->f1 * 10.0f; nf[1] = (f32)v->f2 * 20.0f; nf[2] = (f32)v->f3 * 20.0f;
        for (j = 0; j < 3 && quiet > 0.02f; j++) {
            f32 yy = fy(nf[j] * sc), xx;
            uint32_t s = 0x5EEDu + (uint32_t)j * 131u + (uint32_t)(t * 8.0f);
            if (style == 0)
                yy = PY1 - 8.6f * (f32)(1 + (int)((PY1 - yy) / 8.6f)) + 3.5f;
            g_line(0.0f, yy, 226.0f, yy, 1.4f, quiet * 0.13f * (1.0f - 0.2f * (f32)j));
            xx = fractf(t * 0.12f + (f32)j * 0.31f) * 300.0f - 40.0f;   /* a swell travels along each rule */
            g_beam(xx - 40.0f, yy, xx + 40.0f, yy, 1.0f, 3.0f + 2.0f * air, 0.0f, quiet * 0.35f, 1.0f);
            if (air > 0.05f)
                grain(xx - 30.0f + rng_f(&s) * 60.0f, yy, 2.0f + 8.0f * air, (int)(air * 10.0f), quiet * 0.4f, &s);
        }
    }

    /* the print, oldest on the left */
    for (k = 0; k < NH - 1; k++) {
        int c0 = (P.head + 1 + k) % NH, c1 = (c0 + 1) % NH;
        f32 x0 = (f32)k * 3.0f, x1 = x0 + 3.0f, age = (f32)k / (f32)(NH - 1), fade = 0.2f + 0.8f * age * age;
        f32 a0 = (f32)P.a[c0] * (1.0f / 255.0f), a1 = (f32)P.a[c1] * (1.0f / 255.0f);
        f32 h0 = (f32)P.h[c0] * (1.0f / 255.0f);
        uint32_t s = (uint32_t)c0 * 2654435761u ^ 0x9E3779B9u;
        if (style == 0) {                        /* ROBOT: sixteen rows of lamps */
            for (j = 0; j < NB; j++) {
                f32 v = (f32)P.band[c0][j] * (1.0f / 255.0f);
                if (v > 0.04f)
                    g_rect(x0, PY1 - 8.6f * (f32)(j + 1), 2.0f, 7.0f, fade * minf(1.0f, v * 1.3f));
            }
            continue;
        }
        if (style == 3 || style == 4) {          /* WHISP: grain; CHIP: blocks on a grid */
            for (j = 0; j < 3; j++) {
                f32 y = (f32)P.fy[c0][j], w = 1.0f - 0.25f * (f32)j;
                if (a0 + h0 < 0.03f) continue;
                if (style == 3)
                    grain(x0, y, 4.0f + 10.0f * maxf(a0, h0), (int)(10.0f * maxf(a0, h0) * w) + 1, fade * w, &s);
                else if ((k & 1) == 0 && a0 > 0.03f)
                    g_rect((f32)((int)x0 / 6 * 6), (f32)((int)y / 6 * 6), 5.0f, 5.0f + (a0 > 0.5f ? 6.0f : 0.0f), fade * minf(1.0f, a0 * 1.6f) * w);
            }
        } else {
            for (j = 0; j < 3; j++) {
                f32 y0 = (f32)P.fy[c0][j], y1 = (f32)P.fy[c1][j], w = 1.0f - 0.25f * (f32)j, wd0, wd1, o;
                if (a0 + a1 < 0.03f)
                    continue;
                if (style == 6) {                /* DIVA: the line trembles with her vibrato */
                    o = 3.0f * a0 * sin_turns(P.wob + (f32)k * 0.09f);
                    y0 += o;
                    y1 += 3.0f * a1 * sin_turns(P.wob + (f32)(k + 1) * 0.09f);
                }
                wd0 = 1.5f + 11.0f * a0 * w;
                wd1 = 1.5f + 11.0f * a1 * w;
                if (style == 1) {                /* CHOIR: three voices, a little apart */
                    ribbon(x0, y0 - 5.0f, x1, y1 - 5.0f, wd0 * 0.6f, wd1 * 0.6f, fade * a0 * 0.55f * w, 0);
                    ribbon(x0, y0 + 5.0f, x1, y1 + 5.0f, wd0 * 0.6f, wd1 * 0.6f, fade * a0 * 0.55f * w, 0);
                }
                if (style == 5 && j == 1)       /* MONK: the whistle above the drone, a hard thin line */
                    g_line(x0, y0, x1, y1, 1.2f, fade * minf(1.0f, a0 * 2.0f));
                else
                    ribbon(x0, y0, x1, y1, wd0, wd1, fade * minf(1.0f, a0 * 1.5f) * w, style == 2);
                if (style == 7)                  /* ALIEN: its print comes back upside down */
                    ribbon(x0, 2.0f * mid - y0, x1, 2.0f * mid - y1, wd0 * 0.6f, wd1 * 0.6f, fade * a0 * 0.6f * w, 0);
            }
        }
        if (h0 > 0.04f)                          /* hiss: grain around its centre */
            grain(x0, (f32)P.hy[c0], 18.0f, (int)(h0 * 9.0f) + 1, fade * 0.9f, &s);
        if (air > 0.05f && a0 > 0.05f)           /* AIR: breath on everything */
            grain(x0, (f32)P.fy[c0][(k >> 1) % 3], 14.0f, (int)(air * air * 6.0f * a0) + 1, fade * 0.5f, &s);
    }

    /* the mouth now, at the right edge: where it is, or with nothing sounding, the next vowel waiting */
    {
        const Syl *y = &syl[ph][(cur < 0 ? M.at : M.at) % (nsyl[ph] ? nsyl[ph] : 1)];
        const Ph *v = &PH[y->ph[y->nuc]];
        f32 nf[3], breath = 0.5f + 0.5f * sin_turns(t * 0.35f), on = sat(lev * 3.0f);
        nf[0] = (f32)v->f1 * 10.0f; nf[1] = (f32)v->f2 * 20.0f; nf[2] = (f32)v->f3 * 20.0f;
        for (j = 0; j < 3; j++) {
            f32 yy = lerpf(fy(nf[j] * sc), fy(M.f[j] * sc), on), ww = 1.0f - 0.25f * (f32)j;
            uint32_t s = 0xB1ABu + (uint32_t)j * 977u + (uint32_t)(t * 12.0f);
            if (style == 0)
                yy = PY1 - 8.6f * (f32)(1 + (int)((PY1 - yy) / 8.6f)) + 3.5f;
            g_line(230.0f, yy, 238.0f, yy, 2.0f + 3.0f * on, (0.25f + 0.2f * breath + 0.55f * on) * ww);
            if (air > 0.05f)
                grain(222.0f, yy, 6.0f + 8.0f * air, (int)(air * 8.0f), 0.3f + 0.3f * breath, &s);
        }
    }

    /* the words */
    g_dim(1.0f);
    {
        const char *s = PHRASE[ph].show;
        char seg[24];
        f32 size = 11.0f, tw = 0.0f, x, big;
        int sy, n;
        /* the phrase, its syllables lit as far as it has sung */
        for (i = 0, n = 0; s[i]; i++)
            if (s[i] != '-') seg[n < 23 ? n++ : n] = s[i];
        seg[n < 24 ? n : 23] = 0;
        tw = g_text_w(PHRASE[ph].show, size);
        if (tw > 226.0f) size *= 226.0f / tw;
        x = 120.0f - 0.5f * g_text_w(PHRASE[ph].show, size);
        for (i = 0, sy = 0; s[i];) {
            int e = i;
            f32 a;
            while (s[e] && s[e] != '-' && s[e] != ' ') e++;
            for (n = 0; n < e - i && n < 23; n++) seg[n] = s[i + n];
            seg[n] = 0;
            a = sy == cur ? 1.0f : (cur >= 0 && sy < cur ? 0.55f : 0.22f);
            g_text(x, 12.0f, seg, size, 1.3f, a, 0);
            x += g_text_w(seg, size);
            if (s[e] == ' ') x += g_text_w(" ", size);
            if (s[e] == '-') x += g_text_w("-", size) * 0.4f;
            sy++;
            i = s[e] ? e + 1 : e;
        }
        /* the syllable: sounding, or the one the next key will sing */
        {
            int want = cur >= 0 && lev > 0.02f ? cur : M.at % (nsyl[ph] ? nsyl[ph] : 1);
            for (i = 0, sy = 0, n = 0; s[i]; i++) {
                if (s[i] == '-' || s[i] == ' ') { sy++; continue; }
                if (sy == want && n < 23) seg[n++] = s[i];
            }
            seg[n] = 0;
            big = 38.0f * (1.0f + 0.12f * P.pop);
            tw = g_text_w(seg, big);
            if (tw > 224.0f) big *= 224.0f / tw;
            g_text(120.0f, 40.0f - 0.5f * (big - 38.0f), seg, big, 3.4f,
                   cur >= 0 && lev > 0.02f ? 0.6f + 0.4f * lev : 0.28f + 0.12f * sin_turns(t * 0.7f), 1);
        }
    }
}

const InstDef inst_blab = {
    "BLAB",
    {120, 255, 170},
    {"WORDS", "MOUTH", "DRAWL", "AIR"},
    {"ROBOT", "CHOIR", "TALK", "WHISP", "CHIP", "MONK", "DIVA", "ALIEN"},
    {{0, 50, 30, 15}, 0, {2, 50, 100, 30}, {0, 40, 0, 0}, {10, 30, 30, 60}, {0, 3, 1, 60}, {0, 0, 0}},
    0.36f,
    blab_block, blab_note, blab_render, blab_post, blab_draw,
};

#ifdef BLAB_TOOLS                                /* (the tuning tools: how many keys a phrase takes) */
int blab_syllables(int p) { if (!nsyl[0]) parse(); return p >= 0 && p < NPHRASE ? nsyl[p] : 0; }
#endif
