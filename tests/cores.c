/* Two cores: h1_two_cores hands every other voice to a second core, and the sound must be the same
 * to the bit as on one. Here the second core is a real thread, so two voices' renders do run at the
 * same time. It prints a hash of every section of the same playing; tests/run_tests.sh runs it on
 * one core (no argument) and on two (argument 2) and compares. Any instrument whose render leans on
 * another voice, or on memory two voices share, comes out different. (Build it with -fsanitize=thread to see what two renders share: the
 * `sounding` flags of COZY and BARD, which both only ever set, are all there is.) */
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include "../core/h1.h"

/* ---- the second core: one job at a time, waited for ---- */
static void (*job_fn)(uint32_t);
static uint32_t job_arg;
static atomic_int job_state;                     /* 0 idle, 1 a job is out, 2 stop */
static unsigned long jobs;

static void *core2(void *x)
{
    (void)x;
    for (;;) {
        int s = atomic_load(&job_state);
        if (s == 2)
            return 0;
        if (s == 1) {
            job_fn(job_arg);
            atomic_store(&job_state, 0);
        } else
            sched_yield();
    }
}
static int run2(void (*fn)(uint32_t), uint32_t arg)
{
    job_fn = fn;
    job_arg = arg;
    jobs++;
    atomic_store(&job_state, 1);
    return 1;
}
static int wait2(void)
{
    while (atomic_load(&job_state) == 1)
        ;
    return 0;
}

/* ---- the same playing, hashed in sections ---- */
#define NSEC 160
static uint64_t sec[NSEC];
static int nsec;
static uint64_t h;
static uint32_t ms;

static void run(int blocks)
{
    static int32_t b[512];
    int i, k;
    for (k = 0; k < blocks; k++) {
        h1_render(b, 256);
        for (i = 0; i < 512; i++) {
            h ^= (uint32_t)b[i];
            h *= 1099511628211ull;
        }
        if (k % 3 == 2)
            h1_frame(ms += 17);
    }
}
static void mark(void)
{
    if (nsec < NSEC)
        sec[nsec++] = h;
    h = 1469598103934665603ull;
}
static void tap(int b) { h1_button(b, 1); run(6); h1_button(b, 0); run(6); }

static void play(void)
{
    static const int pat[16] = {0, 7, 12, 4, 14, 7, 19, 11, 2, 9, 16, 5, 12, 21, 7, 24};
    int ni, inst, a, f, k;
    nsec = 0;
    ms = 0;
    h1_init(44100);
    h = 1469598103934665603ull;
    ni = h1_instrument_count();
    /* every algorithm of every instrument: chords of four, the knobs moved, tails overlapping */
    for (inst = 0; inst < ni; inst++) {
        for (a = 0; a < 8; a++) {
            for (f = 0; f < 24; f++) {
                if (f % 4 == 0)
                    for (k = 0; k < 4; k++) {
                        h1_key((pat[(f / 4 + 15) & 15] + k * 5) % 27, 0);
                        h1_key((pat[(f / 4) & 15] + k * 5) % 27, 1);
                    }
                if (f == 8)
                    for (k = 0; k < 4; k++)
                        h1_midi(0xB0, (uint8_t)(20 + k), (uint8_t)(k & 1 ? 10 : 120));
                if (f == 16)
                    for (k = 0; k < 4; k++)
                        h1_midi(0xB0, (uint8_t)(20 + k), (uint8_t)(k & 1 ? 127 : 0));
                run(3);
            }
            for (k = 0; k < 27; k++)
                h1_key(k, 0);
            run(30);
            for (k = 0; k < 4; k++)
                h1_midi(0xB0, (uint8_t)(20 + k), 50);
            mark();
            h1_encoder(H1_ENC_ALGO, 1);
            run(6);
        }
        h1_encoder(H1_ENC_PRESET, 1);
        run(12);
    }
    /* LAYERS: a loop of four instruments, then a key held over it */
    h1_button(H1_BTN_SEQ, 1);
    h1_button(H1_BTN_REC, 1);
    h1_button(H1_BTN_REC, 0);
    h1_button(H1_BTN_SEQ, 0);
    run(6);
    for (inst = 0; inst < 4; inst++) {
        h1_encoder(H1_ENC_PRESET, 3);
        h1_encoder(H1_ENC_ALGO, inst + 1);
        run(60);
        tap(H1_BTN_REC);
        for (f = 0; f < 8; f++) {
            h1_key(pat[f + inst], 1);
            h1_key(pat[f + inst] + 4, 1);
            run(30);
            h1_key(pat[f + inst], 0);
            h1_key(pat[f + inst] + 4, 0);
            run(8);
        }
        if (inst)
            tap(H1_BTN_REC);
        else
            run(500);
        mark();
    }
    h1_key(10, 1);
    run(1500);
    h1_key(10, 0);
    run(200);
    mark();
    /* leant on: every instrument, its lean on a knob, a chord at velocities, the lean swept in and out (a
     * render that reads Voice.press must read what the engine set before either core began) */
    for (inst = 0; inst < ni; inst++) {
        h1_control_set(H1_ENC_PRESET, inst);
        h1_pressure_route(inst, 1 + inst % 4, inst & 1 ? -80 : 80);
        run(6);
        for (k = 0; k < 3; k++)
            h1_key_vel(pat[k + inst % 8], 1, 30 + 40 * k);
        for (f = 0; f < 40; f++) {
            for (k = 0; k < 3; k++)
                h1_key_pressure(pat[k + inst % 8], (f < 20 ? f : 40 - f) * 6 + k * 10);
            if (f == 10)
                h1_midi(0xD0, 90, 0);
            run(2);
        }
        h1_midi(0xD0, 0, 0);
        for (k = 0; k < 3; k++)
            h1_key(pat[k + inst % 8], 0);
        run(30);
        mark();
    }
}

int main(int argc, char **argv)
{
    pthread_t th;
    int i, two = argc > 1 && argv[1][0] == '2';
    if (two) {
        pthread_create(&th, 0, core2, 0);
        h1_two_cores(run2, wait2);
    }
    play();
    if (two) {
        atomic_store(&job_state, 2);
        pthread_join(th, 0);
        if (!jobs) {
            printf("FAIL: nothing went to the second core\n");
            return 1;
        }
    }
    for (i = 0; i < nsec; i++)
        printf("%3d %016llx\n", i, (unsigned long long)sec[i]);
    return 0;
}
