# Changelog

## Unreleased (fork: Bluetooth MIDI)

- Bluetooth MIDI in and out with a computer or a phone, on fm1-base's own Bluetooth stack (MPL-2.0, no
  JieLi libraries). MIDI out goes to USB and Bluetooth, each with its own queue; notes that came in over
  MIDI are still never sent back. The radio's address is kept in a new store object (0xDA000).
- fm1-base's own fonts (only on its update, crash and safe-mode screens) stored trimmed and packed: the
  same pixels in about 17 KB instead of 61, the room Bluetooth needs.
- `./build.sh --no-ble` still builds the USB-only firmware (h1-usb.fwsc).
- The MIDI page, behind HOME held: CHAN (the out channel), SEND (ALL, LIVE, OFF), CLOCK (OFF, ON, +START) and
  BT (Bluetooth on or off, kept through a power-off). It lights the HOME square in the page map, and SELECT
  reaches it after SEQ.
- MIDI clock out at GLOBAL's tempo, 24 a quarter note, straight, always running while CLOCK is on, so gear that
  only follows a clock (loopers, delay and tremolo pedals) has h1's tempo with nothing playing. +START adds
  Start and Stop with the loop and the steps. Clock, Start and Stop go ahead of notes in a busy queue.
- HUSH moves from HOME held to OCT- and OCT+ held together (a tap of both is still the middle octave), and its
  all-notes-off goes out on the page's channel.
- The page lives in five bytes the state kept spare (zero in every saved state so far), so there is no new
  state version: a 0.2.2 state loads as h1's MIDI was, and 0.2.2 reads a newer one, ignoring the page.

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
