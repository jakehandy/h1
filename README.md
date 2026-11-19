# h1

Custom firmware for the M-VAVE FM-1. Fourteen instruments with dynamic visuals. Eight algorithms 
and four live knobs, plus a loop recorder, a step sequencer, an arpeggiator and a metronome. 
Works on both the original flash chip and the newer UC25HQ80. Uses fm1-base.

**USB audio and MIDI.** Over one USB cable the FM-1 is an audio input and a MIDI device, so you can
record h1 straight into a computer or a phone. MIDI in works over USB and the TRS jack: notes play
at their velocity, and CC 20 to 23 turn the four knobs. MIDI out is USB only, on channel 1: the
FM-1's TRS jack is an input. See [USB audio and MIDI](#usb-audio-and-midi).

[![Install on The Firmware Hub](https://img.shields.io/badge/install-The%20Firmware%20Hub-c8ff4a?style=for-the-badge)](https://thefirmwarehub.com/firmware/h1/)

The easiest way to install h1 is [its listing on The Firmware Hub](https://thefirmwarehub.com/firmware/h1/):
connect the FM-1 over USB in Chrome or Edge and press **Install**. [Install](#install) below has
the details and the manual `.fwsc` route.

<p>
<img src="docs/img/cozy.webp" width="104" alt="COZY"> <img src="docs/img/orbit.webp" width="104" alt="ORBIT"> <img src="docs/img/koi.webp" width="104" alt="KOI"> <img src="docs/img/haunt.webp" width="104" alt="HAUNT"> <img src="docs/img/tide.webp" width="104" alt="TIDE">
<img src="docs/img/pip.webp" width="104" alt="PIP"> <img src="docs/img/bonk.webp" width="104" alt="BONK"> <img src="docs/img/urchin.webp" width="104" alt="URCHIN"> <img src="docs/img/twang.webp" width="104" alt="TWANG"> <img src="docs/img/lava.webp" width="104" alt="LAVA">
<img src="docs/img/knot.webp" width="104" alt="KNOT"> <img src="docs/img/ink.webp" width="104" alt="INK"> <img src="docs/img/bard.webp" width="104" alt="BARD"> <img src="docs/img/turbo.webp" width="104" alt="TURBO">
</p>

**The full guide is at [jakehandy.com/h1](https://jakehandy.com/h1).** It covers every instrument's
knobs and algorithms, the settings pages, loop and step recording, and FAQs.

| | Sound | Knobs |
|---|---|---|
| **COZY** | lo-fi keys | DUST, WOW, MUFFLE, SWAY |
| **ORBIT** | space synths | SWEEP, SPIN, SHIMMER, GRAVITY |
| **KOI** | struck metal and wood: handpan, kalimba, marimba | MALLET, RIPPLE, LINGER, SINK |
| **HAUNT** | spooky ambient | FOG, DREAD, SHIVER, DEPTH |
| **TIDE** | analog pad | GLOW, HALO, DRIFT, SURGE |
| **PIP** | chiptune | SHAPE, WOBBLE, ZAP, CRUNCH |
| **BONK** | drum kit, a drum on every note of the octave | TUNE, DECAY, PUNCH, DIRT |
| **URCHIN** | FM | RATIO, DEPTH, FUZZ, TAIL |
| **TWANG** | plucked strings, from recordings of real ones: harp, guitars, sitar, banjo, koto, bass, and a bowed cello | DAMP, PICK, TONE, BUZZ |
| **LAVA** | wavefolder bass | FOLD, HEAT, LEAN, SUB |
| **KNOT** | additive bells | BLOOM, RING, TWIST, WARP |
| **INK** | gliding lead | SLIDE, SHAKE, PRESS, DRY |
| **BARD** | medieval, from recordings of real instruments: lute, harp, psaltery, gurdy, rebec, shawm, bagpipe, organ, over a drone | DRONE, COURSE, BITE, AGE |
| **TURBO** | eighties synths, a little tape-warped | CHORUS, TAPE, GLARE, DRIVE |

<p>
<img src="docs/img/page.png" width="200" alt="A settings page: four columns, one per knob"> <img src="docs/img/step.png" width="200" alt="A step pattern, the notches along the bottom"> <img src="docs/img/hud.png" width="200" alt="TIDE with the knob strip pinned">
</p>

## Try it in the browser

Open [the simulator](https://jakehandy.com/public/h1/simulator.html). It lives in its own repo,
[fm1-browser-simulator](https://github.com/jakehandy/fm1-browser-simulator), opens on h1, runs the
same C code that goes on the device, and can play other FM-1 firmwares too.

## Install

> **h1 is new.** Its releases have been installed and played on one FM-1, a unit bought in
> November 2026: v0.1.0 from the stock firmware, each later one over the one before. Going back to stock from h1,
> installing h1 from another custom firmware, and a unit with the older flash chip have not been
> tried yet:
> [docs/RELEASE-CHECKLIST.md](docs/RELEASE-CHECKLIST.md) says what has.

### Before you install

- **Back up first.** Every firmware keeps your work in the FM-1's flash in its own way. Presets,
  projects and samples saved by the firmware you are leaving may be gone or unreadable afterwards,
  and going back doesn't always bring them back. Save them with that firmware's own backup. The
  Firmware Hub's notes on this are [here](https://thefirmwarehub.com/kb/#data).
- Charge the FM-1, and use a USB cable that carries data.
- h1 is community firmware with no warranty. You install it at your own risk. It has nothing to
  do with M-VAVE.

### From the stock firmware (V15)

If the FM-1 runs something older than V15, update it to V15 with M-VAVE's updater first.

1. Connect the FM-1 over USB to a computer running Chrome or Edge.
2. Open [h1 on The Firmware Hub](https://thefirmwarehub.com/firmware/h1/), press **Connect FM-1** and allow
   MIDI.
3. Press **Install** on h1. It takes about a minute. Keep the cable in until the FM-1 restarts
   into h1.

### From another custom firmware

The same three steps. The firmware you are running starts h1's update loader, and the loader
writes h1. If the Hub says the FM-1 is still in another firmware's update mode, finish that update
with that firmware's installer first.

### From a terminal

Download `h1-X.Y.Z.fwsc` from [Releases](https://github.com/jakehandy/h1/releases), then from a
clone of this repo (`git clone --recurse-submodules`):

```
pip3 install mido python-rtmidi
python3 base/tools/fm1_install.py h1-X.Y.Z.fwsc
```

It works from V15 and from other firmwares, asks before it writes, and checks what the FM-1
answers afterwards.

## Leave h1 again

h1's package carries an update loader, so the same installers can replace it.

- **Another custom firmware:** press **Install** on its card at
  [The Firmware Hub](https://thefirmwarehub.com), or use that firmware's own installer.
- **Back to stock:** press **Back to official V15** in the Hub's
  [Official firmware](https://thefirmwarehub.com/#official) box.

h1 only saves its own settings. Leaving it doesn't restore what an earlier firmware had saved.
That is what the backup is for.

## If something goes wrong

- **An install stopped halfway:** plug the FM-1 back in and press Install again. The loader picks
  up where it stopped.
- **h1 doesn't start:** after two failed starts it comes up in SAFE MODE. There is no sound, but
  USB works, so the Hub can install h1 again, another firmware, or V15.
- **Nothing reaches it:** hold **REC** while you switch it on. That is the chip's own update mode,
  and the Hub's [rescue page](https://thefirmwarehub.com/tools/rescue/) takes it from there. The
  chip's bootloader is in ROM and can't be overwritten.

## Compatibility

- **M-VAVE FM-1 only.** Not the SMK-37.
- **Both flash chips.** h1 saves and updates on the original Puya P25Q80 (JEDEC ID `0x856014`)
  and on the UC25HQ80 (`0xB36014`) fitted to units sold since autumn 2026.
- USB audio out, USB MIDI in and out, and Bluetooth MIDI in and out with a computer or a phone. MIDI
  in over the TRS jack too. No MIDI out over TRS. See [USB audio and MIDI](#usb-audio-and-midi).
- A screen reader ([fm1-reader](https://github.com/charlesvestal/fm1-reader)) hears the
  instrument, the algorithm, the page and each control's value as it changes. It can't describe
  the pictures.
- Package identity `FM-1_481`. A release X.Y.Z answers `FM-1_481XXYYZZ`.

## USB audio and MIDI

Plug the FM-1 into a computer or a phone with a USB cable that carries data. It shows up as one
device with two parts.

**Audio.** An audio input, like a USB microphone. Pick the FM-1 as the input in a DAW, a recording
app or the system's sound settings, and record. It carries the whole mix at the MASTER volume, the
same as the speaker. Nothing plays back through it: the FM-1 isn't a sound card for the computer.

**MIDI in,** over USB or the TRS jack, on any channel:

- Notes play the instrument in hand at their velocity, about 20 dB from the softest to the hardest,
  and darker the softer they are. The FM-1's own keys play at full.
- CC 20 to 23 turn the four knobs. CC 123 stops every note.
- The loop and the step sequencer record the velocity and play it back.

**Bluetooth MIDI.** The FM-1 advertises as a Bluetooth MIDI device from power-on. Connect to it from
macOS (Audio MIDI Setup, Bluetooth), iOS (an app's Bluetooth MIDI page, such as GarageBand's) or
anything else that speaks BLE-MIDI. Everything below works over it the same as over USB. One
Bluetooth link at a time.

**MIDI out,** over USB and Bluetooth, on channel 1 unless the MIDI page says otherwise:

- The notes of the keys, the arpeggiator and the sequencer, so a DAW can record what you play as
  MIDI, or h1 can play another synth.
- CC 20 to 23 as the four knobs turn.
- A note that came in over MIDI isn't sent back out, so a DAW that echoes its input doesn't loop.

**The MIDI page** (hold **HOME**):

| Knob | Setting | Values | What it does |
|---|---|---|---|
| 1 | CHAN | 1–16 | The channel notes and knob CCs go out on. MIDI in stays on any channel |
| 2 | SEND | ALL, LIVE, OFF | ALL: the keys, the arpeggiator, the loop and the steps (as before). LIVE: what your hands play (the keys and the arpeggiator), not the loop's or the steps' playback. OFF: no notes or knobs, only the clock |
| 3 | CLOCK | OFF, ON, +START | ON: MIDI clock at GLOBAL's tempo, 24 a beat, all the time, whether anything plays or not, so a looper, a delay or a tremolo pedal follows h1's tempo. It ignores SWING. +START: also Start when the loop or the steps start (a take, PLAY) and Stop when they stop |
| 4 | BT | ON, OFF | Bluetooth MIDI. Off, the radio is quiet. It stays as you left it through a power-off |

Changing CHAN or SEND sends all-notes-off where notes went before, so nothing hangs on the old channel.
The clock goes out over USB and Bluetooth, ahead of the notes when the queue is busy.

## Hotkeys

| Control | What it does |
|---|---|
| PRESETS | Instrument |
| ALGORITHM | One of the instrument's eight algorithms. Each keeps its own four knob settings |
| KNOB 1–4 | Home screen: the instrument's four live modulations. Settings page: its four columns |
| SELECT | Home screen: tempo. Settings page: next page. Writing steps: moves the cursor |
| FX SEL ENV LFO GLO ARP SEQ | Open that settings page. Press it again, or HOME, to go back |
| REC / PLAY | Loop: REC arms a take, again closes it, again overdubs. PLAY stops and starts it, and a stopped loop is kept through a power-off. Step: REC turns step-write on and off, PLAY runs the pattern |
| EDIT | Pins the knob strip on the home screen. Writing steps: leaves a rest |
| OCT − / + | Octave, two each way. Press both for the middle octave |
| hold OCT − and OCT + | Silence everything (HUSH), and send all-notes-off over MIDI |
| MASTER | Volume of everything. A ring in the middle of the screen shows it while it turns, marked GLOBAL |
| hold EDIT, turn MASTER or SELECT | Volume of the instrument in hand. The same ring, marked INSTRUMENT. MASTER sets it to where the knob stands, SELECT moves it a step a click. Holding EDIT alone shows where it stands |
| hold SEQ | Swap LOOP and STEP |
| hold GLO | Metronome: CLICK REC, CLICK ON, CLICK OFF |
| SEQ + REC | LAYERS on and off (on by default): with it on, each take keeps its own instrument, algorithm and settings |
| hold REC | Clear the loop or the pattern. With LAYERS on: clear what was recorded on the instrument in hand, and keep holding to three seconds to clear everything |
| hold HOME | The MIDI page: channel, what goes out, MIDI clock, Bluetooth. Hold HOME again, or tap it, to go back |
| hold SAVE | Reset the algorithm in hand: its four knobs go back to how they came (everything else saves by itself) |
| hold SAVE for two seconds | Reset this instrument: every algorithm's knobs, and its FX, ENV, LFO and ARP pages |
| hold SAVE for four seconds | Reset every instrument, and the KEYS and GLOBAL pages. The sequencer and what it holds, and the MIDI page, are left alone |

A button with a hold does its tap when you let go, so holding SEQ or GLO never opens the page, and
holding EDIT never pins the strip.

MASTER is one knob for two volumes, and it can only say where it stands. After you have turned it
with EDIT held it is no longer where the volume of everything was left, so that volume stays put
until you turn MASTER, without EDIT, back through it. Until then the ring shows the volume that is
held, and a small dot outside the ring shows where the knob is.

While the sequencer runs, the sixteen white keys light as the pattern, left to right: lit where
there are notes, with the playing step flipped. Up to sixteen steps each key is a step, up to
thirty-two each key is two. A step holds up to eight notes.

The four knobs belong to the algorithm. Turn ALGORITHM and they stand where you last left them on
that algorithm, not where they were on the one before. The settings pages and the instrument's
volume belong to the instrument: the volume is saved with it, and resetting the instrument puts it
back to full.

**LAYERS** gives the sequencer more than one instrument, and it's on unless you turn it off with
SEQ + REC. Record a loop, change the instrument (or just its knobs), press REC and play over it:
that take is a new layer. Changing the instrument over a loop says `REC: NEW LAYER`, each take
says `LAYER 2`, `LAYER 3` and so on, and a dot for each layer sits by the loop's progress line. A
take started over a stopped loop joins it, and nothing is erased until you hold REC. Steps work
the same way: each run of step-write is a layer.

Each layer keeps its own instrument, algorithm, four knobs, ENV and LFO, as they stood when its
take ended. Record a bass on COZY, turn COZY into a pad and record over it: the bass stays a bass.
Up to sixteen layers. The effects are one chain that follows the instrument in hand, and the
instrument's volume is shared by its layers (hold EDIT and turn MASTER or SELECT to sit one
instrument under another). The eight voices are shared by all the layers, and a step holds eight
notes in all. With the ARP on, the loop records the notes the arpeggiator played. The picture is
the instrument in hand. With LAYERS off, everything recorded plays on the instrument in hand.
The SEQ page's top line scrolls through the sequencer hotkeys and shows which way LAYERS is set.

The loop and its layers are saved once the loop is stopped with PLAY and nothing is sounding, and
come back after a restart. The step pattern and every instrument's settings save by themselves.

In the simulator: <kbd>A</kbd>–<kbd>'</kbd> are the white keys from F and <kbd>W</kbd> <kbd>E</kbd> <kbd>R</kbd> <kbd>Y</kbd> <kbd>U</kbd> <kbd>O</kbd> <kbd>P</kbd> <kbd>[</kbd>
the black keys. <kbd>Z</kbd> <kbd>X</kbd> change the octave, <kbd>←</kbd> <kbd>→</kbd> change the
instrument, and <kbd>↑</kbd> <kbd>↓</kbd> change the algorithm. <kbd>1</kbd>–<kbd>4</kbd> turn a knob
up, or down with <kbd>Shift</kbd>.

## For developers

```
core/    the whole firmware: portable C99, no libc, no malloc
app/     the device: fm1-base's app, a thin pass-through to core/
base/    fm1-base (a git submodule, with felucca-bsp inside it)
port/    other builds of the same core: host (pictures, WAVs), rfm1 (the browser simulator)
tests/   the device app in fm1-base's host simulator, LAYERS, the knobs and two cores on the core alone, and the crash hunt
```

- `./build.sh` builds `app/build/h1.fwsc`, a development build with identity `FM-1_48100`. It
  needs JieLi's toolchain and the AC79 SDK (see `base/README.md`). `./build.sh --release X.Y.Z`
  builds the release package.
- `sh tests/run_tests.sh` builds the device app for the host and runs `tests/basic.txt`, then
  runs `tests/layers.c`, `tests/knobs.c`, `tests/cores.c` and `tests/lines.c` on the core alone. It needs only a C
  compiler and Pillow.
- The chip's second core renders half the voices. `FM1B_CPU2=0 ./build.sh` builds the firmware
  without it: see "Two cores" in [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).
- `sh tests/run_fuzz.sh` is the crash hunt: the core under the compiler's address and
  undefined-behaviour checkers, fed any saved state and any panel input.
- `tools/build_host.sh` builds the core alone as `build/h1host`, a script runner that writes
  pictures and WAVs.
- CI (`.github/workflows/ci.yml`) runs the tests and the crash hunt and builds the firmware on every pull request.
- A tag `vX.Y.Z` runs `.github/workflows/release.yml`: the shared
  [fm1-build](https://github.com/zvenson/fm1-build) builder builds the package, runs the tests,
  signs build provenance and publishes the release. Go through
  [docs/RELEASE-CHECKLIST.md](docs/RELEASE-CHECKLIST.md) before tagging.
- `fmhub.json` is h1's card on The Firmware Hub. `firmware.json` is written by the build from
  `app/fm1.toml`.

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for how the code is laid out and
[docs/RESEARCH.md](docs/RESEARCH.md) for the hardware and what was learned from other firmwares.
`python3 tools/guide_media.py` renders the pictures in `docs/img` from the current build. Add
`--site <path to jakehandynew>` to also copy them, and the simulator from a clone of
fm1-browser-simulator, to the guide site.

## Licence

h1 is [MIT](LICENSE). The recordings BARD and TWANG are built on are public domain (CC0); [docs/SAMPLES.md](docs/SAMPLES.md) credits each one.

The firmware package also contains [fm1-base](https://github.com/charlesvestal/fm1-base) by
Charles Vestal and [felucca-bsp](https://github.com/hugelton/felucca-bsp), which come from
[Felucca](https://github.com/hugelton/Felucca) by Leo Kuroshita, Hügelton Instruments. Both are
MPL-2.0, unchanged, and their source is the `base` submodule at the commit a release was built
from. Their notices are in `base/LICENSING.md` and `base/bsp/NOTICE`.

M-VAVE and FM-1 are trademarks of their owners. h1 is not affiliated with M-VAVE.
