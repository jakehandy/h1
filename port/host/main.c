/* h1 host port: runs the core on a computer from a small script and writes what it made:
 * frames as PGM/PPM images and the sound as a WAV. No hardware, no browser: this is where the
 * art is tuned and where tests run.
 *
 *   h1host OUTDIR [script...]
 * Script words (whitespace separated):
 *   inst N        switch instrument        algo N        set algorithm (via detents)
 *   knob I V      set home knob I to V     enc E D       turn encoder E by D detents
 *   key K 1|0     press or release a key   btn B 1|0     press or release a button
 *   vkey K VEL    press a key at a velocity, 1..127 (key K 0 lets it go)
 *   lean K P      lean on held key K, 0..127, from now on (a key let go is back to none)
 *   leanrun K P MS  run MS milliseconds while the lean on key K goes in a straight line to P
 *   route T D     the instrument in hand's lean onto home knob T (1..4, 0 none), D -100..100 at a full lean
 *   midi S A B    a MIDI message in, three bytes in decimal (S 160: poly aftertouch, 208: channel pressure)
 *   master V      the volume knob, 0..1000
 *   run MS        advance MS milliseconds  shot NAME     write OUTDIR/NAME.ppm
 *   wav NAME      start writing audio      bytes         print the present() byte count stats, and what a
 *                                                        frame took to draw here (the FM-1 takes about 39 times as long)
 */
#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef __SSE2__
#include <pmmintrin.h>
#endif
#include "../../core/h1.h"

static uint32_t now_ms;
static FILE *wav;
static uint32_t wav_frames;
static const char *outdir = ".";
static uint64_t bytes_sum;
static uint32_t bytes_max, frames_n;
static double draw_s, draw_max;
static int lean_v[H1_NKEYS];                     /* what each key is leant on: given to the core every frame */

static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static void wav_header(FILE *f, uint32_t frames)
{
    uint32_t sr = 44100, data = frames * 4, riff = 36 + data, br = sr * 4;
    uint16_t pcm = 1, ch = 2, ba = 4, bits = 16;
    uint32_t fmtlen = 16;
    fseek(f, 0, SEEK_SET);
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
    fwrite(&fmtlen, 4, 1, f); fwrite(&pcm, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&sr, 4, 1, f);
    fwrite(&br, 4, 1, f); fwrite(&ba, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f);
}

static void run(uint32_t ms)
{
    static int32_t buf[735 * 2];
    uint32_t end = now_ms + ms;
    while (now_ms < end) {
        uint32_t i, b;
        for (i = 0; i < H1_NKEYS; i++)           /* (as a port does: every frame, for the keys held) */
            if (lean_v[i] >= 0)
                h1_key_pressure((int)i, lean_v[i]);
        h1_render(buf, 735);                     /* 1/60 s of audio */
        if (wav) {
            for (i = 0; i < 735 * 2; i++) {
                int16_t s = (int16_t)(buf[i] >> 8);
                fwrite(&s, 2, 1, wav);
            }
            wav_frames += 735;
        }
        now_ms += 1000 / 60 + (frames_n % 3 == 0);
        {
            double t0 = now_s(), d;
            h1_frame(now_ms);
            d = now_s() - t0;
            draw_s += d;
            if (d > draw_max && frames_n > 2) draw_max = d;
        }
        b = h1_present(0);
        bytes_sum += b;
        if (b > bytes_max && frames_n > 2) bytes_max = b;
        frames_n++;
    }
}

