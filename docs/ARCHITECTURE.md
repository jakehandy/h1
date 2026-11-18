# How H1 is built

```
core/        the whole firmware: portable C99, single-precision float, no libc, no malloc
  h1.h         the public face: panel in, sound and picture out
  synth.c      voices, envelopes, LFO, arp, loop recorder, the event queue from the UI
  fx.c         tone, echo, space
  gfx.c        anti-aliased lines, discs, rings, ellipses, a stroke font
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
mix), and `draw`. Adding a sixth instrument is one new file and one line in the table.

## RAM on the device

The pool holds 336 KB. H1 asks for about 313 KB of it:

| Buffer | KB |
|---|---|
| Framebuffer and the copy of what the panel shows | 115 |
| String delay lines (8 voices, 2 each) | 66 |
| Echo | 64 |
| Reverb | 48 |
| Two RGB565 bands for the panel | 19 |

fm1-base keeps buffers of its own in the same pool, so this needs measuring on the first device
build. The first things to shrink are the string lines and the reverb (16-bit samples halve
both).

## Building

- `tools/build_host.sh` then `build/h1host OUTDIR script...` for images and WAVs.
- `tools/build_web.sh` for `dist/h1-simulator.html` (clang with the wasm32 target and wasm-ld;
  set `WASM2JS` to binaryen's wasm2js to include the plain-JavaScript fallback).
- The device build goes through fm1-base's `tools/build.py` with `port/fm1` as the app.
