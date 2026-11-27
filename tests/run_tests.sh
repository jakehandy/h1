#!/bin/sh
# h1's tests: the device app in fm1-base's host simulator (the platform simulated, the same app.c
# and core as the firmware), then LAYERS (tests/layers.c), each algorithm's own knobs, the resets and a
# step's eight notes (tests/knobs.c), each instrument's own volume and the volume ring (tests/volume.c),
# what 0.2.2 adds (tests/v022.c), ECHO after a long time on (tests/echo.c), two cores (tests/cores.c), what is hidden in the pictures (tests/eggs.c) and the line drawing (tests/lines.c) on the core alone. The release workflow
# runs this after the build; a failure stops it. (tests/stems.txt: the USB audio input's four stems, checked by tools/h1_stems.py)
set -eu
cd "$(dirname "$0")/.."
./build.sh --host
out=app/build/host
"$out/h1_host" tests/basic.txt "$out"
# USB host: OCT- and OCT+ held, a keyboard plugged in and played, unplugged, and back to the computer
"$out/h1_host" tests/usbhost.txt "$out"
# the loop kept through a power-off while it plays (#39), and one cut off in the middle of its save
"$out/h1_host" tests/looppower.txt "$out"
# the loop out to a computer (0.3.1): the frames read back into a .mid, checked note for note
rm -f "$out/export.syx"
"$out/h1_host" tests/export.txt "$out"
python3 tools/h1_loop_mid.py "$out/export.syx" "$out/export.mid" > "$out/export.txt"
cat "$out/export.txt"
grep -q "h1 loop: 51 notes, 3 tracks" "$out/export.txt"
# stems (USB audio's channels 3..10): three layers and the instrument in hand, each on its own pair, adding up to the master
"$out/h1_host" tests/stems.txt "$out"
python3 tools/h1_stems.py "$out/stems.wav" --check
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
# LAVA's ninth algorithm (HOOVER): parts counted, state version 9, a loop saved as version 2, and 0.2.2's of both read
"${CC:-cc}" -std=c99 -O1 -g -ffp-contract=off -fsanitize=address,undefined -fno-sanitize-recover=all -w \
    core/*.c tests/ninth.c -o build/h1ninth -lm
ASAN_OPTIONS=detect_leaks=0 build/h1ninth
# BARD's drone holds a moment after the playing stops and dies away, never cut off
"${CC:-cc}" -std=c99 -O2 -ffp-contract=off -w core/*.c tests/drone.c -o build/h1drone -lm
build/h1drone | tail -1
# the loop's room and length: four layers on eight bars, 16 to 128 bars, a full loop, 0.2.2's loop read
"${CC:-cc}" -std=c99 -O2 -g -ffp-contract=off -fsanitize=address,undefined -fno-sanitize-recover=all -w \
    core/*.c tests/looper.c -o build/h1looper -lm
ASAN_OPTIONS=detect_leaks=0 build/h1looper
# ECHO the same after 25 minutes on as at start (issue #31: it went grainy the longer the unit was on)
"${CC:-cc}" -std=c99 -O2 -ffp-contract=off -w core/*.c tests/echo.c -o build/h1echo -lm
build/h1echo
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
# what is hidden in the pictures, and the noise each makes
"${CC:-cc}" -std=c99 -O1 -g -ffp-contract=off -fsanitize=address,undefined -fno-sanitize-recover=all -w \
    core/*.c tests/eggs.c -o build/h1eggs -lm
ASAN_OPTIONS=detect_leaks=0 build/h1eggs
# 0.3.1: the stuck ARP note, HOLD ARP, the MIDI and SETUP pages saved, EDIT from any page, the layer view, the CPU guard, silence
"${CC:-cc}" -std=c99 -O1 -g -ffp-contract=off -fsanitize=address,undefined -fno-sanitize-recover=all -w \
    core/*.c tests/v031.c -o build/h1v031 -lm
ASAN_OPTIONS=detect_leaks=0 build/h1v031
# 0.3.3: CLEAR ALL lets go of the layers; the steps and the layers saved with the loop's record
"${CC:-cc}" -std=c99 -O1 -g -ffp-contract=off -fsanitize=address,undefined -fno-sanitize-recover=all -w \
    core/*.c tests/song.c -o build/h1song -lm
ASAN_OPTIONS=detect_leaks=0 build/h1song
# 0.4.0: a take's own length; the PLAY page; LEDS SCALE
"${CC:-cc}" -std=c99 -O1 -g -ffp-contract=off -fsanitize=address,undefined -fno-sanitize-recover=all -w \
    core/*.c tests/v040.c -o build/h1v040 -lm
ASAN_OPTIONS=detect_leaks=0 build/h1v040
# g_line against a distance worked out in floats at every pixel: never more than one ink level off
"${CC:-cc}" -std=c99 -O1 -g -ffp-contract=off -fsanitize=address,undefined -fno-sanitize-recover=all -w \
    core/gfx.c tests/lines.c -o build/h1lines -lm
ASAN_OPTIONS=detect_leaks=0 build/h1lines
