# Changelog

## 0.3.0

Identity `FM-1_481000300`. Fixes from the GitHub issues so far; PLINK and VIBE join it.

**USB**
- Hold OCT− and OCT+ for a second: the USB port plays host to a USB-MIDI keyboard (#23), which needs its own
  power or a powered hub. The screen and the screen reader name it when it connects. Hold again to go back
  to the computer. Never saved: every start is a USB device.

**Room**
- fm1-base's system fonts (the update, crash and safe-mode screens) are stored trimmed and compressed:
  the same pixels in far less flash, which makes room for new instruments. Text on those screens draws a
  little slower.

**Loop**
- A loop that is still playing is kept through a power-off (#39). It used to be saved only once it had been
  stopped with PLAY, because a save erases flash and that stalls the sound; a loop left playing was lost.
  It now goes into flash erased ahead of time (at start, or while nothing sounds), a little at a time while it
  plays, so nothing is heard. A power-off in the middle of a save leaves the loop before it.

**Sound**
- LAVA has a ninth algorithm, **HOOVER** (#24): two PWM saws (a saw plus a copy of it swept along, the old
  Juno trick) a little apart, a swoop up into every note, and a light fold. FOLD sets how far the sweep goes
  and LEAN how far apart the two saws are. Its lamp turns into a whirl that twists harder the further it
  sweeps. Every other instrument keeps eight.
- The saved state is version 9 and a saved loop version 2, so a recorded note can name a ninth algorithm.
  Version 9 has room for sixteen instruments and says how many it was saved with, so instruments added
  later come up as new without another change of format.
  States and loops saved by 0.2.2 load as they were; an older firmware refuses a version 9 state and
  starts fresh.
- EDIT + MASTER no longer turns everything else up (#38). While MASTER was the instrument's volume,
  everything's volume was held at a level worked out on a different curve from the knob's, so the other
  instruments and the loop's layers jumped louder (at half way, by about 12 dB) until the knob came back
  through it. The held volume is now the knob's own, and it is held from the moment EDIT goes down, so a
  nudge too small to count does not move it either.
- ECHO no longer turns grainy the longer the unit is on (#31). Its read position was worked out in a
  float from a counter that never stops, so after about 13 minutes it lost the fraction of a sample,
  then whole samples, and only a restart brought it back. It now sounds the same after hours as at
  start.

## 0.2.2

Identity `FM-1_481000202`. From the first round of feedback after the launch.

**Sound**
- No more crackle or fuzz on loud chords. Six keys at once on most instruments came to two to five
  times full scale, and the soft clip at the end bent every peak. A limiter with 0.7 ms of look-ahead
  now holds the mix under that point and lets go over about 100 ms; a note or two at an ordinary level
  goes through untouched. Loud chords are a little quieter than before, and clean.
- A voice taken over while it still sounds (fast runs on eight voices) no longer jumps: the new note
  starts where the old one was.
- COZY's DUST pops are half as loud. They were the loudest thing in the mix.

**Velocity**
- Notes from a MIDI keyboard (USB or TRS) play at their velocity: about 20 dB from the softest to the
  hardest, and darker the softer they are. The FM-1's own keys play at full, as before.
- The loop and the step sequencer record the velocity and play it back.

**Layers**
- LAYERS is on by default, and comes on with the update. Record a loop, change instrument, press REC
  and play over it: that is a new layer. SEQ + REC still turns it off.
- Each layer keeps its own settings: the instrument, the algorithm, the four knobs, ENV and LFO, as
  they were when its take ended. Record a bass on COZY, turn the same COZY into a pad and record over
  it: the bass stays a bass. (The effects are still one chain, following the instrument in hand.)
- The screen says so: changing instrument over a loop shows `REC: NEW LAYER`, a take shows
  `LAYER 2`, `LAYER 3`, and a dot for each layer sits by the loop's progress line.

**The loop survives a power-off**
- The loop and its layers are saved to flash once the loop is stopped and nothing sounds (stop it with
  PLAY), and come back after a restart. They go in a sector pair of their own (0xDE000) through the
  platform's storage, which knows both flash chips (P25Q80 and UC25HQ80); the boot loader and the
  panel's calibration are not touched.

**USB audio and MIDI out**
- The FM-1 is a USB audio input beside its USB MIDI: a computer or a phone records it straight off the
  cable, at MASTER.
- MIDI out over USB: the keys, the arpeggiator and the sequencer's notes on channel 1, and CC 20-23 as
  the four knobs turn. Notes and an all-notes-off (CC 123) that came in over MIDI are not sent back.
  (The FM-1's TRS jack is an input only, so there is no MIDI out there.)

**Under the hood**
- Saved state version 8. A 0.2.0 or 0.2.1 state (version 6) loads as it was, with LAYERS turned on. A
  0.2.1 or older firmware does not read a version 8 state: going back loses the settings.

## 0.2.1

EDIT held and MASTER: the instrument's volume, and MASTER waits for the knob to come back. The CC0
recordings named in fmhub.json.

## 0.2.0

The graphics pass, BARD and TWANG on CC0 recordings, and each instrument's own volume (EDIT held and
SELECT).

## 0.1.0

The first release: fourteen instruments, the loop recorder and step sequencer, LAYERS, the
arpeggiator and the metronome.
