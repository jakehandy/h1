/* h1 - DSP bricks the instruments share. Header-only, single precision. */
#pragma once
#include "mathx.h"

#define SIN_N 2048
extern f32 sin_tab[SIN_N + 1];
void dsp_init(void);

/* sin(2*pi*t) from the table, t in turns, any value */
static inline f32 tsin(f32 t)
{
    f32 x;
    int32_t i;
    t -= (f32)ifloor(t);
    x = t * (f32)SIN_N;
    i = (int32_t)x;
    x -= (f32)i;
    i &= SIN_N - 1;                              /* (a t just under a whole number rounds up to SIN_N; and whatever
                                                  * a number that is not one turns into, it stays in the table) */
    return sin_tab[i] + (sin_tab[i + 1] - sin_tab[i]) * x;
}

/* MIDI note to Hz (fractional notes fine) */
static inline f32 mtof(f32 n) { return 440.0f * exp2fx((n - 69.0f) * (1.0f / 12.0f)); }

/* the polynomial band-limited step: subtract at a saw's wrap, add/subtract at a pulse's edges */
static inline f32 blep(f32 t, f32 dt)
{
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0f;
    }
    if (t > 1.0f - dt) {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}
static inline f32 saw_bl(f32 ph, f32 dt) { return 2.0f * ph - 1.0f - blep(ph, dt); }
static inline f32 pulse_bl(f32 ph, f32 dt, f32 duty)
{
    f32 p2 = ph + 1.0f - duty;
    if (p2 >= 1.0f) p2 -= 1.0f;
    return (ph < duty ? 1.0f : -1.0f) + blep(ph, dt) - blep(p2, dt) - (2.0f * duty - 1.0f);
}

/* a state-variable filter (trapezoidal): stable at any cutoff, clean under modulation */
typedef struct Svf { f32 g, k, a1, a2, a3; } Svf;
static inline void svf_set(Svf *f, f32 fc, f32 q, f32 isr)
{
    f32 x = fc * isr;
    if (x > 0.42f) x = 0.42f;
    if (x < 0.0002f) x = 0.0002f;
    f->g = tanfx(H1_PI * x);
    f->k = 1.0f / q;
    f->a1 = 1.0f / (1.0f + f->g * (f->g + f->k));
    f->a2 = f->g * f->a1;
    f->a3 = f->g * f->a2;
}
/* one sample; s = two state floats. lp is returned, *bp and *hp optional by the macro below */
#define SVF_TICK(f, s, x, lp, bp) do { \
        f32 v3_ = (x) - (s)[1]; \
        f32 v1_ = (f).a1 * (s)[0] + (f).a2 * v3_; \
        f32 v2_ = (s)[1] + (f).a2 * (s)[0] + (f).a3 * v3_; \
        (s)[0] = 2.0f * v1_ - (s)[0]; \
        (s)[1] = 2.0f * v2_ - (s)[1]; \
        (lp) = v2_; (bp) = v1_; \
    } while (0)

/* white noise from a 32-bit state */
static inline f32 noise_s(uint32_t *s) { return (f32)(int32_t)rng_next(s) * (1.0f / 2147483648.0f); }

/* a one-pole coefficient for a time constant in seconds, used once per control block */
static inline f32 onepole_k(f32 seconds, f32 rate)
{
    if (seconds <= 0.0f) return 1.0f;
    return 1.0f - exp2fx(-1.442695f / (seconds * rate));
}

/* Pressure as a push. A lean is a gesture: eased, it is gone, and a pad under a steady finger may drift back
 * by itself. A push makes of it a place that stays. *at is where it has been pushed to, 0..1, and is the
 * instrument's to keep (in a voice's s[], or its own), started wherever it likes at the note.
 *   push_hold   goes up with the lean and stays when the lean eases: the hardest lean so far.
 *   push_move   moves on while the lean is past dead (0..1), the faster the harder: speed is how far a full
 *               lean takes it in a second, dt the seconds since the last call. Easing leaves it where it is.
 * Neither comes back down by itself: set *at again to take it back. Both return it. */
static inline f32 push_hold(f32 *at, f32 lean)
{
    if (lean > *at)
        *at = lean < 1.0f ? lean : 1.0f;
    return *at;
}
static inline f32 push_move(f32 *at, f32 lean, f32 dead, f32 speed, f32 dt)
{
    f32 over = lean - dead;
    if (over > 0.0f && dead < 1.0f) {
        f32 x = *at + over / (1.0f - dead) * speed * dt;
        *at = x < 1.0f ? x : 1.0f;
    }
    return *at;
}
