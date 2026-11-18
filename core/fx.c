/* H1 - the shared effects: TONE (one knob, low-pass below the middle and high-pass above),
 * ECHO (tempo-synced ping-pong, run at half rate so 1.5 s fits in 64 KB) and SPACE (a small
 * comb/all-pass reverb). The four knobs of the FX page are all there is. */
#include "internal.h"
#include "dsp.h"

#define ECHO_N 32768u
static int16_t echo_buf[ECHO_N] H1_POOL;
static uint32_t echo_w;
static f32 echo_t = 4000.0f, echo_lp, echo_ol, echo_or;
static f32 send_s, fb_s, wet_s, room_s;

#define NCOMB 4
#define NAP 2
static const uint16_t COMB_LEN[NCOMB] = {1116, 1188, 1277, 1356};
static const uint16_t AP_LEN[NAP] = {556, 441};
#define STEREO_SPREAD 23
#define REV_TOTAL ((1116 + 1188 + 1277 + 1356) * 2 + NCOMB * STEREO_SPREAD + (556 + 441) * 2 + NAP * STEREO_SPREAD)
static f32 rev_buf[REV_TOTAL] H1_POOL;
static f32 *comb_b[2][NCOMB], *ap_b[2][NAP];
static uint16_t comb_n[2][NCOMB], ap_n[2][NAP], comb_i[2][NCOMB], ap_i[2][NAP];
static f32 comb_f[2][NCOMB];

static f32 tone_s[2][2], tone_k = 1.0f;
static int tone_mode;
static f32 SRf = 44100.0f;

void fx_init(f32 sr)
{
    f32 *p = rev_buf;
    int c, i;
    SRf = sr;
    for (c = 0; c < 2; c++) {
        for (i = 0; i < NCOMB; i++) {
            comb_b[c][i] = p;
            comb_n[c][i] = (uint16_t)(COMB_LEN[i] + c * STEREO_SPREAD);
            p += comb_n[c][i];
        }
        for (i = 0; i < NAP; i++) {
            ap_b[c][i] = p;
            ap_n[c][i] = (uint16_t)(AP_LEN[i] + c * STEREO_SPREAD);
            p += ap_n[c][i];
        }
    }
}

static inline f32 echo_read(f32 delay)
{
    f32 pos = (f32)echo_w - delay, fr;
    int32_t i = ifloor(pos);
    f32 a, b;
    fr = pos - (f32)i;
    a = (f32)echo_buf[(uint32_t)i & (ECHO_N - 1u)];
    b = (f32)echo_buf[(uint32_t)(i + 1) & (ECHO_N - 1u)];
    return (a + (b - a) * fr) * (1.0f / 32768.0f);
}

void fx_block(f32 *l, f32 *r, const InstParams *ip, f32 bpm)
{
    static const f32 DIV[8] = {0.125f, 0.25f, 1.0f / 3.0f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f};   /* in beats */
    f32 x, send, fb, tgt, wet, room, t;
    int n, c, i;

    /* TONE */
    t = (f32)ip->fx[3] * 0.01f;
    if (t < 0.47f) {
        tone_mode = 1;
        tone_k = onepole_k(1.0f / (H1_TAU * 180.0f * exp2fx(t / 0.47f * 6.7f)), SRf);
    } else if (t > 0.53f) {
        tone_mode = 2;
        tone_k = onepole_k(1.0f / (H1_TAU * 20.0f * exp2fx((t - 0.53f) / 0.47f * 7.2f)), SRf);
    } else
        tone_mode = 0;
    if (tone_mode)
        for (n = 0; n < CTRL; n++) {
            f32 a = l[n], b = r[n];
            tone_s[0][0] += (a - tone_s[0][0]) * tone_k;
            tone_s[0][1] += (tone_s[0][0] - tone_s[0][1]) * tone_k;
            tone_s[1][0] += (b - tone_s[1][0]) * tone_k;
            tone_s[1][1] += (tone_s[1][0] - tone_s[1][1]) * tone_k;
            if (tone_mode == 1) {
                l[n] = tone_s[0][1];
                r[n] = tone_s[1][1];
            } else {
                l[n] = a - tone_s[0][1];
                r[n] = b - tone_s[1][1];
            }
        }
    else
        for (c = 0; c < 2; c++)
            tone_s[c][0] = tone_s[c][1] = (c ? r[CTRL - 1] : l[CTRL - 1]);

    /* ECHO */
    x = (f32)ip->fx[0] * 0.01f;
    send = x * 0.85f;
    fb = x > 0.0f ? 0.22f + 0.56f * x : 0.0f;
    tgt = DIV[ip->fx[1] & 7] * 60.0f / bpm * SRf * 0.5f;
    while (tgt > 15800.0f)
        tgt *= 0.5f;
    if (tgt < 24.0f)
        tgt = 24.0f;
    for (n = 0; n < CTRL; n += 2) {
        f32 in, tl, tr, w;
        send_s += (send - send_s) * 0.02f;
        fb_s += (fb - fb_s) * 0.02f;
        echo_t += (tgt - echo_t) * 0.0012f;
        in = (l[n] + r[n] + l[n + 1] + r[n + 1]) * 0.25f * send_s;
        tl = echo_read(echo_t);
        tr = echo_read(echo_t * 2.0f);
        echo_lp += (tr * fb_s - echo_lp) * 0.42f;
        w = softclip(in + echo_lp) * 32767.0f;
        echo_buf[echo_w & (ECHO_N - 1u)] = (int16_t)w;
        echo_w++;
        l[n] += (echo_ol + tl) * 0.5f;
        r[n] += (echo_or + tr) * 0.5f;
        l[n + 1] += tl;
        r[n + 1] += tr;
        echo_ol = tl;
        echo_or = tr;
    }

    /* SPACE */
    x = (f32)ip->fx[2] * 0.01f;
    wet = x * (0.25f + 0.55f * x);
    room = 0.74f + 0.235f * x;
    if (x <= 0.0f && wet_s < 0.0005f)
        return;
    for (n = 0; n < CTRL; n++) {
        f32 in, o[2];
        wet_s += (wet - wet_s) * 0.002f;
        room_s += (room - room_s) * 0.002f;
        in = (l[n] + r[n]) * 0.03f + 1e-18f;
        for (c = 0; c < 2; c++) {
            f32 acc = 0.0f;
            for (i = 0; i < NCOMB; i++) {
                f32 *b = comb_b[c][i];
                uint16_t k = comb_i[c][i];
                f32 y = b[k];
                comb_f[c][i] = y * 0.68f + comb_f[c][i] * 0.32f;
                b[k] = in + comb_f[c][i] * room_s;
                if (++k >= comb_n[c][i]) k = 0;
                comb_i[c][i] = k;
                acc += y;
            }
            for (i = 0; i < NAP; i++) {
                f32 *b = ap_b[c][i];
                uint16_t k = ap_i[c][i];
                f32 y = b[k];
                b[k] = acc + y * 0.5f;
                acc = y - acc;
                if (++k >= ap_n[c][i]) k = 0;
                ap_i[c][i] = k;
            }
            o[c] = acc;
        }
        l[n] += o[0] * wet_s * 2.2f;
        r[n] += o[1] * wet_s * 2.2f;
    }
}
