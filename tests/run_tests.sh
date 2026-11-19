#!/bin/sh
# h1's tests: the device app in fm1-base's host simulator (the platform simulated, the same app.c
# and core as the firmware), then LAYERS (tests/layers.c), each algorithm's own knobs, the resets and a
# step's eight notes (tests/knobs.c), each instrument's own volume and the volume ring (tests/volume.c),
# what 0.2.2 adds (tests/v022.c), the MIDI page (tests/midi.c), two cores (tests/cores.c) and the line drawing (tests/lines.c) on the core alone. The release workflow
# runs this after the build; a failure stops it.
set -eu
cd "$(dirname "$0")/.."
./build.sh --host
out=app/build/host
"$out/h1_host" tests/basic.txt "$out"
# Bluetooth MIDI: in, out beside USB, nothing echoed, nothing sent once the link goes
"$out/h1_host" tests/ble.txt "$out"
# (that build has the second core, as the firmware does: the simulator runs its jobs at once, in order)
# and the build without it: the same script
FM1B_CPU2=0 ./build.sh --host
"$out/h1_host" tests/basic.txt "$out"
./build.sh --host
mkdir -p build
"${CC:-cc}" -std=c99 -O1 -g -ffp-contract=off -fsanitize=address,undefined -fno-sanitize-recover=all -w \
    core/*.c tests/layers.c -o build/h1layers -lm
ASAN_OPTIONS=detect_leaks=0 build/h1layers
"${CC:-cc}" -std=c99 -O1 -g -ffp-contract=off -fsanitize=address,undefined -fno-sanitize-recover=all -w \
    core/*.c tests/knobs.c -o build/h1knobs -lm
ASAN_OPTIONS=detect_leaks=0 build/h1knobs
"${CC:-cc}" -std=c99 -O1 -g -ffp-contract=off -fsanitize=address,undefined -fno-sanitize-recover=all -w \
    core/*.c tests/volume.c -o build/h1volume -lm
ASAN_OPTIONS=detect_leaks=0 build/h1volume
# 0.2.2: the limiter, velocity, each layer's own settings, the loop saved, MIDI out, a 0.2.1 state
"${CC:-cc}" -std=c99 -O1 -g -ffp-contract=off -fsanitize=address,undefined -fno-sanitize-recover=all -w \
    core/*.c tests/v022.c -o build/h1v022 -lm
ASAN_OPTIONS=detect_leaks=0 build/h1v022
# the MIDI page: CLOCK (its rate and spacing, +START), CHAN, SEND, BT, the state
"${CC:-cc}" -std=c99 -O1 -g -ffp-contract=off -fsanitize=address,undefined -fno-sanitize-recover=all -w \
    core/*.c tests/midi.c -o build/h1midi -lm
ASAN_OPTIONS=detect_leaks=0 build/h1midi
# two cores sound the same as one, to the bit (tests/cores.c: the second core is a thread)
"${CC:-cc}" -std=c11 -O2 -ffp-contract=off -w core/*.c tests/cores.c -o build/h1cores -lm -lpthread
build/h1cores > build/cores1.txt
build/h1cores 2 > build/cores2.txt
if cmp -s build/cores1.txt build/cores2.txt; then
    echo "cores: $(wc -l < build/cores1.txt) sections, the same on one core and two"
else
    echo "FAIL cores: two cores sound different from one, in these sections:"
    diff build/cores1.txt build/cores2.txt | head -20
    exit 1
fi
# g_line against a distance worked out in floats at every pixel: never more than one ink level off
"${CC:-cc}" -std=c99 -O1 -g -ffp-contract=off -fsanitize=address,undefined -fno-sanitize-recover=all -w \
    core/gfx.c tests/lines.c -o build/h1lines -lm
ASAN_OPTIONS=detect_leaks=0 build/h1lines
