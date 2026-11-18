# H1

A custom firmware for the M-VAVE FM-1: five instruments, each a full-screen animated picture you
play. White or one colour on the bezel's black, so the screen reads bigger than it is.

| | Sound | Picture | Knobs |
|---|---|---|---|
| **PIP** | chiptune | a fat-pixel creature who hops on every note | SHAPE, WOBBLE, ZAP, CRUNCH |
| **URCHIN** | FM | an outline that is itself an FM wave; chords push out spines | RATIO, DEPTH, FUZZ, TAIL |
| **TWANG** | plucked strings | eight strings moving like real ones, and a plectrum who walks | DAMP, PICK, TONE, BUZZ |
| **YAWP** | vowel choir | a head per note; chords crowd the screen | VOWEL, WOBBLE, BREATH, SIZE |
| **TIDE** | analog pad | a night sea; each note lifts a swell and lights a star | GLOW, HALO, DRIFT, SURGE |

PRESETS picks the instrument, ALGORITHM picks one of its eight algorithms, and the four knobs
are always its four live modulations on the home screen. ARP, LFO, ENV, FX, SEL, GLO and SEQ
open settings pages that look the same for every instrument: four columns, one per knob.

## Status

The design is being approved in the browser simulator (`dist/h1-simulator.html`), which runs the
same C code that goes on the device. The device port in `port/fm1` is written but has not been
compiled or flashed. Nothing should be flashed until the simulator is signed off.

See [docs/RESEARCH.md](docs/RESEARCH.md) for the hardware and what was learned from existing
firmwares, and [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for how the code is laid out.
