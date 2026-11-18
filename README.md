# H1

A custom firmware for the M-VAVE FM-1: nine instruments, each a full-screen animated picture you
play. White or one colour on the bezel's black, so the screen reads bigger than it is.

| | Sound | Picture | Knobs |
|---|---|---|---|
| **PIP** | chiptune | a fat-pixel creature who hops on every note | SHAPE, WOBBLE, ZAP, CRUNCH |
| **BONK** | drum kit, one drum per pitch class | twelve shapes that fill, pop and throw rings when hit | TUNE, DECAY, PUNCH, DIRT |
| **URCHIN** | FM | an outline that is itself an FM wave; chords push out spines | RATIO, DEPTH, FUZZ, TAIL |
| **TWANG** | plucked strings | eight strings moving like real ones, and a plectrum who walks | DAMP, PICK, TONE, BUZZ |
| **LAVA** | wavefolder bass | contour lines around blobs that rise with each note | FOLD, HEAT, LEAN, SUB |
| **KNOT** | additive bells | a harmonograph: the interval you hold decides the figure | BLOOM, RING, TWIST, WARP |
| **INK** | gliding lead | a scroll the melody is painted on, in a different hand per algorithm | SLIDE, SHAKE, PRESS, DRY |
| **TIDE** | analog pad | a night sea; each note lifts a swell and lights a star | GLOW, HALO, DRIFT, SURGE |
| **HAUNT** | spooky ambient | hooded apparitions of hanging thread, fog and black water | FOG, DREAD, SHIVER, DEPTH |

PRESETS picks the instrument, ALGORITHM picks one of its eight algorithms, and the four knobs
are always its four live modulations on the home screen. FX, SEL, ENV, LFO, GLO, ARP and SEQ
open settings pages that look the same for every instrument: four columns, one per knob, under
a small map of the button panel with the current page lit.

SEQ has two modes, swapped by its first column or by holding SEQ. LOOP records what you play
(REC, then play; REC again to overdub). STEP is a pattern of up to 64 steps drawn as small
notches along the bottom of the screen: REC turns writing on, a key or chord fills the step
under the cursor and moves on, EDIT writes a rest, SELECT moves the cursor, PLAY runs it.
BARS, METER (4/4, 3/4, 5/4, 6/8, 7/8) and GRID or SNAP are the other three columns.

## Status

The design is being approved in the browser simulator (`dist/h1-simulator.html`), which runs the
same C code that goes on the device. The device port in `port/fm1` is written but has not been
compiled or flashed. Nothing should be flashed until the simulator is signed off.

See [docs/RESEARCH.md](docs/RESEARCH.md) for the hardware and what was learned from existing
firmwares, and [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for how the code is laid out.
