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
 * ARPAbet-like phones split the same way, by '-' and '/'. The parser turns them into phones once. The
 * first, at WORDS all the way down, is your own: written on the keys (WRITE) and spelt out below. */
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
#define NWORD (NPHRASE + 1)      /* WORDS' choices: your own, then the phrases */
#define NSYL 16                  /* syllables a phrase, at most */
#define NSP 10                   /* phones a syllable, at most */
#define NSHOW 84                 /* your own words as shown, split */

typedef struct Syl {
    uint8_t n, nuc;              /* phones, and which is the vowel */
    uint8_t ph[NSP];
    uint8_t word;                /* 1: the first syllable of a word */
} Syl;

static Syl syl[NPHRASE][NSYL];
static uint8_t nsyl[NPHRASE];
/* your own, twice over: the keys write one while the voice sings from the other, then they swap */
static Syl mine[2][NSYL];
static uint8_t nmine[2];
static char mine_show[2][NSHOW];
static volatile uint8_t mcur, mine_gen;
volatile uint8_t blab_writing;
static volatile int8_t want_letter = -1;         /* WRITE: the letter the next key sings */

static const Syl *syls(int p) { return p ? syl[p - 1] : mine[mcur]; }
static int nsyls(int p) { return p ? nsyl[p - 1] : nmine[mcur]; }
static const char *shows(int p) { return p ? PHRASE[p - 1].show : mine_show[mcur]; }

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

