# How H1 is built

```
core/        the whole firmware: portable C99, single-precision float, no libc, no malloc
  h1.h         the public face: panel in, sound and picture out
  synth.c      voices, envelopes, LFO, arp, loop recorder, step sequencer, the event queue from the UI
  fx.c         tone, echo, space
  gfx.c        anti-aliased lines, discs, rings, ellipses, filled polygons, a stroke font
  ui.c         what every control does, the name flash, the knob strip, the settings pages
  inst_*.c     one file per instrument: its sound and its picture, side by side
port/web/    the browser simulator: the core as WebAssembly, one HTML file
port/host/   a script runner that writes images and WAVs: where art is tuned and tests run
port/fm1/    the device, on fm1-base (written, not yet compiled)
tools/       build scripts
dist/        the built simulator page
```

## The rule that keeps the simulator honest

The ports contain no behaviour. Everything Jake sees and hears in the browser is `core/`, and the
device compiles the same files. A port only moves bytes: keys, buttons and encoder detents in;
audio frames, LED bits and changed screen rectangles out.

## Two threads

`h1_render` runs in the audio interrupt. Everything else (`h1_key`, `h1_button`, `h1_encoder`,
`h1_frame`, `h1_present`) runs in the main loop. The UI reaches the engine through a small
single-writer event queue; the engine publishes a snapshot (voice envelopes, levels, a scope
buffer) that the picture reads.

## The picture

Instruments draw into a 240 x 240 buffer of 8-bit ink levels: 0 is the bezel's black, 255 is
full ink. A 256-entry palette turns ink into the instrument's colour when the frame is sent.
`h1_present` compares each row with what the panel already shows and reports rectangles of at
most 20 rows, so the device sends only what moved. It also returns the byte count, which the
simulator turns into its screen-link meter.

Motion uses springs integrated with the real frame time, so the art moves the same at any frame
rate and never snaps.

## An instrument

One `InstDef`: a name, an ink colour, four knob names, eight algorithm names, defaults, and five
functions: `block` (once per 32 frames), `note`, `render` (one voice), an optional `post` (the
mix), and `draw`. Adding an instrument is one new file, one line in the table and `NINST` plus one.

## The sequencer

Time is 96 ticks per quarter note. A bar is 384, 288, 480, 288 or 336 ticks for 4/4, 3/4, 5/4,
6/8 and 7/8. The loop recorder stores timed events in RAM and is lost at power off. The step
pattern is 64 steps of up to four notes, on a 1/8, 1/16 or 1/32 grid, and is saved with the
rest of the state. `eng_seq_info` gives the picture what it needs to draw the progress line or
the notches.

## Controls as values

`h1_control` reports what each encoder currently means (its name, range, value and value text)
and `h1_control_set` sets it outright. The device never calls them. The simulator uses them to
show each control as a labelled slider, while its nudge buttons, the mouse wheel and the number
keys still send real encoder detents.

## RAM on the device

The pool holds 336 KB. H1 asks for about 320 KB of it:

| Buffer | KB |
|---|---|
| Framebuffer and the copy of what the panel shows | 115 |
| String delay lines (8 voices, 2 each) | 66 |
| Echo | 64 |
| Reverb | 48 |
| Two RGB565 bands for the panel | 19 |
| LAVA's field and INK's scroll history | 7 |

fm1-base keeps buffers of its own in the same pool, so this needs measuring on the first device
build. The first things to shrink are the string lines and the reverb (16-bit samples halve
both).

## Building

- `tools/build_host.sh` then `build/h1host OUTDIR script...` for images and WAVs.
- `tools/build_web.sh` for `dist/h1-simulator.html` (clang with the wasm32 target and wasm-ld;
  set `WASM2JS` to binaryen's wasm2js to include the plain-JavaScript fallback).
- The device build goes through fm1-base's `tools/build.py` with `port/fm1` as the app.
