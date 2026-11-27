#!/usr/bin/env python3
"""h1's USB audio as a computer records it: ten channels, the master on 1 and 2, four stereo stems on 3..10
(core/h1.h h1_stems). Standard library only.

    tools/h1_stems.py REC.wav                  each pair's level, half a second at a time, and how far the four
                                               stems are from the master (what is left is the effects over the mix)
    tools/h1_stems.py REC.wav --split DIR      the master and the four stems as five stereo WAVs in DIR
    tools/h1_stems.py REC.wav --check          tests/run_tests.sh: every stem heard, each where it should be, and
                                               the stems adding up to the master but for its effects

Most DAWs split a ten-channel input into tracks themselves; --split is for an app that records the file whole."""
import math
import os
import struct
import sys
import wave

NAMES = ["master", "stem1", "stem2", "stem3", "stem4"]


def read(path):
    w = wave.open(path)
    if w.getnchannels() != 10 or w.getsampwidth() != 2:
        sys.exit(f"{path}: {w.getnchannels()} channels of {8 * w.getsampwidth()} bits, not h1's ten of 16")
    n = w.getnframes()
    s = struct.unpack(f"<{n * 10}h", w.readframes(n))
    return w.getframerate(), [s[c::10] for c in range(10)]


def rms(x):
    return math.sqrt(sum(v * v for v in x) / len(x)) if x else 0.0


def pair_rms(ch, p, a, b):
    return (rms(ch[2 * p][a:b]) + rms(ch[2 * p + 1][a:b])) / 2


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    rate, ch = read(sys.argv[1])
    n = len(ch[0])
    if "--split" in sys.argv:
        out = sys.argv[sys.argv.index("--split") + 1]
        os.makedirs(out, exist_ok=True)
        base = os.path.splitext(os.path.basename(sys.argv[1]))[0]
        for p, name in enumerate(NAMES):
            w = wave.open(os.path.join(out, f"{base}-{name}.wav"), "wb")
            w.setnchannels(2)
            w.setsampwidth(2)
            w.setframerate(rate)
            fr = [0] * (2 * n)
            fr[0::2], fr[1::2] = ch[2 * p], ch[2 * p + 1]
            w.writeframes(struct.pack(f"<{2 * n}h", *fr))
            w.close()
        print(f"{n} frames: {', '.join(base + '-' + x + '.wav' for x in NAMES)} in {out}")
        return
    half = rate // 2
    rows = []
    for a in range(0, n, half):
        rows.append([pair_rms(ch, p, a, a + half) for p in range(5)])
    resid = rms([ch[0][i] - sum(ch[c][i] for c in (2, 4, 6, 8)) for i in range(n)])
    master = rms(ch[0])
    if "--check" not in sys.argv:
        print("   s  " + " ".join(f"{x:>7}" for x in NAMES))
        for i, r in enumerate(rows):
            print(f"{i * 0.5:4.1f}  " + " ".join(f"{v:7.0f}" for v in r))
        print(f"master {master:.0f}, master less the stems {resid:.0f} ({resid / max(master, 1e-9):.0%})")
        return
    # tests/stems.txt: layers 1, 2 and 3 on stems 1..3, the instrument in hand played over them on stem 4
    bad = []
    for p in range(1, 5):
        if max(r[p] for r in rows) < 20:
            bad.append(f"stem {p} silent")
    for at, p in ((0, 1), (5, 2), (3, 3), (6, 4)):  # each layer's note on its own pair, the hand's on the fourth
        r = rows[at]
        if r[p] < 0.6 * r[0]:
            bad.append(f"{at * 0.5:.1f} s: stem {p} at {r[p]:.0f}, the master at {r[0]:.0f}")
    for i, r in enumerate(rows):                     # a stem is never louder than the master is with it, and none
        if r[0] < 2 and max(r[1:]) > 2:              # sounds while the master is silent
            bad.append(f"{i * 0.5:.1f} s: a stem without the master")
    if resid > 0.2 * master:
        bad.append(f"the stems are {resid / master:.0%} off the master")
    if bad:
        sys.exit("stems: " + "; ".join(bad))
    print(f"stems: four pairs heard, {resid / master:.0%} of the master is not on them (its effects)")


if __name__ == "__main__":
    main()
