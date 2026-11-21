/* h1 on the FM-1: what app.c makes of the keys' readings beyond fm1-base's own, kept apart so tests/keys.c can run
 * it on a computer. Platform-free; it needs fm1-base's fastmath.h (in scope in the unity build) included first. */
#pragma once
#include <stdint.h>

/* ---- a lean that can be held -----------------------------------------------------------------------
 * One key's reading made into a lean, 0..127. r is the pill's resistance in counts above the diode: it falls as
 * the key is pressed harder. The rest, what r is when the key is not leant on, settles after the landing for
 * `settle` ms and is then left where it is: it moves only when the key is eased off (r above it), and then up to
 * r. The lean is how far r is under the rest, as fm1-base counts it (log2 of the two, less `dead`, over `span`).
 * Every rate here is a rate in time, not in calls, so the reading is the same asked every millisecond or once in
 * a slow frame: the main loop asks between frames, and not while it draws one (23 to 83 ms). */
typedef struct Lean { float rest, s; uint32_t t0, at; uint8_t on, val; } Lean;

#define LEAN_SETTLE_K (-0.0830075f)      /* log2(0.75) / 5: a quarter of the way every 5 ms (fm1-base's 0.25) */
#define LEAN_EASE_K (-0.0537640f)        /* log2(0.83) / 5: fm1-base's 0.17 every 5 ms, about 27 ms */

static inline void lean_off(Lean *l)
{
    l->on = 0;
    l->s = 0.0f;
    l->val = 0;
}

static inline uint8_t lean_step(Lean *l, float r, uint32_t now, float dead, float span, uint32_t settle)
{
    uint32_t was, into;
    float dt, x;
    if (!l->on) {
        l->on = 1;
        l->rest = r;
        l->t0 = l->at = now;
        l->s = 0.0f;
    }
    was = l->at - l->t0;                 /* (ms since the landing, at the last reading) */
    dt = (float)(now - l->at > 1000u ? 1000u : now - l->at);
    l->at = now;
    into = was < settle ? settle - was : 0u;   /* of this step, the part still in the settling */
    if (r > l->rest)
        l->rest = r;                     /* eased off: this is where it rests now */
    else if (into)
        l->rest += (r - l->rest) * (1.0f - fm_exp2f(LEAN_SETTLE_K * (dt < (float)into ? dt : (float)into)));
    x = fm_clampf((fm_log2f(l->rest / r) - dead) / span, 0.0f, 1.0f);
    l->s += (x - l->s) * (1.0f - fm_exp2f(LEAN_EASE_K * dt));
    l->val = (uint8_t)(l->s * 127.0f + 0.5f);
    return l->val;
}

/* ---- a key whose release was lost --------------------------------------------------------------------
 * fm1-base gives a sensed key's presses and releases as events, sixteen at most between two reads of them, so in a
 * long frame of fast playing one can be lost, and a lost release leaves the note sounding. The plain scan of the
 * keys still sees every key. A sensed key the events hold down, that the scan has seen open for KEY_LOST_MS and
 * that is not seated, is taken as let go, and stays so until it is seated again (a new press). */
#define KEY_LOST_MS 50u
typedef struct KeyMend { uint32_t lost; uint32_t open_at[27]; uint32_t open; } KeyMend;

static inline uint32_t keys_mend(KeyMend *m, uint32_t keys, uint32_t sensed, uint32_t scan, uint32_t seated, uint32_t now)
{
    uint32_t k;
    for (k = 0; k < 27u; k++) {
        uint32_t bit = 1u << k;
        if (!(sensed & bit))
            continue;
        if (m->lost & bit) {
            if (seated & bit || !(keys & bit))
                m->lost &= ~bit;         /* pressed again, or the release came after all */
            else {
                keys &= ~bit;
                continue;
            }
        }
        if ((keys & bit) && !(scan & bit) && !(seated & bit)) {
            if (!(m->open & bit)) {
                m->open |= bit;
                m->open_at[k] = now;
            } else if (now - m->open_at[k] >= KEY_LOST_MS) {
                m->open &= ~bit;
                m->lost |= bit;
                keys &= ~bit;
            }
        } else
            m->open &= ~bit;
    }
    return keys;
}