static void shot(const char *name)
{
    char path[512];
    const uint8_t *fb = h1_framebuffer();
    const uint16_t *pal = h1_palette();
    FILE *f;
    int i;
    snprintf(path, sizeof path, "%s/%s.ppm", outdir, name);
    f = fopen(path, "wb");
    if (!f) { perror(path); exit(1); }
    fprintf(f, "P6\n240 240\n255\n");
    for (i = 0; i < 240 * 240; i++) {
        uint16_t c = pal[fb[i]];
        uint8_t rgb[3] = {(uint8_t)(((c >> 11) & 31) * 255 / 31), (uint8_t)(((c >> 5) & 63) * 255 / 63), (uint8_t)((c & 31) * 255 / 31)};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

int main(int argc, char **argv)
{
    int i = 2;
    if (argc < 2) { fprintf(stderr, "usage: h1host OUTDIR script...\n"); return 2; }
    outdir = argv[1];
#ifdef __SSE2__
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);          /* as the FM-1 does: a denormal costs it nothing */
    _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
#endif
    for (i = 0; i < H1_NKEYS; i++)
        lean_v[i] = -1;
    i = 2;
    h1_init(44100);
    run(20);
#define ARG(n) (i + (n) < argc ? atoi(argv[i + (n)]) : 0)
    while (i < argc) {
        const char *w = argv[i];
        if (!strcmp(w, "inst")) { int d = ARG(1) - h1_instrument(); h1_encoder(H1_ENC_PRESET, d); i += 2; }
        else if (!strcmp(w, "enc")) { h1_encoder(ARG(1), ARG(2)); i += 3; }
        else if (!strcmp(w, "algo")) { h1_encoder(H1_ENC_ALGO, ARG(1)); i += 2; }
        else if (!strcmp(w, "knob")) { h1_midi(0xB0, (uint8_t)(20 + ARG(1)), (uint8_t)(ARG(2) * 127 / 100)); i += 3; }
        else if (!strcmp(w, "key")) {
            if (!ARG(2) && ARG(1) >= 0 && ARG(1) < H1_NKEYS) lean_v[ARG(1)] = -1;
            h1_key(ARG(1), ARG(2));
            i += 3;
        } else if (!strcmp(w, "vkey")) { h1_key_vel(ARG(1), 1, ARG(2)); i += 3; }
        else if (!strcmp(w, "lean")) {
            if (ARG(1) >= 0 && ARG(1) < H1_NKEYS) lean_v[ARG(1)] = ARG(2);
            i += 3;
        } else if (!strcmp(w, "leanrun")) {
            int k = ARG(1), to = ARG(2), ms = ARG(3), t, from;
            if (k >= 0 && k < H1_NKEYS) {
                from = lean_v[k] < 0 ? 0 : lean_v[k];
                for (t = 0; t < ms; t += 17) {
                    lean_v[k] = from + (to - from) * (t + 17 > ms ? ms : t + 17) / (ms ? ms : 1);
                    run(17);
                }
                lean_v[k] = to;
            }
            i += 4;
        } else if (!strcmp(w, "route")) { h1_pressure_route(h1_instrument(), ARG(1), ARG(2)); i += 3; }
        else if (!strcmp(w, "midi")) { h1_midi((uint8_t)ARG(1), (uint8_t)ARG(2), (uint8_t)ARG(3)); i += 4; }
        else if (!strcmp(w, "btn")) { h1_button(ARG(1), ARG(2)); i += 3; }
        else if (!strcmp(w, "master")) { h1_master(ARG(1)); i += 2; }
        else if (!strcmp(w, "run")) { run((uint32_t)ARG(1)); i += 2; }
        else if (!strcmp(w, "shot")) { shot(argv[i + 1]); i += 2; }
        else if (!strcmp(w, "wav")) {
            char path[512];
            if (wav) { wav_header(wav, wav_frames); fclose(wav); }
            snprintf(path, sizeof path, "%s/%s.wav", outdir, argv[i + 1]);
            wav = fopen(path, "wb");
            wav_frames = 0;
            if (wav) wav_header(wav, 0);
            i += 2;
        } else if (!strcmp(w, "bytes")) {
            printf("frames %u  avg %u bytes (%.1f ms at 44 Mbit/s)  max %u bytes (%.1f ms)\n", frames_n,
                   (unsigned)(bytes_sum / (frames_n ? frames_n : 1)), (double)(bytes_sum / (frames_n ? frames_n : 1)) * 8.0 / 44000.0,
                   bytes_max, bytes_max * 8.0 / 44000.0);
            printf("draw avg %.3f ms  max %.3f ms here\n", draw_s * 1000.0 / (frames_n ? frames_n : 1), draw_max * 1000.0);
            bytes_sum = 0; bytes_max = 0; frames_n = 0; draw_s = 0.0; draw_max = 0.0;
            i += 1;
        } else if (!strcmp(w, "leds")) {                       /* the key lights, low key first: # lit, . dark */
            uint32_t m = h1_key_leds();
            int k;
            for (k = 0; k < H1_NKEYS; k++) putchar(m >> k & 1u ? '#' : '.');
            putchar('\n');
            i += 1;
        } else { fprintf(stderr, "unknown word %s\n", w); return 2; }
    }
    if (wav) { wav_header(wav, wav_frames); fclose(wav); }
    return 0;
}
