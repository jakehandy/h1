/* What app.c makes of the keys' readings (app/keys.h), on a computer: the reading is fed as fm1-base's sensing
 * would give it, at the cadences the device asks at. Run by tests/run_tests.sh.
 *
 *   the lean     the same at a reading every millisecond, every 83 ms (a slow frame) and the device's mix of the
 *                two: a landing that goes on settling for a while is no lean, a lean held is held, eased is gone
 *   the mend     a key the events hold down but the scan has seen open for a while is let go, and comes back at
 *                its next press; a key held, or lifting for a moment, is left alone */
#include <stdio.h>
#include "../base/include/fastmath.h"
#include "../app/keys.h"

static int fails, checks;
#define CHECK(c, what) do { checks++; if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, what); } } while (0)

/* fm1-base's scale (lib/touch.c P_DEAD, P_SPAN at sensitivity 0, P_SETTLE) */
#define DEAD 0.04f
#define SPAN (0.8f * (1.0f - 0.85f * 0.5f))
#define SETTLE 250u

/* a key: lands at 240 counts and settles towards 150 over the first few hundred ms (a pill going on squashing),
 * is leant on from 1000 ms to 2500 ms (r down to 90: 0.74 of an octave), then eased back to its rest */
static float reading(uint32_t t)
{
    float r = 150.0f + 90.0f * fm_exp2f(-(float)t / 50.0f);
    if (t >= 1000u && t < 2500u)
        r = 150.0f - 60.0f * fm_clampf((float)(t - 1000u) / 100.0f, 0.0f, 1.0f);
    return r;
}

/* the lean at times 0..3000 ms, asked at the cadence `step` gives: v[t / 10] */
static void run(uint8_t *v, uint32_t (*step)(uint32_t))
{
    Lean l = {0};
    uint32_t t = 0, i;
    uint8_t now = 0;
    for (i = 0; i <= 300u; i++) {
        while (t <= i * 10u) {
            now = lean_step(&l, reading(t), t, DEAD, SPAN, SETTLE);
            t += step(t);
        }
        v[i] = now;
    }
}
static uint32_t every_ms(uint32_t t) { (void)t; return 1u; }
static uint32_t slow_frames(uint32_t t) { (void)t; return 83u; }
static uint32_t device(uint32_t t) { return t % 100u < 17u ? 1u : 83u; }   /* (1 ms between frames, 83 while one draws) */

int main(void)
{
    static uint8_t a[301], b[301], c[301];
    KeyMend m = {0};
    uint32_t keys, t, i, worst;

    run(a, every_ms);
    run(b, slow_frames);
    run(c, device);
    CHECK(a[90] < 4, "every ms: a landing that goes on settling is no lean");
    CHECK(b[90] < 4, "83 ms a reading: no lean either");
    CHECK(c[90] < 4, "the device's mix: no lean either");
    CHECK(a[200] > 100 && a[240] > 100, "every ms: a lean held is held");
    CHECK(b[200] > 100 && b[240] > 100, "83 ms a reading: held too");
    for (i = 120, worst = 0; i < 245; i++) {
        uint32_t d = a[i] > b[i] ? a[i] - b[i] : b[i] - a[i], e = a[i] > c[i] ? a[i] - c[i] : c[i] - a[i];
        if (d > worst) worst = d;
        if (e > worst) worst = e;
    }
    CHECK(worst <= 6, "and the same at every cadence, once it has eased in");
    CHECK(a[290] < 4 && b[290] < 4 && c[290] < 4, "eased off, it is gone");

    /* the mend: key 3 sensed; the events hold it, the scan has it open, it is not seated */
    keys = 0;
    for (t = 0; t < 40u; t++)
        keys = keys_mend(&m, 1u << 3, 1u << 3, 0, 0, t);
    CHECK(keys == 1u << 3, "a key open a moment is left alone (a release on its way)");
    for (; t < 60u; t++)
        keys = keys_mend(&m, 1u << 3, 1u << 3, 0, 0, t);
    CHECK(keys == 0, "open for 50 ms, it is let go");
    keys = keys_mend(&m, 1u << 3, 1u << 3, 1u << 3, 0, t++);
    CHECK(keys == 0, "and stays let go until it is seated");
    keys = keys_mend(&m, 1u << 3, 1u << 3, 1u << 3, 1u << 3, t++);
    CHECK(keys == 1u << 3, "seated again, it is down");
    for (i = 0; i < 200u; i++)
        keys = keys_mend(&m, 1u << 3, 1u << 3, 1u << 3, i & 1u ? 1u << 3 : 0, t++);
    CHECK(keys == 1u << 3, "held, and lifting a little while the scan has it closed: left alone");
    for (i = 0; i < 200u; i++)
        keys = keys_mend(&m, 1u << 20, 1u << 3, 0, 0, t++);
    CHECK(keys == 1u << 20, "a key that is not sensed (the top row) is the scan's own business");

    printf("keys: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
