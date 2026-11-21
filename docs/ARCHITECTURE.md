# How h1 (or handy-1) is built

```
core/        the whole firmware: portable C99, single-precision float, no libc, no malloc
  h1.h         the public face: panel in, sound and picture out
  synth.c      voices, envelopes, LFO, arp, loop recorder, step sequencer, the event queue from the UI
  fx.c         tone, echo, space
  gfx.c        anti-aliased lines, discs, rings, ellipses, filled polygons, a stroke font
  ui.c         what every control does, the name flash, the knob strip, the settings pages
  inst_*.c     one file per instrument: its sound and its picture, side by side
  smp.c        plays the recordings BARD and TWANG are built on (smp_data.c, written by tools/samples)
  plunk_data.c  PLUNK's recorded hits (written by tools/samples/drums.py; inst_plunk.c plays them)
  str.h        the string those recordings' plucks are fed into
port/rfm1/   the core as an .rfm1 (WebAssembly behind the fm1core ABI) for the FM-1 browser simulator
port/host/   a script runner that writes images and WAVs: where art is tuned and tests run
app/         the device, on fm1-base (base/, a submodule): built by ./build.sh
tools/       build scripts, and samples/, which cuts the recordings down to what the firmware carries
dist/        h1.rfm1, the build the simulator loads
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

## Two cores

The FM-1's chip has a second core, and fm1-base can hand it one job at a time from the audio
interrupt (`base/docs/SECOND-CORE.md`). `h1_two_cores(run, wait)` tells the core a port has one.
The engine then renders a part's voices half on each: a voice's control block (`voice_block`: its
envelope, its glide, the instrument's `render`) touches the voice and its own buffer in `vt` and
nothing else, so the second core takes every other voice while the first does the rest, and the
first mixes them afterwards in voice order. The sound is the same to the bit as on one core.
`tests/cores.c` plays everything on one core and on two (a real thread) and compares.

What that asks of an instrument: `render` may write the voice it is given and its output, and read
anything `block` and `note` set up. It must not write memory another voice's `render` reads. (COZY
and BARD each set a `sounding` flag from `render`; both cores only ever set it, and `post` reads it
after they are done.)

The second core is in the default build (`cpu2 = true` in `app/fm1.toml`). On an FM-1 playing 16
notes a second it took the picture from 34 to 44 frames a second on average, and the slowest
setting from 12 to 28. `FM1B_CPU2=0 ./build.sh` makes the build without it, and CI keeps both
packages. Some FM-1s misread memory on the second core. fm1-base checks each job it hands over,
and `app/fm1.toml` sets `cpu2_fallback`, so an FM-1 that fails a check goes on with one core until
it is switched off. A misread inside a job's own data is not checked. The timing readout ends with
`CORES 2` and how deep the second core's stack has gone, or `CORES 1` and why.

## The picture

Instruments draw into a 240 x 240 buffer of 8-bit ink levels: 0 is the bezel's black, 255 is
full ink. A 256-entry palette turns ink into the instrument's colour when the frame is sent.
`h1_present` compares the frame with what the panel already shows, in tiles of 8 pixels, and
reports rectangles of at most 20 rows, so the device sends only what moved. Two ink levels that
come out as the same colour count as no change. It also returns the byte count, which the
simulator turns into its screen-link meter.

The device takes the same thing in pieces, because sending is as slow as drawing there (a full
screen is 21 ms of SPI). `h1_present_begin` finds what changed, copies it to the panel's copy and
queues the rectangles; `h1_present_next` hands them out; their pixels are read from `h1_panel()`,
which the next frame's drawing does not touch. The drawing primitives call a hook
(`h1_while_drawing`), and `app/app.c` sends a rectangle from it whenever the panel has finished
the last one. A frame costs the longer of drawing and sending, not their sum.

KNOT VOID clears a path through the picture before it draws its cord. It marks the path in a
1-bit mask (`g_bed_move`, `g_bed_to`) and clears the framebuffer under the mask once
(`g_bed_clear`), so a cord that crosses itself is not cleared stroke by stroke.

Most of a frame is lines (URCHIN draws about 950 of them over 49,000 pixels), and a pixel of a line
costs the FM-1 far more than it costs a desktop. `g_line` sets up a line in floats and then steps a
pixel's distance from it in integers, down each column and from column to column. In a path
(`g_poly`, `g_arc`) a stroke's round start lies inside the stroke before it, so it is left out. The
old way, a float distance at every pixel, is kept for a line too long, too far off the screen or too
wide for the integers. `tests/lines.c` holds `g_line` to within one ink level of 255 of a float
distance at every pixel, over random lines, paths and arcs.

Soft things (steam, candle glow, light shafts, rain, comet tails) are not lines. `g_glow` is a round
falloff and `g_beam` a stroke whose width and ink both taper along it; both dither their faint ink on
a 4x4 pattern so a gradient costs no float per pixel and no banding. In `G_PAINT` they darken what is
under them. `g_fillv` fills a polygon with ink that changes down its rows (ghost cloth, candle wax),
and `g_comet` builds a comet from beams, a glow and a few grains. A soft shape changes every pixel it
covers, so it also costs bytes to the panel: keep the ones that move small.

Motion uses springs integrated with the real frame time, so the art moves the same at any frame
rate and never snaps.

## An instrument

One `InstDef`: a name, an ink colour, four knob names, eight algorithm names, defaults, and five
functions: `block` (once per 32 frames), `note`, `render` (one voice), an optional `post` (the
mix), and `draw`. Adding an instrument is one new file, one line in the table and `NINST` plus one.

An instrument holds the numbers of one algorithm at a time. With LAYERS on, more than one of its
algorithms can sound in the same block, so `block` is called again for each before that
algorithm's voices are rendered. `Blk.step` is the time that has passed: the control block's the
first time `block` is called in it, 0 on the later calls. Anything in `block` that moves on its
own (a phase, a wander) must advance by `Blk.step`, or it would run faster with every layer.

The four home knobs belong to the algorithm: `st.knob[instrument][algorithm][4]`, read through
`KNOBS` and `KNOBS_NOW`. Every algorithm starts on the instrument's `def.knob0`. The engine eases
the knobs of the algorithm an instrument is on (`inst_touch`), which are the only ones a hand can
turn, and they ease across a change of algorithm too. A part that goes on sounding on another
algorithm gets that algorithm's knobs as they stand (`part_set`). What an instrument keeps for all
its voices follows the algorithm in hand: its `post`, and a clock in `block` whose rate is a knob
(ORBIT's spin). `tests/knobs.c` holds all of it, and the three resets on SAVE.
`post` runs once per instrument, on that instrument's own mix.

Every instrument has a volume of its own beside MASTER (EDIT held, and SELECT or MASTER): `INST_VOL`, 0 to
100, kept in `InstParams.mix[IP_CUT]` as how far it is turned down, so the zero that a state from
before it holds there is full. The engine takes an instrument's mix into the sum at that volume
after its `post`, as a square like MASTER's, eased over about ten milliseconds and ramped across
the block. At full the samples go in untouched, so nothing sounds different until one is turned.
It is the instrument's: saved with it, back at full on the instrument's reset, left alone by the
algorithm's. The ring that shows either volume is `draw_volume` in `ui.c`. MASTER's gain is the port's, as
it was, but the port asks `h1_global_volume()` first: once the knob has been the instrument's
(turned with EDIT held) the volume of everything is held where it was, and the port uses that
until the knob, turned without EDIT, comes back through it. `tests/volume.c` holds
all of it.

## The sequencer

Time is 96 ticks per quarter note. A bar is 384, 288, 480, 288 or 336 ticks for 4/4, 3/4, 5/4,
6/8 and 7/8. A loop is 1 to 128 bars (`st.seq[1]` is log2 of the bars; 128 bars of 5/4 is 61,440 ticks,
inside the events' 16-bit tick). The loop recorder stores timed events (eight bytes each: tick, note, on, part, layer,
velocity) in RAM, 2,048 of them (`NLEV`), kept in order of tick so that each audio block finds the ones it
plays by halving (reading them all every block would cost more than the loop's sound). A note-on goes in
only with room kept for its note-off, and a note the full loop turns away moves `eng_loop_full`, which the
UI shows as LOOP FULL. The saved loop is the header and those events as they lie in RAM (`eng_loop_blob`,
little-endian, no copy), and the device keeps them in a ring of its own in the data area (app.c `ring_step`,
0xC8000..0xDBFFF: four slots of five sectors, `h1_loop` / `h1_loop_load`). A store save would erase
first, and an erase stalls the sound for about 50 ms, so a save is a record written into erased space,
a 256-byte page a frame, with a header page (magic `H1LR`, sequence, length, CRC-32) written last. That
happens as soon as no take is going in, while the loop plays (#39). A slot takes records one after
another, page-aligned, so a loop that never stops has room for many saves, not one a slot (0.3.0's first
builds lost the fourth take that way). The newest record whose header reads right is the loop. A save
given up part-way (a take moved the events under it, or a write failed) gets a dead header (`H1LX`, how
far it got), so the records after it are still found; a slot whose next header is blank but whose bytes
after it are not (a power-off mid-save) takes no more until it is erased. Slots other than the loop's are
erased at start, before the sound, or once the loop is stopped and nothing sounds. A record holds a blob
of up to 20,224 bytes (`H1_LOOP_MAX` must stay under it). Before
the ring the loop was store object `OBJ_LOOP` (0xDE000); it is read once, when the ring holds none. The step
pattern is 64 steps of up to eight notes, on a 1/8, 1/16 or 1/32 grid, and is saved with the
rest of the state. `eng_seq_info` gives the picture what it needs to draw the progress line or
the notches.

Every recorded note also stores its part: the instrument and algorithm it was played on, in one
byte (`PART`: 1 + instrument × 9 + algorithm, 0 for none, so sixteen instruments of nine algorithms
fit; before version 9 it was `0x80 | instrument << 3 | algorithm`, which only had room for eight, and
`PART_V8` reads that). Every voice carries the part it sounds and whether the sequencer started it. With
LAYERS off (`st.seq[6] == 0`) the stored parts are ignored and recorded notes go through the same
path as a key, on the instrument in hand. With LAYERS on, the sequencer starts voices on the
stored part directly, past the arpeggiator (the loop records what the arpeggiator played, not the
keys held). The engine then renders the voices part by part into a mix per instrument, runs each
instrument's `post` on its mix, and sums them into the one effects chain, which takes its
settings from the instrument in hand. An instrument that is not in hand and has gone quiet keeps
its `post` running for a third of a second, fading, so its tail is not cut. The snapshot for the picture
leaves out the voices of other instruments.

Since 0.2.2 LAYERS is on by default, and each take is a layer of its own (`st.layer`, sixteen of them):
the part, and that part's four knobs, ENV and LFO, copied when the take ends (REC, the loop coming
round from its first take, PLAY, another instrument, step-write off). A loop event and a step note
keep their layer. While the take goes on the layer sounds as the instrument does; once it is closed
its voices are a group of their own in `block()` (key: the part, with the layer above it), on their
own LFO, envelope rates and knobs (`lay_set`). A layer no note is on any more is let go when a new one
needs its slot. Notes with no layer (recorded before 0.2.2) follow their part as it stands, as before.

Every note carries a velocity: on the FM-1 the one its key landed at (fm1-base's sensing, `APP_TOUCH`), MIDI
what it was sent with, and 127 wherever none is given: `h1_key`, a key pressed from the computer over
`fm1_remote.py`, and in the simulator a key with no `key_vel` (the computer's keys, a RemoteFM1 FM-1 that sends
none). A key clicked in the simulator has the velocity of where it was clicked. A voice turns it into a gain
(about 20 dB from 1 to 127) and a two-pole low-pass in the mix (open at 127), so a note at 127 sounds as the
keys always did, to the bit. A voice taken over while it still sounds starts the new note from where the
old one was and lets the difference go over a millisecond or two (`Voice.tail`).

A note played by hand can also be leant on. The main loop writes a byte a note (`eng_pressure`: the panel's
keys through `h1_key_pressure`, MIDI poly aftertouch) and one for the channel (MIDI channel pressure), and
`press_block` reads them once a control block, before any instrument's `block` or `render`: each voice a hand
started (`Voice.hand`) eases to its note's pressure, or the channel's if that is harder, over about 20 ms, and
`Voice.press` is what its `render` and the picture (`VVoice.press`) see. `Blk.press` and `Vis.press` are the
hardest lean among the instrument's notes. The loop's notes, the steps' and the arpeggiator's are under no
finger and have none. An instrument may route the lean onto one of its home knobs as the LFO goes onto its
target (`InstParams.mix[IP_PTO]` the knob, `mix[IP_PDEPTH]` how far a full lean turns it, -100..100: two bytes
that were spare, so an older state leans on nothing); none does by default. `push_hold` and `push_move`
(`dsp.h`) turn a lean into a value that stays where it was pushed. On the device the lean is h1's own reading
of the keys (`app/keys.h`, which holds a steady lean, at rates in time so a slow frame reads it the same) or
fm1-base's (which fades back over seconds): CC 118 picks. A key's aftertouch goes back to 0 as it is let go,
and one the port could not send (`h1_midi_out_dropped`) is sent again. `app/keys.h` also lets go of a sensed key
whose release was lost from fm1-base's event queue, once the plain key scan has seen it open for 50 ms.
`tests/touch.c` and `tests/keys.c` hold all of it.

At the end of the mix a limiter with one control block of look-ahead keeps everything under 0.7
(`lim_block`): six keys at once on most instruments came to two to five times that, and the soft clip
that follows used to bend every peak. Under 0.7 it does nothing.

MIDI out (`h1_midi_out`): what the keys, the arpeggiator and the sequencer play, on channel 1, poly
aftertouch as the keys are leant on (not with the arpeggiator on; a move of two or more at most every 20 ms a
key, a move of one once it has stood 200 ms), and CC 20-23 as the home knobs turn. Never a note or a CC 123 that came in over MIDI, so a computer that
echoes its input does not loop. A note-on or a knob goes out only while the USB queue has room to spare
(app.c), so a full queue never leaves a note-off out. The device sends it over USB; the FM-1's TRS jack is an input only.

The saved state is version 9 (3344 bytes; 7 was never released). It has room for 16 instruments
(`NSLOT`) of nine algorithms each, and keeps in `ninst` how many instruments it was saved with: on a
load, the slots from there on come up as new, so adding an instrument (TILT and PLUNK) needs no new
version; they are last in PRESETS so that an instrument's number, which a recorded note's part holds,
never changed. Every older state is of fourteen instruments of eight algorithms (`H1Old`, read by offsets
only): each of its tables goes to the start of version 9's, its parts are read the old way, HOOVER's
knobs and the instruments past the fourteen come up as new. Version 8 (3160 bytes) is version 6 and,
after the knob table, each step note's velocity and layer and the sixteen layers. A loop saved by 0.2.2 is loop version 1, read the same way; h1 now writes version 2. A version 6 state (1880 bytes)
loads with its steps at full velocity on no layer, and LAYERS on. Versions 4 and 5 are the same up to the steps, where
a step held four notes and not eight, and they have no table of knobs (an instrument's algorithms
shared four). Version 4 (664 bytes) has no parts for its step notes; version 5 (920 bytes) has.
`h1_state_load` takes all of them, straight from the bytes (`H1Old` in `internal.h` is the old layout): a version 4 state comes up with LAYERS
on (since 0.2.2) and no parts kept, and in both older ones every algorithm starts from where the instrument's
knobs stood, so nothing sounds different after an update. A step note with no part kept belongs
to whichever instrument is in hand. An instrument's volume took a spare byte of version 6, so a
v0.1.0 state loads as it is, at full, and a v0.1.0 firmware reads a newer state without minding.

## Controls as values

`h1_control` reports what each encoder currently means (its name, range, value and value text)
and `h1_control_set` sets it outright. On the device, `app/app.c` reads `h1_control` to tell a
screen reader a control's name and value. The simulator reaches both through the rfm1 port's sim1
exports to
show each control as a labelled slider, while its nudge buttons, the mouse wheel and the number
keys still send real encoder detents.

## The recordings

BARD and TWANG are recordings of real instruments, all CC0 (`docs/SAMPLES.md` lists each one, where it
came from and its licence). `tools/samples/build.py` fetches them, cuts them and writes
`core/smp_data.c`: 162 KB of 8-bit mu-law at 22.05 kHz, 56 cuts in 16 banks. It needs ffmpeg, numpy and
scipy, and only runs when a recording changes; the firmware build just compiles its output.

A plucked instrument is not played back as a recording, which would cost a second or more of storage a
note. The script fits the string model of `core/str.h` (a delay line with a loss filter in its loop) to
each recorded note's decay, then runs the note's first tenth of a second through that loop backwards.
What is left is what the string was fed: the pluck and the body's answer. The firmware feeds that to
the same string, played faster or slower for the notes between two recordings, and the note comes out
with the recording's attack and a decay the model makes. `tools/samples/dsp.py` holds the script's copy
of the loop: change `str.h` and it has to change too.

A held instrument (the bowed ones, the reeds, the organ, the drone) is a recorded start and a stretch
that repeats, a whole number of periods long and crossfaded into itself.

Every cut is stored with its highs lifted and the player (`smp_read`) lowers them again, which puts the
8 bits' noise under the sound instead of over it.

PLUNK's kits are another matter: 33 single hits (`sources.DRUMS`, all from VCSL), cut by
`tools/samples/drums.py` into `core/plunk_data.c`, 25.7 KB. A drum is short and can take some grit, so
they are 4-bit IMA ADPCM, each at the lowest rate its top allows (5.5 to 22.05 kHz), a tenth of a second
or so. The hit's own dying away is divided out before it is stored and the player multiplies it back,
longer or shorter with DECAY; a ringing hit (gong, bells, cymbal) keeps a short stretch that repeats
while the player lets it fade. Five kits are picks and tunings from that pool, and three more are
treatments of it (slowed, small, shuffled): eight separate sampled kits would not have fitted the flash.

## RAM and flash on the device

As the build prints them (CI's one-core build of what became v0.2.0; v0.1.0's image was 304,848 bytes,
and the recordings are 162,048 of the difference):

| | Bytes | Of |
|---|---|---|
| App image | 526,284 | the 581,564-byte app slot, with 55,280 free |
| RAM code | 5,100 | |
| `.data` + `.bss` | 55,944 | |
| Pool | 355,172 | |
| SRAM in all | 416,220 | 475,136, with 58,916 free (the build keeps 8,192 spare) |

`app/fm1.toml` sets `ram_layout = "packed"`: RAM code, `.data`/`.bss` and the pool share one
budget. fm1-base's default map gives the pool a fixed 336 KB, which h1 overflows. `app/app.h`
also cuts fm1-base's own canvas to a 48-row strip (23 KB, down from 69 KB), since h1 draws with
its own framebuffer and the platform only needs the strip for its update and safe-mode text.

What h1 puts in the pool, about 321 KB:

| Buffer | KB |
|---|---|
| Framebuffer and the copy of what the panel shows | 115 |
| String delay lines (8 voices, 2 each) | 66 |
| Echo | 64 |
| Reverb | 48 |
| Two RGB565 bands for the panel | 19 |
| LAVA's field and INK's scroll history | 7 |

These are link-time numbers. Stack depth has not been measured on hardware. Frame and audio time
can be: send MIDI CC 119 to the FM-1 while a screen reader is listening (`base/tools/fm1_remote.py
listen`) and it says `PERF. FPS n. DRAW avg MAX max. SEND avg MAX max. AUDIO avg MAX max. KB n RECTS n.
STACK main interrupt`: the times in milliseconds a frame, the audio as a percentage of each block, the
kilobytes and rectangles sent to the panel a frame, all since it was last asked, and the bytes of each
stack never used since the start. A build with the second core (the default) adds `CORES 2 n` or `CORES 1 why`. If RAM gets tight again, the first things to shrink are the string lines and the reverb
(16-bit samples halve both).

## What the FM-1 does not forgive

A computer shrugs at things that stop the FM-1's CPU, and a start that stops twice in a row is safe
mode. The first h1 did exactly that: it read its saved state's magic as a word straight out of a byte
buffer, the buffer sat at an odd address, and from the first save on it never started again. The rules
that came out of it:

- Bytes from outside (a saved state) are copied out a byte at a time before anything reads them.
  `h1_state_load` takes a blob at any address, and clamps every value it keeps.
- Nothing is read or written a word at a time through a byte pointer. A byte array sits wherever the
  linker has room, odd addresses included: what is cleared or compared by words (the framebuffer) is
  declared as words. `tools/build_host.sh` makes a cast that raises alignment an error; a cast through
  `void *` hides from it, so there are none.
- A number that came from a float is masked or clamped before it indexes anything (`tsin`, the string
  and echo lines), and a loop that runs on a float has a bound.
- A counter that runs for as long as the unit is on stays a whole number and is never turned into a
  float: a float keeps 24 bits, so the fraction is gone after a few minutes. ECHO's read position did
  that until 0.3.0 and the echo went grainy the longer the unit was on (`tests/echo.c`).
- A rectangle sent to the panel is at most 20 rows: the device converts it in a buffer that size.
- After a start that failed, `app_init` leaves the saved state alone and comes up as new, so one bad
  start cannot become two.
- `tests/fuzz.c` holds all of this to account: any saved state from an odd address, any panel input
  on every instrument, and the sound on a second thread, under `-fsanitize=address,undefined`.
- `app/fm1.toml` keeps the crash history in flash (`base/tools/fm1_remote.py crashes`), and the stacks
  are painted at every start so the PERF readout can say how much of each was never used.

## Building

- `tools/build_host.sh` then `build/h1host OUTDIR script...` for images and WAVs.
- `tools/build_rfm1.sh [VERSION]` for `dist/h1.rfm1` (clang with the wasm32 target and wasm-ld). Run it in the
  [FM-1 browser simulator](https://github.com/jakehandy/fm1-browser-simulator): open the page and drop the file
  on it, or run `tools/update-h1.sh` there to make it the built-in firmware.
- `./build.sh` builds the device package: fm1-base's `tools/build.py` with `app/` as the app.
- `sh tests/run_tests.sh` runs the device app in fm1-base's host simulator.
- `sh tests/run_fuzz.sh [SEEDS]` runs the crash hunt.