/* phones, split by '-' and '/', into syllables: how many */
static APART int parse_say(const char *s, Syl *y0, int max)
{
    Syl *y = y0;
    int ns = 0, i;
    y->n = 0; y->nuc = 255; y->word = 1;
    while (*s && *s != '|') {
        if (*s == ' ') { s++; continue; }
        if (*s == '-' || *s == '/') {
            if (y->n && ns < max - 1) {
                y = &y0[++ns];
                y->n = 0; y->nuc = 255; y->word = (uint8_t)(*s == '/');
            } else if (*s == '/' && !y->n)
                y->word = 1;
            s++;
            continue;
        }
        for (i = 0; s[i] && s[i] != ' ' && s[i] != '-' && s[i] != '/' && s[i] != '|'; i++)
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
    for (i = 0; i <= ns; i++)
        if (y0[i].nuc == 255)
            y0[i].nuc = 0;
    return y0[0].n ? ns + 1 : 0;
}

/* ---- your own words: spelling to sounds --------------------------------------------------- *
 * English spelling, read the way a reader guesses at a word: the words it gets wrong most are looked up
 * (EX), the rest go letter by letter through RU, longest first, and what RU leaves goes by the letter
 * itself (a vowel long before a last E or at the end of a word, short otherwise). ^ is the start of a
 * word, $ its end. Then each word is cut into syllables, a vowel each, a consonant between two going with
 * the second, and two going one each way unless they start a word well together (PL, TR, ST...). */
static const char LETTER[] =   /* WRITE sings each letter's name */
    "EY|B IY|S IY|D IY|IY|EH F|JH IY|EY CH|AY|JH EY|K EY|EH L|EH M|EH N|OW|P IY|K Y UW|AA R|EH S|T IY|"
    "Y UW|V IY|D AH B L Y UW|EH K S|W AY|Z IY|";

static const char EX[] =
    "THE:DH AX|A:AX|I:AY|TO:T UW|DO:D UW|YOU:Y UW|YOUR:Y AO R|OF:AH V|ONE:W AH N|TWO:T UW|ARE:AA R|"
    "WAS:W AH Z|IS:IH Z|HAS:HH AE Z|AS:AE Z|WHAT:W AH T|WHO:HH UW|LOVE:L AH V|SAID:S EH D|EYE:AY|"
    "THIS:DH IH S|THAT:DH AE T|THEY:DH EY|THEM:DH EH M|THEN:DH EH N|THERE:DH EH R|THEIR:DH EH R|"
    "WHERE:W EH R|COME:K AH M|SOME:S AH M|DONE:D AH N|GONE:G AO N|HAVE:HH AE V|GIVE:G IH V|LIVE:L IH V|"
    "FOUR:F AO R|EIGHT:EY T|OWN:OW N|KNOW:N OW|GOOD:G UH D|FROM:F R AH M|WANT:W AA N T|BEEN:B IH N|"
    "ANY:EH N IY|MANY:M EH N IY|AGAIN:AX G EH N|PEOPLE:P IY P AX L|MUSIC:M Y UW Z IH K|HEART:HH AA R T|"
    "WORLD:W ER L D|WORD:W ER D|WORK:W ER K|BE:B IY|ME:M IY|WE:W IY|HE:HH IY|SHE:SH IY|YEAH:Y EH|"
    "OK:OW K EY|PUT:P UH T|FULL:F UH L|BUSY:B IH Z IY|ONCE:W AH N S|BECAUSE:B IH K AH Z|ROBOT:R OW B AA T|"
    "HELLO:HH EH L OW|BODY:B AA D IY|MONEY:M AH N IY|HONEY:HH AH N IY|COMPUTER:K AX M P Y UW T ER|EYES:AY Z|GONNA:G AA N AX|DOOR:D AO R|FLOOR:F L AO R|OUT:AW T|NOW:N AW|HOW:HH AW|WOW:W AW|DOWN:D AW N|BABY:B EY B IY|WATER:W AO T ER|"
    "ONLY:OW N L IY|EVERY:EH V R IY|FRIEND:F R EH N D|NEVER:N EH V ER|SING:S IH NG|GREAT:G R EY T|"
    "BREAK:B R EY K|MACHINE:M AX SH IY N|CRAZY:K R EY Z IY|LADY:L EY D IY|PAPER:P EY P ER|TIGER:T AY G ER|"
    "HEY:HH EY|BEAUTIFUL:B Y UW T IH F AX L|HEAVEN:HH EH V AX N|HEAD:HH EH D|DEAD:D EH D|READY:R EH D IY|"
    "GREY:G R EY|SEVEN:S EH V AX N|AWAY:AX W EY|";
static const char RU[] =
    "^KN:N|^WR:R|^Y:Y|RH:R|OLD:OW L D|IND$:AY N D|ILD$:AY L D|TION:SH AX N|SION:ZH AX N|OUGH:AO|EIGH:EY|IGH:AY|DGE:JH|TCH:CH|EAR:IY R|AIR:EH R|"
    "OUR:AW R|ALL:AO L|ALK:AO K|OOK:UH K|QU:K W|CH:CH|SH:SH|TH:TH|PH:F|WH:W|CK:K|NG:NG|NK:NG K|EE:IY|"
    "EA:IY|OO:UW|OU:AW|OW$:OW|OW:AW|EY$:IY|AI:EY|AY:EY|OA:OW|OI:OY|OY:OY|AU:AO|AW:AO|EW:UW|UE:UW|IE$:AY|IE:IY|"
    "EY:EY|AR:AA R|OR:AO R|ER:ER|IR:ER|UR:ER|LE$:AX L|GE$:JH|X:K S|^GH:G|GH:|A$:AA|";
#define NTK 48
static struct { char t[NTK][3]; uint8_t src[NTK]; int n; } W;   /* a word's phones, and the letter each began at */

static int isvow(const char *t)
{
    static const char V[] = "IYIHEHAEAAAOUHUWAHERAXEYAYOYOWAW";
    int i;
    for (i = 0; V[i]; i += 2)
        if (t[0] == V[i] && t[1] == V[i + 1])
            return 1;
    return 0;
}

static int vowel_letter(char c) { return c == 'A' || c == 'E' || c == 'I' || c == 'O' || c == 'U'; }

static void emit(const char *p, int n, int src)  /* phones (space split, to n chars) onto the word */
{
    int i = 0;
    while (i < n && p[i] && p[i] != '|') {
        int k = 0;
        while (i < n && p[i] == ' ') i++;
        if (i >= n || !p[i] || p[i] == '|') break;
        if (W.n < NTK) {
            while (i < n && p[i] && p[i] != ' ' && p[i] != '|' && k < 2)
                W.t[W.n][k++] = p[i++];
            W.t[W.n][k] = 0;
            W.src[W.n++] = (uint8_t)src;
        }
        while (i < n && p[i] && p[i] != ' ' && p[i] != '|') i++;
    }
}

/* the entry of a table that w (n letters, from at) starts with: its length matched, and its phones */
static int look(const char *tab, const char *w, int at, int n, int whole, const char **ph)
{
    const char *e = tab;
    while (*e) {
        const char *g = e;
        int k = 0, ok = 1, start = *g == '^';
        if (start) g++;
        while (g[k] && g[k] != ':' && g[k] != '$') k++;
        if (start && at) ok = 0;
        if (whole && k != n - at) ok = 0;
        if (g[k] == '$' && at + k != n) ok = 0;
        if (ok && at + k <= n) {
            int j;
            for (j = 0; j < k; j++)
                if (g[j] != w[at + j]) { ok = 0; break; }
            if (ok) {
                while (*g != ':') g++;
                *ph = g + 1;
                return k;
            }
        }
        while (*e && *e != '|') e++;
        if (*e) e++;
    }
    return 0;
}

static APART void spell(const char *w, int n)    /* a word's letters to phones, into W */
{
    static const char CON[] = "  B K D   F G HH  JHK L M N   P K R S T   V W   Y Z ";   /* each letter by itself */
    const char *ph;
    int i = 0, k, other = 0, skip = -1;
    W.n = 0;
    if (look(EX, w, 0, n, 1, &ph)) {
        emit(ph, 64, 0);
        for (k = 0; k < W.n; k++)                /* (where its syllables start, near enough) */
            W.src[k] = (uint8_t)(k * n / W.n);
        return;
    }
    if (n == 1) {                                /* a letter alone: its name */
        for (ph = LETTER, k = w[0] - 'A'; k > 0; ph++)
            k -= *ph == '|';
        emit(ph, 64, 0);
        return;
    }
    while (i < n) {
        char c = w[i], nx = i + 1 < n ? w[i + 1] : 0;
        if (i == skip || (i && c == w[i - 1] && !vowel_letter(c)) || (c == 'H' && i == n - 1 && i)) {
            i++;                                 /* a double consonant: one; a last E after one, or H: not said */
            continue;
        }
        if ((k = look(RU, w, i, n, 0, &ph)) != 0 && !(skip > i && skip < i + k)) {
            emit(ph, 64, i);
            other |= vowel_letter(c) || c == 'Y';
            i += k;
            continue;
        }
        if (vowel_letter(c)) {
            static const char SHORT[] = "AEEHIHAAAH", LONG[] = "EYIYAYOWUW";
            int v = c == 'A' ? 0 : c == 'E' ? 1 : c == 'I' ? 2 : c == 'O' ? 3 : 4;
            int magic = i + 2 < n && w[i + 2] == 'E' && !vowel_letter(nx) && (i + 3 == n || (i + 4 == n && w[i + 3] == 'S'));
            if (c == 'E' && i == n - 1 && other) { i++; continue; }     /* a last E: not said */
            if (!i && c == 'A' && n > 3 && !vowel_letter(nx) && vowel_letter(w[2]))
                emit("AX", 2, i);                /* ALONG, AWAY, AGO */
            else if (magic || (i == n - 1) || (vowel_letter(nx) && nx != c)
                     || (v >= 3 && i + 2 < n && !vowel_letter(nx) && nx != 'R' && vowel_letter(w[i + 2])))
                emit(LONG + 2 * v, 2, i);        /* (and O, U open: OVER, SUPER) */
            else
                emit(SHORT + 2 * v, 2, i);
            other = 1;
            if (magic) {
                skip = i + 2;
                if (nx == 'S') { emit("Z", 1, i + 1); i += 2; continue; }   /* ROSE, WISE */
            }
            i++;
            continue;
        }
        if (c == 'Y') {                          /* last: EE after a vowel, EYE alone (MY); inside: IH */
            emit(i == n - 1 ? (other ? "IY" : "AY") : "IH", 2, i);
            other = 1;
        } else if (c == 'C')
            emit(nx == 'E' || nx == 'I' || nx == 'Y' ? "S" : "K", 1, i);
        else if (c >= 'A' && c <= 'Z')
            emit(CON + 2 * (c - 'A'), 2, i);
        i++;
    }
}

static int onset2(const char *a, const char *b)  /* two consonants that start a syllable well */
{
    int liq = (b[0] == 'R' || b[0] == 'L' || b[0] == 'W') && !b[1];
    if (a[0] == 'S' && !a[1])
        return liq || ((b[0] == 'P' || b[0] == 'T' || b[0] == 'K' || b[0] == 'M' || b[0] == 'N') && !b[1]);
    return liq && (a[1] == 'H' ? a[0] == 'T' || a[0] == 'S' : a[1] == 0 && a[0] != 'R' && a[0] != 'L' && a[0] != 'M'
                   && a[0] != 'N' && a[0] != 'W' && a[0] != 'Y' && a[0] != 'V' && a[0] != 'Z');
}

static char *put(char *d, const char *s, const char *end)
{
    while (*s && d < end)
        *d++ = *s++;
    return d;
}

/* your words into syllables: the voice's copy that is not singing, then swapped in */
static char say[NWORDS * 10];
static APART void build_mine(void)
{
    const char *s = st.words;
    char *d = say, *sh, *de = say + sizeof say - 8, *she;
    int b = mcur ^ 1, i = 0, n;
    sh = mine_show[b];
    she = sh + NSHOW - 3;
    while (s[i]) {
        int w0, nv = 0, k, cut[NTK], nc = 0, last = -1, at;
        while (s[i] == ' ') i++;
        if (!s[i]) break;
        for (w0 = i; s[i] && s[i] != ' '; i++)
            ;
        spell(s + w0, i - w0);
        for (k = 0; k < W.n; k++)                /* where each syllable after the first begins */
            if (isvow(W.t[k])) {
                if (last >= 0) {
                    int m = k - last - 1, c = m == 0 ? k : m == 1 ? k - 1 : onset2(W.t[k - 2], W.t[k - 1]) ? k - 2 : k - 1;
                    if (nc < NTK) cut[nc++] = c;
                }
                last = k;
                nv++;
            }
        if (d != say)
            d = put(d, " / ", de);
        if (sh != mine_show[b] && sh < she)
            *sh++ = ' ';
        for (k = 0, at = 0; k < W.n; k++) {
            if (at < nc && cut[at] == k) {
                d = put(d, " - ", de);
                at++;
            }
            d = put(d, " ", de);
            d = put(d, W.t[k], de);
        }
        for (k = 0, at = 0, last = 0; w0 + k < i; k++) {   /* the letters, split where the syllables are */
            while (at < nc) {
                int c = W.src[cut[at]], len = i - w0;
                if (c + 1 < len && s[w0 + c] == s[w0 + c + 1])
                    c++;                         /* (a double letter splits between: HEL-LO) */
                if (c <= last) c = last + 1;
                if (c != k) break;
                if (sh < she) *sh++ = '-';
                last = c;
                at++;
                break;
            }
            if (sh < she) *sh++ = s[w0 + k];
        }
        (void)nv;
    }
    *d = 0;
    *sh = 0;
    n = parse_say(say, mine[b], NSYL);
    if (!n)
        n = parse_say("L AA", mine[b], 1);       /* nothing written: it sings LA */
    nmine[b] = (uint8_t)n;
    mcur = (uint8_t)b;
    mine_gen++;
}

static APART void parse(void)
{
    int p;
    for (p = 0; p < NPHRASE; p++)
        nsyl[p] = (uint8_t)parse_say(PHRASE[p].say, syl[p], NSYL);
    build_mine();
}

/* ---- the mouth -------------------------------------------------------------------------- */
#define NQ 24                    /* phones queued */
#define NB 16                    /* ROBOT's bands */
typedef struct Res { f32 a, b, c, y1, y2; } Res;

static struct {
    int algo, me;
    int phrase, at;              /* the phrase, and the syllable the next key sings */
    int gen;                     /* your words as last seen (mine_gen) */
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

/* a key went down: a syllable starts */
static void sing(const Syl *y, f32 sr)
{
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
    M.since_on = 0;
    start_phone(0, sr);
    if (PH[M.q[0]].kind == K_VOW || PH[M.q[0]].kind == K_SEMI || PH[M.q[0]].kind == K_NAS)
        for (i = 0; i < 3; i++)                  /* a syllable that starts open starts there, not on a glide */
            M.f[i] = M.tf[i];
}

static void syllable(f32 sr)                     /* the next of the phrase */
{
    int n = imax(1, nsyls(M.phrase));
    M.shown = M.at % n;
    M.at = (M.at + 1) % n;
    sing(&syls(M.phrase)[M.shown], sr);
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
    i = iclamp((int)(b->k[0] * (f32)NWORD * 0.999f), 0, NWORD - 1);
    if (i != M.phrase || (!i && M.gen != mine_gen)) {   /* a new phrase (or your words written anew): from the top */
        M.phrase = i;
        M.gen = mine_gen;
        M.at = 0;
        M.shown = -1;
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
        if (blab_writing && want_letter >= 0) {  /* WRITE: the key's letter, by name */
            const char *s = LETTER;
            Syl t;
            for (i = want_letter; i > 0; s++)
                i -= *s == '|';
            want_letter = -1;
            parse_say(s, &t, 1);
            sing(&t, sr);
        } else if (M.since_on > 2 || M.shown < 0 || M.qn == 0)   /* (notes of a chord sing one syllable) */
            syllable(sr);
    }
    if (gate)
        M.quiet = 0;
    else if (++M.quiet > (uint32_t)(2.5f * sr / (f32)CTRL) && M.at && !blab_writing) {
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
 * A head that sings. Its mouth is the one the voice is shaped by: the first formant opens it, the second
 * spreads it (EE) or rounds it (OO), M, B and P shut it, F and V bite the lip, and the hiss shows the
 * teeth. Every voice is its own head; the words run along the bottom, the syllable sounding large above
 * them. With nothing played the head breathes and its mouth waits on the next vowel. WRITE: the head
 * goes up, and the keys are drawn with their letters.
 *
 *   K1 WORDS   the words along the bottom
 *   K2 MOUTH   a giant's head to a chipmunk's
 *   K3 DRAWL   the eyelids droop, the mouth slurs
 *   K4 AIR     breath out of the mouth */
#define NPUFF 18
static struct {
    f32 open, wide, hiss, shut, bite;            /* the mouth, eased */
    f32 pop, popv, blink, wob, tflash;
    f32 mx, my;                                  /* where the mouth is (for the breath) */
    f32 puff[NPUFF][4];                          /* breath: x, y, age (0: free), size */
    f32 spawn;
    int seen, inited;
} P;
static int8_t typed_key = -1;                    /* WRITE: the key last typed, lit a moment */

static f32 bump(f32 hz, f32 sc)                  /* how much of the mouth's sound is near hz */
{
    f32 g = 0.0f;
    int j;
    for (j = 0; j < 3; j++) {
        f32 d = (lg2(hz) - lg2(maxf(100.0f, M.f[j] * sc))) * 2.6f;
        g += (j == 0 ? 1.0f : j == 1 ? 0.8f : 0.55f) * exp2fx(-d * d);
    }
    return g;
}

static void mouth(f32 cx, f32 cy, f32 w, f32 h, f32 a, f32 k)
{
    f32 mw = w * (0.55f + 0.5f * P.wide) * (1.0f - 0.2f * P.bite);
    f32 mh = h * (0.07f + P.open * k) * (1.25f - 0.5f * P.wide) * (1.0f - P.shut);
    P.mx = cx; P.my = cy;
    if (mh < 1.8f) {                             /* shut: the lips pressed */
        g_line(cx - mw, cy, cx + mw, cy, 2.6f + P.shut, a);
        return;
    }
    g_mode(G_PAINT);
    g_ellipse(cx, cy, mw, mh, 0.0f, 0.0f);       /* (the mouth is dark inside) */
    g_mode(G_MAX);
    if (P.open > 0.3f)                           /* the tongue */
        g_ellipse(cx, cy + mh * 0.55f, mw * 0.45f, mh * 0.32f, 0.0f, a * 0.35f);
    if (P.hiss > 0.06f || P.open > 0.25f)        /* the teeth */
        g_rect(cx - mw * 0.5f, cy - mh + 1.5f, mw, minf(mh * 0.45f, 2.0f + 5.0f * P.hiss), a * (0.3f + 0.6f * P.hiss));
    if (P.bite > 0.1f)                           /* F, V: the lip under the teeth */
        g_line(cx - mw * 0.6f, cy - mh * 0.15f, cx + mw * 0.6f, cy - mh * 0.15f, 2.2f, a * P.bite);
    g_ellipse(cx, cy, mw, mh, 2.6f, a);
}

static void eye(f32 x, f32 y, f32 r, f32 lid, f32 look, int pupil)
{
    f32 ry = r * (1.0f - lid);
    if (ry < 1.4f) {
        g_line(x - r, y, x + r, y, 2.0f, 1.0f);
        return;
    }
    g_ellipse(x, y, r, ry, 2.0f, 1.0f);
    if (pupil)
        g_disc(x + r * 0.2f * sin_turns(P.wob * 0.05f), y + look * ry * 0.4f, r * 0.42f, 1.0f);
    g_line(x - r * 1.05f, y - ry, x + r * 1.05f, y - ry, 1.6f, lid * 0.9f);   /* (the lid) */
}

static void lash_eye(f32 x, f32 y, f32 r, f32 a)  /* shut, singing: a curve, the lashes under it */
{
    int i;
    g_arc(x, y, r, 0.0f, 0.5f, 2.0f, a);
    for (i = 0; i < 3; i++) {
        f32 u = 0.12f + 0.13f * (f32)i, c = cos_turns(u), s = sin_turns(u);
        g_line(x + c * r, y + s * r, x + c * r * 1.45f, y + s * r * 1.45f, 1.4f, a);
    }
}

static void quad_poly(f32 *q, int n, f32 fill)   /* a filled shape (convex) with its edge */
{
    f32 f[16];
    int i;
    for (i = 0; i < 8 && n > 8; i++) {           /* (filled through eight of its points: the edge covers the rest) */
        f[2 * i] = q[2 * (i * (n - 1) / 7)];
        f[2 * i + 1] = q[2 * (i * (n - 1) / 7) + 1];
    }
    g_fill(n > 8 ? f : q, n > 8 ? 8 : n, fill);
    g_poly(q, n, 1, 2.5f, 1.0f);
}

static SMALL void head(int style, f32 cx, f32 cy, f32 r, f32 lid, f32 on, f32 t, f32 look, f32 sc)
{
    f32 q[40];
    int i, j;
    switch (style) {
    case 0: {                                    /* ROBOT: a box, and its mouth is the vocoder's bands */
        f32 w = r * 0.95f, h = r * 0.82f, c = r * 0.2f, mx0 = cx - r * 0.62f, my = cy + r * 0.42f, hm = r * 0.26f;
        static const int8_t B[16] = {-1, -2, 1, -2, 2, -1, 2, 1, 1, 2, -1, 2, -2, 1, -2, -1};   /* (2: the side, 1: cut in) */
        for (i = 0; i < 8; i++) {
            q[2 * i] = cx + (f32)B[2 * i] * (B[2 * i] * B[2 * i] > 1 ? w * 0.5f : (w - c));
            q[2 * i + 1] = cy + (f32)B[2 * i + 1] * (B[2 * i + 1] * B[2 * i + 1] > 1 ? h * 0.5f : (h - c));
        }
        quad_poly(q, 8, 0.1f);
        g_line(cx, cy - h, cx, cy - h - r * 0.3f, 2.0f, 1.0f);
        g_glow(cx, cy - h - r * 0.34f, 6.0f + 12.0f * on, 0.4f + 0.6f * on);
        g_disc(cx, cy - h - r * 0.34f, 3.5f, 1.0f);
        g_rect(cx - w - 7.0f, cy - r * 0.22f, 6.0f, r * 0.44f, 0.6f);
        g_rect(cx + w + 1.0f, cy - r * 0.22f, 6.0f, r * 0.44f, 0.6f);
        for (i = -1; i <= 1; i += 2) {
            f32 ex = cx + (f32)i * r * 0.42f, ey = cy - r * 0.3f, s = r * 0.13f * (1.0f - lid) + 1.0f;
            g_rect(ex - r * 0.19f, ey - s, r * 0.38f, 2.0f * s, 0.9f);
            g_mode(G_PAINT);                     /* a scan going across each */
            g_rect(ex - r * 0.19f + fractf(t * 0.6f) * r * 0.32f, ey - s, 2.0f, 2.0f * s, 0.0f);
            g_mode(G_MAX);
        }
        g_mode(G_PAINT);
        g_rect(mx0 - 4.0f, my - hm - 4.0f, r * 1.24f + 8.0f, 2.0f * hm + 8.0f, 0.0f);
        g_mode(G_MAX);
        q[0] = mx0 - 4.0f; q[1] = my - hm - 4.0f; q[2] = mx0 + r * 1.24f + 4.0f; q[3] = q[1];
        q[4] = q[2]; q[5] = my + hm + 4.0f; q[6] = q[0]; q[7] = q[5];
        g_poly(q, 4, 1, 1.5f, 0.6f);
        P.mx = cx; P.my = my;
        for (j = 0; j < 12; j++) {
            f32 hz = 200.0f * exp2fx((f32)j * 0.42f), g, hh;
            g = (bump(hz, sc) * M.av + (M.af + M.ah) * (j > 7 ? 0.7f : 0.1f)) * on;
            hh = hm * (0.07f + 0.93f * sat(g * (1.0f - P.shut))) + 0.04f * hm * sin_turns(t * 0.3f + (f32)j * 0.08f);
            g_rect(mx0 + (f32)j * r * 0.105f, my - hh, r * 0.07f, 2.0f * hh, 1.0f);
        }
        break;
    }
    case 1:                                      /* CHOIR: three heads in a row, the middle one in front */
        for (i = 0; i < 3; i++) {
            int o = i == 2 ? 0 : i ? 1 : -1;
            f32 rr = r * (i == 2 ? 0.72f : 0.55f), x = cx + (f32)o * r * 1.05f;
            f32 y = cy + (i == 2 ? 0.0f : r * 0.3f) + 2.5f * sin_turns(t * 0.45f + (f32)i * 0.33f);
            g_mode(G_PAINT);
            g_disc(x, y, rr + 3.0f, 0.0f);
            g_disc(x, y, rr, 0.1f);
            g_mode(G_MAX);
            g_ring(x, y, rr, 2.4f, 1.0f);
            g_arc(x, y, rr * 0.78f, 0.6f, 0.9f, 2.0f, 0.55f);
            for (j = -1; j <= 1; j += 2) {
                g_arc(x + (f32)j * rr * 0.38f, y - rr * 0.08f, rr * 0.15f, 0.5f, 1.0f, 2.0f, 1.0f);
                g_disc(x + (f32)j * rr * 0.56f, y + rr * 0.18f, rr * 0.1f, 0.25f);
            }
            for (j = 0; j < 7; j++)
                g_ring(x + ((f32)j - 3.0f) * rr * 0.28f, y + rr * 1.06f, rr * 0.16f, 1.8f, 0.7f);
            mouth(x, y + rr * 0.45f, rr * 0.3f, rr * 0.4f, 1.0f, 0.8f + 0.2f * sin_turns(t * 0.7f + (f32)i * 0.4f));
        }
        P.mx = cx;
        P.my = cy + r * 0.28f;
        break;
    case 2:                                      /* TALK: a face, front on */
        g_ellipse(cx, cy, r * 0.8f, r, 0.0f, 0.08f);
        g_ellipse(cx, cy, r * 0.8f, r, 2.5f, 1.0f);
        g_arc(cx, cy + r * 0.05f, r * 0.82f, 0.6f, 0.9f, 3.0f, 1.0f);
        g_arc(cx + r * 0.15f, cy - r * 1.02f, r * 0.18f, 0.25f, 0.75f, 2.0f, 1.0f);
        for (i = -1; i <= 1; i += 2) {
            g_arc(cx + (f32)i * r * 0.32f, cy - r * 0.2f - on * r * 0.1f, r * 0.24f, 0.6f, 0.9f, 2.2f, 1.0f);
            eye(cx + (f32)i * r * 0.32f, cy - r * 0.2f, r * 0.15f, lid, look, 1);
            g_arc(cx + (f32)i * r * 0.8f, cy, r * 0.12f, i > 0 ? 0.75f : 0.25f, i > 0 ? 1.25f : 0.75f, 2.0f, 1.0f);
        }
        g_line(cx, cy - r * 0.1f, cx - r * 0.08f, cy + r * 0.14f, 2.0f, 0.6f);
        g_line(cx - r * 0.08f, cy + r * 0.14f, cx + r * 0.05f, cy + r * 0.15f, 2.0f, 0.6f);
        mouth(cx, cy + r * 0.48f, r * 0.34f, r * 0.3f, 1.0f, 1.0f);
        break;
    case 3: {                                    /* WHISP: a ghost, its tail torn */
        f32 x = cx + 6.0f * sin_turns(t * 0.21f), y = cy + 4.0f * sin_turns(t * 0.33f);
        g_glow(x, y, r * 1.7f, 0.22f + 0.2f * on);
        g_disc(x, y - r * 0.15f, r * 0.8f, 0.24f);
        g_rect(x - r * 0.8f, y - r * 0.15f, r * 1.6f, r * 0.8f, 0.24f);
        g_arc(x, y - r * 0.15f, r * 0.8f, 0.5f, 1.0f, 2.2f, 0.8f);
        g_line(x - r * 0.8f, y - r * 0.15f, x - r * 0.8f, y + r * 0.7f, 2.2f, 0.8f);
        g_line(x + r * 0.8f, y - r * 0.15f, x + r * 0.8f, y + r * 0.7f, 2.2f, 0.8f);
        for (i = 0; i < 4; i++) {                /* the hem, waving */
            f32 hx = x - r * 0.6f + (f32)i * r * 0.4f, hy = y + r * 0.66f + 3.0f * sin_turns(t * 0.5f + (f32)i * 0.25f);
            g_disc(hx, hy, r * 0.2f, 0.24f);
            g_arc(hx, hy, r * 0.2f, 0.0f, 0.5f, 2.2f, 0.8f);
        }
        g_mode(G_PAINT);
        for (i = -1; i <= 1; i += 2)
            g_ellipse(x + (f32)i * r * 0.3f, y - r * 0.22f, r * 0.14f, r * 0.2f * (1.0f - 0.7f * lid) + 1.0f, 0.0f, 0.0f);
        g_mode(G_MAX);
        mouth(x, y + r * 0.35f, r * 0.26f, r * 0.34f, 0.9f, 1.0f);
        break;
    }
    case 4: {                                    /* CHIP: a head of pixels */
        f32 b = (f32)(int)(r * 0.15f + 0.5f), x0 = (f32)(int)((cx - 6.0f * b) / b) * b, y0 = (f32)(int)((cy - 6.0f * b) / b) * b;
        int mw = 2 + (int)(4.0f * P.wide + 0.5f), mh = (int)(0.5f + 3.5f * P.open * (1.0f - P.shut));
        g_rect(x0, y0, 12.0f * b, 12.0f * b, 0.08f);
        for (i = 0; i < 12; i++) {
            g_rect(x0 + (f32)i * b, y0, b - 1.0f, b - 1.0f, 1.0f);
            g_rect(x0 + (f32)i * b, y0 + 11.0f * b, b - 1.0f, b - 1.0f, 1.0f);
            g_rect(x0, y0 + (f32)i * b, b - 1.0f, b - 1.0f, 1.0f);
            g_rect(x0 + 11.0f * b, y0 + (f32)i * b, b - 1.0f, b - 1.0f, 1.0f);
        }
        for (i = 0; i < 2; i++)
            g_rect(x0 + (f32)(3 + 4 * i) * b, y0 + (lid > 0.5f ? 4.0f : 3.0f) * b, 2.0f * b - 1.0f, (lid > 0.5f ? 1.0f : 2.0f) * b - 1.0f, 1.0f);
        g_rect(x0 + 6.0f * b, y0 - b, b - 1.0f, b - 1.0f, 0.4f + 0.6f * on);
        g_rect(x0 + 6.0f * b, y0 - 2.0f * b, b - 1.0f, b - 1.0f, on);
        for (i = 0; i < mw; i++)
            for (j = 0; j <= mh; j++)
                if (!mh || j == 0 || j == mh || i == 0 || i == mw - 1)
                    g_rect(x0 + (f32)(6 - (mw + 1) / 2 + i) * b, y0 + (f32)(8 - mh / 2 + j) * b, b - 1.0f, b - 1.0f, 1.0f);
        P.mx = x0 + 6.0f * b;
        P.my = y0 + 8.0f * b;
        break;
    }
    case 5:                                      /* MONK: a hood, a face in its shadow, the drone in rings */
        for (i = 0; i <= 8; i++) {               /* (a dome, a little pointed) */
            f32 u = (f32)i * (0.25f / 8.0f);
            q[2 * i] = cx - r * sin_turns(u);
            q[2 * i + 1] = cy - r * 1.2f + r * 2.15f * (1.0f - cos_turns(u)) - (i ? 0.0f : r * 0.12f);
            q[34 - 2 * i] = cx + r * sin_turns(u);
            q[35 - 2 * i] = q[2 * i + 1];
        }
        quad_poly(q, 17, 0.16f);
        g_mode(G_PAINT);
        g_ellipse(cx, cy + r * 0.12f, r * 0.55f, r * 0.7f, 0.0f, 0.0f);
        g_mode(G_MAX);
        g_ellipse(cx, cy + r * 0.12f, r * 0.55f, r * 0.7f, 1.8f, 0.5f);
        g_glow(cx, cy + r * 0.42f, r * 0.5f, 0.3f * on);
        for (i = -1; i <= 1; i += 2)
            g_arc(cx + (f32)i * r * 0.22f, cy - r * 0.05f, r * 0.11f, 0.0f, 0.5f, 1.8f, 0.55f);
        mouth(cx, cy + r * 0.42f, r * 0.22f, r * 0.26f, 0.95f, 1.0f);
        for (i = 0; i < 3; i++) {
            f32 ph = fractf(t * 0.4f + (f32)i * 0.333f);
            g_ring(cx, cy + r * 0.42f, r * 0.2f + ph * r * 1.4f, 1.5f, (1.0f - ph) * on * 0.8f);
        }
        break;
    case 6: {                                    /* DIVA: the hair, the lashes, the vibrato in her head */
        f32 x = cx + 3.0f * on * sin_turns(P.wob);
        for (i = 0; i < 9; i++) {
            f32 u = 0.5f + (f32)i * 0.0625f, hx = x + cos_turns(u) * r * 0.92f, hy = cy - r * 0.12f + sin_turns(u) * r * 0.95f;
            g_disc(hx, hy, r * 0.27f, 0.16f);
            g_ring(hx, hy, r * 0.27f, 1.8f, 0.8f);
        }
        g_ring(x, cy - r * 1.18f, r * 0.26f, 2.2f, 1.0f);
        g_mode(G_PAINT);
        g_ellipse(x, cy, r * 0.72f, r * 0.9f, 0.0f, 0.1f);
        g_mode(G_MAX);
        g_ellipse(x, cy, r * 0.72f, r * 0.9f, 2.4f, 1.0f);
        for (i = -1; i <= 1; i += 2) {
            f32 ex = x + (f32)i * r * 0.3f, ey = cy - r * 0.15f, sw = 4.0f * sin_turns(P.wob * 0.5f + (f32)i * 0.1f);
            if (on > 0.1f)
                lash_eye(ex, ey, r * 0.14f, 1.0f);
            else
                eye(ex, ey, r * 0.14f, lid, look, 1);
            g_arc(ex, ey - r * 0.04f, r * 0.2f, 0.6f, 0.9f, 1.8f, 0.9f);
            g_line(x + (f32)i * r * 0.72f, cy + r * 0.1f, x + (f32)i * r * 0.72f + sw, cy + r * 0.32f, 1.5f, 0.8f);
            g_disc(x + (f32)i * r * 0.72f + sw, cy + r * 0.36f, 3.5f, 1.0f);
        }
        g_disc(x + r * 0.32f, cy + r * 0.28f, 2.0f, 1.0f);
        mouth(x, cy + r * 0.45f, r * 0.36f, r * 0.42f, 1.0f, 1.0f);
        for (i = 0; i < 3; i++) {                /* and notes going up */
            f32 ph = fractf(t * 0.22f + (f32)i * 0.333f), nx = x + r * (0.95f + 0.25f * sin_turns(ph + (f32)i * 0.3f)) * (i == 1 ? -1.0f : 1.0f);
            f32 ny = cy + r * 0.2f - ph * r * 1.5f, a = sin_turns(ph * 0.5f) * on;
            g_disc(nx, ny, 3.4f, a);
            g_line(nx + 3.0f, ny, nx + 3.0f, ny - 13.0f, 1.6f, a);
            g_line(nx + 3.0f, ny - 13.0f, nx + 8.0f, ny - 9.0f, 1.6f, a);
        }
        break;
    }
    default:                                     /* ALIEN: a great head, eyes like seeds, the voice rung in halos */
        for (i = 0; i < 20; i++) {
            f32 u = (f32)i * 0.05f, s = sin_turns(u), wf = s < 0.0f ? 0.72f - 0.38f * s : 0.72f - 0.4f * s;
            q[2 * i] = cx + cos_turns(u) * r * wf;
            q[2 * i + 1] = cy + s * r * 1.02f;
        }
        quad_poly(q, 20, 0.12f);
        for (i = -1; i <= 1; i += 2) {
            f32 ex = cx + (f32)i * r * 0.32f, ey = cy - r * 0.02f, rot = (f32)i * -0.07f;
            for (j = 0; j < 8; j++) {
                f32 u = (f32)j * 0.125f, px = cos_turns(u) * r * 0.27f, py = sin_turns(u) * r * 0.13f * (1.0f - 0.8f * lid);
                q[2 * j] = ex + px * cos_turns(rot) - py * sin_turns(rot);
                q[2 * j + 1] = ey + px * sin_turns(rot) + py * cos_turns(rot);
            }
            g_fill(q, 8, 1.0f);
            g_mode(G_PAINT);
            g_disc(ex + (f32)i * r * 0.07f, ey - r * 0.03f + look * r * 0.03f, r * 0.04f, 0.0f);
            g_mode(G_MAX);
            g_line(cx + (f32)i * r * 0.28f, cy - r * 0.95f, cx + (f32)i * r * 0.55f, cy - r * 1.35f, 1.8f, 1.0f);
            g_glow(cx + (f32)i * r * 0.55f, cy - r * 1.35f, 4.0f + 10.0f * on, 0.5f + 0.5f * on);
        }
        mouth(cx, cy + r * 0.62f, r * 0.12f * (1.0f + 0.35f * sin_turns(M.vib * 9.0f)), r * 0.16f, 1.0f, 1.0f);
        for (i = 0; i < 2; i++) {
            f32 ph = fractf(t * 0.5f + (f32)i * 0.5f);
            g_ellipse(cx, cy - r * 1.15f - ph * r * 0.2f, r * (0.4f + 0.5f * ph), r * (0.08f + 0.08f * ph), 1.5f, (1.0f - ph) * on * 0.8f);
        }
        break;
    }
}

static SMALL void blab_draw(const Vis *vis)
{
    f32 sc = M.scale * (M.algo == 5 ? 0.86f : M.algo == 6 ? 1.16f : M.algo == 7 ? 1.45f : 1.0f);
    f32 lev = sat(vis->level * 4.0f), on = sat(lev * 3.0f), t = vis->t, dt = vis->dt, air = vis->k[3];
    f32 fade_in = absf(2.0f * vis->algo_t - 1.0f), k, r, cx = 120.0f, cy, lid, look;
    int i, ph = M.phrase, cur = M.shown, style = vis->algo_t < 0.5f ? vis->algo_prev : vis->algo, n = imax(1, nsyls(ph));
    int wr = blab_writing;
    const Syl *nx = &syls(ph)[M.at % n];
    if (!P.inited || vis->fresh) {
        uint8_t *z = (uint8_t *)&P;
        for (i = 0; i < (int)sizeof P; i++) z[i] = 0;
        P.inited = 1;
        P.seen = cur;
    }
    if (cur != P.seen) {                         /* a new syllable: the head nods */
        P.seen = cur;
        P.popv += 3.0f;
    }
    spring_z(&P.pop, &P.popv, 0.0f, 16.0f, 0.3f, dt);
    P.wob += dt * 5.6f;
    P.tflash += dt;

    /* the mouth: where the voice is, or with nothing sounding, the next vowel, barely open */
    {
        int q = M.qi < M.qn ? M.q[M.qi] : -1;
        const Ph *v = &PH[nx->ph[nx->nuc]];
        f32 o, w, sh = 0.0f, bi = 0.0f;
        if (on > 0.03f) {
            o = sat((M.f[0] - 230.0f) * (1.0f / 520.0f)) * sat(M.av * 1.4f) * on;
            w = sat((M.f[1] - 850.0f) * (1.0f / 1250.0f));
            sh = q == M_ || q == CL || q == VCL || q == PB ? 1.0f : 0.0f;
            bi = q == F_ || q == V_ ? 1.0f : 0.0f;
        } else {
            o = 0.05f + 0.04f * sin_turns(t * 0.3f);
            w = sat(((f32)v->f2 * 20.0f - 850.0f) * (1.0f / 1250.0f));
        }
        k = 1.0f - exp2fx(-dt * (16.0f + 30.0f * (1.0f - vis->k[2])));
        P.open += (o - P.open) * k;
        P.wide += (w - P.wide) * k;
        P.shut += (sh - P.shut) * minf(1.0f, k * 2.0f);
        P.bite += (bi - P.bite) * k;
        P.hiss += (sat(M.af * 1.3f) * on - P.hiss) * k;
    }
    P.blink -= dt;
    if (P.blink < -0.16f)
        P.blink = 2.0f + 3.0f * rng_f(vis->rng);
    lid = maxf(P.blink < 0.0f ? 1.0f - absf(P.blink + 0.08f) * 12.0f : 0.0f, vis->k[2] * 0.55f);
    look = clampf((60.0f - vis->hand_pitch) * (1.0f / 18.0f), -1.0f, 1.0f);

    g_clear();
    g_mode(G_MAX);
    g_dim(0.35f + 0.65f * fade_in);
    r = 66.0f - 20.0f * vis->k[1];               /* MOUTH: a giant's head to a chipmunk's */
    cy = 84.0f + 2.0f * sin_turns(t * (0.28f + 0.2f * vis->k[1])) - 5.0f * P.pop;
    if (wr) {
        r *= 0.55f;
        cy = 58.0f;
    }
    head(style, cx, cy, r * (1.0f + 0.03f * P.pop), lid, on, t, look, sc);

    /* breath: out of the mouth with the hiss, and with AIR always */
    P.spawn += dt * (P.hiss * 26.0f + air * air * 7.0f + (style == 3 ? 5.0f * on : 0.0f));
    for (i = 0; i < NPUFF; i++) {
        f32 *p = P.puff[i];
        if (p[2] <= 0.0f && P.spawn >= 1.0f) {
            P.spawn -= 1.0f;
            p[0] = P.mx + rng_s(vis->rng) * 4.0f;
            p[1] = P.my;
            p[2] = 0.001f;
            p[3] = 2.0f + 3.0f * rng_f(vis->rng);
        }
        if (p[2] > 0.0f) {
            f32 a = (1.0f - p[2]) * (0.3f + 0.5f * air);
            p[2] += dt * 0.9f;
            p[0] += dt * 22.0f * sin_turns(p[3] + p[2]);
            p[1] -= dt * 30.0f;
            if (p[2] >= 1.0f) p[2] = 0.0f;
            else if (style == 3) g_glow(p[0], p[1], p[3] * 3.0f + p[2] * 10.0f, a);
            else g_ring(p[0], p[1], p[3] + p[2] * 9.0f, 1.3f, a);
        }
    }
    if (P.spawn > 1.0f) P.spawn = 1.0f;
    g_dim(1.0f);

    if (wr) {
        /* WRITE: what is written, and the keys with their letters */
        char ch[2] = {0, 0};
        f32 size = 16.0f, w = g_text_w(st.words, size), x;
        int wi = 0, key;
        if (w > 224.0f) { size *= 224.0f / w; w = 224.0f; }
        if (st.words[0])
            g_text(120.0f, 116.0f, st.words, size, 2.0f, 1.0f, 1);
        else
            g_text(120.0f, 118.0f, "PLAY A KEY: A TO Z", 11.0f, 1.4f, 0.4f + 0.2f * sin_turns(t * 0.7f), 1);
        if (fractf(t * 1.6f) < 0.6f)
            g_rect(120.0f + 0.5f * w + 2.0f, 114.0f, 2.0f, size + 4.0f, 1.0f);
        for (key = 0; key < 27; key++) {         /* white keys first, then the black over them */
            int nn = (53 + key) % 12;
            f32 lit = key == typed_key ? sat(1.0f - P.tflash * 2.5f) : 0.0f;
            if (nn == 1 || nn == 3 || nn == 6 || nn == 8 || nn == 10) continue;
            x = (f32)wi * 15.0f;
            ch[0] = key == 26 ? '_' : (char)('A' + key);
            g_rect(x + 1.0f, 150.0f, 13.0f, 88.0f, 0.12f + 0.85f * lit);
            g_mode(lit > 0.5f ? G_PAINT : G_MAX);
            g_text(x + 7.5f, 222.0f, ch, 10.0f, 1.5f, lit > 0.5f ? 0.0f : 1.0f, 1);
            g_mode(G_MAX);
            wi++;
        }
        for (key = 0, wi = 0; key < 27; key++) {
            int nn = (53 + key) % 12;
            f32 lit = key == typed_key ? sat(1.0f - P.tflash * 2.5f) : 0.0f;
            if (!(nn == 1 || nn == 3 || nn == 6 || nn == 8 || nn == 10)) { wi++; continue; }
            x = (f32)wi * 15.0f - 5.0f;
            ch[0] = (char)('A' + key);
            g_mode(G_PAINT);
            g_rect(x - 1.0f, 150.0f, 12.0f, 52.0f, 0.0f);
            g_mode(G_MAX);
            g_rect(x, 150.0f, 10.0f, 51.0f, 0.3f + 0.65f * lit);
            g_text(x + 5.0f, 188.0f, ch, 8.5f, 1.3f, 1.0f, 1);
        }
        return;
    }

    /* the words, along the bottom: the phrase, its syllables lit as far as it has sung */
    {
        const char *s = ph || st.words[0] ? shows(ph) : "EDIT: WRITE YOUR OWN";
        char seg[24];
        f32 size = 12.0f, tw = g_text_w(s, size), x, big;
        int sy, e, m, sounding = cur >= 0 && lev > 0.02f, want = sounding ? cur : M.at % n;
        if (tw > 228.0f) size *= 228.0f / tw;
        x = 120.0f - 0.5f * g_text_w(s, size);
        for (i = 0, sy = 0; s[i];) {
            f32 a;
            for (e = i; s[e] && s[e] != '-' && s[e] != ' '; e++)
                ;
            for (m = 0; m < e - i && m < 23; m++) seg[m] = s[i + m];
            seg[m] = 0;
            a = !ph && !st.words[0] ? 0.35f + 0.25f * sin_turns(t * 0.6f) : sy == cur && sounding ? 1.0f : cur >= 0 && sy < cur ? 0.55f : 0.25f;
            g_text(x, 218.0f, seg, size, 1.4f, a, 0);
            x += g_text_w(seg, size);
            if (s[e] == ' ') x += g_text_w(" ", size);
            if (s[e] == '-') x += g_text_w("-", size) * 0.4f;
            sy++;
            i = s[e] ? e + 1 : e;
        }
        /* the syllable sounding, or the one the next key will sing, large above them */
        for (i = 0, sy = 0, m = 0; s[i]; i++) {
            if (s[i] == '-' || s[i] == ' ') { sy++; continue; }
            if (sy == want && m < 23) seg[m++] = s[i];
        }
        seg[m] = 0;
        if (!ph && !st.words[0]) { seg[0] = 'L'; seg[1] = 'A'; seg[2] = 0; }
        big = 30.0f * (1.0f + 0.12f * P.pop);
        tw = g_text_w(seg, big);
        if (tw > 224.0f) big *= 224.0f / tw;
        g_mode(G_PAINT);                         /* (a dark halo keeps it clear of the head) */
        g_text(120.0f, 172.0f - 0.5f * (big - 30.0f), seg, big, 9.0f, 0.0f, 1);
        g_mode(G_MAX);
        g_text(120.0f, 172.0f - 0.5f * (big - 30.0f), seg, big, 3.0f,
               sounding ? 0.6f + 0.4f * lev : 0.3f + 0.12f * sin_turns(t * 0.7f), 1);
    }
}

void blab_type(int key)
{
    int n = 0;
    while (n < NWORDS - 1 && st.words[n]) n++;
    if (key == -2)
        n = 0;
    else if (key == -1)
        n = n ? n - 1 : 0;
    else if (key >= 0 && n < NWORDS - 1) {
        char c = key >= 26 ? ' ' : (char)('A' + key);
        if (c != ' ' || (n && st.words[n - 1] != ' '))
            st.words[n++] = c;
        if (key < 26)
            want_letter = (int8_t)key;
        typed_key = (int8_t)key;
        P.tflash = 0.0f;
    }
    while (n < NWORDS)
        st.words[n++] = 0;
    build_mine();
}

const InstDef inst_blab = {
    "BLAB",
    {120, 255, 170},
    {"WORDS", "MOUTH", "DRAWL", "AIR"},
    {"ROBOT", "CHOIR", "TALK", "WHISP", "CHIP", "MONK", "DIVA", "ALIEN"},
    {{7, 50, 30, 15}, 0, {2, 50, 100, 30}, {0, 40, 0, 0}, {10, 30, 30, 60}, {0, 3, 1, 60}, {0, 0, 0}},
    0.36f,
    blab_block, blab_note, blab_render, blab_post, blab_draw,
};

#ifdef BLAB_TOOLS                                /* (the tuning tools: how many keys a phrase takes) */
int blab_syllables(int p) { if (!nsyl[0]) parse(); return p >= 0 && p < NPHRASE ? nsyl[p] : 0; }
const char *blab_mine(const char **show, int *n) { *show = mine_show[mcur]; *n = nmine[mcur]; return say; }
#endif
