# Changelog

## 0.3.3

Identity `FM-1_481000303`. Fixes from the GitHub issues since 0.3.2.

**Loops and layers**
- A deleted loop no longer comes back (#71). The loop h1 kept before 0.3.0 was never emptied, and came back
  whenever the loop's own space held none (another firmware had it, say). It is emptied now, once the loop
  is safely in its new place.
- CLEAR ALL clears the layers too (#55, #71): the layer view shows only what is recorded, and a solo left on a
  cleared layer no longer silences the takes after it.
- The steps and the layers are saved with the loop, while it plays (#39, #71). They were saved only once
  nothing sounded, so a pattern written, or a layer recorded, while the loop or the steps played could be gone
  after a power-off, and the layer view could miss layers that still played. A loop saved by 0.3.2 whose
  layers were lost gets them back, at full and heard.
- With SNAP on, the arpeggiator's notes go into the loop on its own RATE (#49). They were snapped to 1/16,
  which broke triplets and 1/32s.

## 0.3.2

Identity `FM-1_481000302`.

**Screen**
- The frame rate is back on FM-1s with the newer flash chip. 0.3.1 sent the picture to the screen at 12 MHz
  on those units, against 60 on the others, and a busy picture took three times as long to send. SCREEN's
  AUTO is now the fast clock on every unit. If a screen shows noise or wrong colours, set SCREEN to SAFE on
  the SETUP page.

## 0.3.1

Identity `FM-1_481000301`. Fixes from the GitHub issues and Discord since 0.3.0, the layer view, MIDI extras
and a loop you can take out as a MIDI file. Saved states and loops from 0.3.0 load as they were; every new
setting starts off, so nothing changes until you turn it on.

**Screen**
- FM-1s from the newer production run have a different screen, and h1 showed it in the wrong colours. h1 now
  sends the screen the same setup the stock firmware does, at start, and runs the newer screen at a slower,
  safe clock. Older units look as before. **SCREEN** on the SETUP page picks the clock: AUTO (the default,
  by unit), FAST, or SAFE if a screen still shows noise. (0.3.2 makes AUTO the fast clock on every unit.)

**Crackles and dropouts**
- No more crackle at the start (#64), and no more dropouts with the key lights flickering (#58). Both came
  from flash erases, which stall the sound while they run. The loop's space is now erased a sector at a time,
  and only while nothing sounds, and the instrument in hand and its knobs save once nothing sounds (or
  after half a minute at most, or at once on SAVE).
- No more loud crackle after three or four looped instruments (#66). That was the sound taking longer than it
  has. When the sound runs out of time for a while, h1 now plays fewer voices at once, lets go of releasing
  notes faster, and gives the voices back once there is room. Nothing changes until the load is high.

**Fixes**
- A note no longer sticks after turning the arpeggiator off with keys held (#56).
- Layers of the same instrument are independent (#55, #52): a layer's note starts on that layer's own
  knobs, so turning TILT's BOUNCE (or any instrument's knobs) after a take no longer changes the drums
  already recorded. LAYERS on or off, the take keeps what it was recorded with.
- A USB-MIDI keyboard on USB host no longer says NOT MIDI when it is one (#54). fm1-base's host now waits for
  slow keyboards and reads every MIDI endpoint.
- USB audio: the FM-1 now gives the computer a serial number of its own (from its chip). Without one, a
  computer (macOS in particular) can go on treating it as whatever firmware was last on that USB port, and
  not see its audio input.
- EDIT on a settings page goes back to the home screen with the knob strip up (#49). The step lights show
  the pattern only on the SEQ page or while writing steps, unless LEDS on the SETUP page says SEQ.
- ORBIT's UFO now needs a key held for twenty seconds, and comes once in ten minutes at most (#62).

**Layer view**
- With LAYERS on, hold PLAY: the sixteen white keys are the sixteen layers, lit where a layer is heard (a soloed one
  blinks). Tap a
  key to mute or unmute that layer. Hold a key to hear only that layer (solo); hold it again for all of
  them. Hold a key and turn SELECT for that layer's volume. Hold a key and press REC to clear that layer.
  Mutes, solo and volumes are saved with the loop.
- With LAYERS on and the sequencer running, PLAY stops it on a quick tap with nothing else touched, so
  holding it for the layer view never stops the loop.

**MIDI**
- A new MIDI page (after SEQ, with SELECT): **CHAN** (ALL, or 1 to 16, in and out), **BEND** (OFF, 2 or 12
  semitones), **WHEEL** (the mod wheel deepens the live modulation) and **PEDAL** (a sustain pedal on
  CC 64). All off by default.
- KEYS' HOLD has a third setting, **ARP**: the keys latch only while the arpeggiator is on.
- MIDI out goes on CHAN's channel (channel 1 when it is ALL).
- The loop can be taken out as a MIDI file: a computer asks for it over USB-MIDI with a SysEx message and h1
  sends the loop back, one layer a track. `tools/h1_loop_mid.py` turns what comes back into a `.mid` file.
  SysEx, because it needs nothing new on the device (no USB storage) and works on any computer that talks
  MIDI.

**Sound**
- URCHIN, COZY's TINE and REED, and TURBO are brighter the harder they are played, as the other FM
  instruments are.
- LAVA's high notes fold less, so they no longer turn harsh and gritty.
- ORBIT's pulse and HAUNT's choir no longer jump at the end of each swell; HAUNT's bells and KNOT no
  longer alias on high notes.

**Setup**
- A new SETUP page (after MIDI): **ANIM** (OFF shows the instrument's name and algorithm instead of its
  picture), **EGGS** (OFF turns off everything hidden in the pictures), **SCREEN** (see above) and
  **LEDS**.
- Holding REC for three seconds to clear everything now asks first: the screen says `TAP REC: CLEAR ALL`,
  and only a tap of REC within three seconds clears it. Any other button keeps everything.

## 0.3.0

Identity `FM-1_481000300`. Two drum machines, and the fixes from the GitHub issues so far.

**Instruments**
- **TILT** (the fifteenth): electronic drum kits, 8-bit first, on a pinball table that is a drum kit. The
  kick is the flippers, the snare and toms the bumpers, the crash fires the plunger. Knobs TUNE, DECAY,
  BOUNCE, BITS.
- **PLUNK** (the sixteenth): drum kits of recorded hits (gong, bells, darbuka, congas, timpani, anvil) on a
  steel tongue drum in a garden in the rain. Knobs TUNE, DECAY, WIND, STORM.
- A few of the pictures hide something for whoever plays them a certain way, some with a sound of their own.

**USB**
- Hold OCT− and OCT+ for a second: the USB port plays host to a USB-MIDI keyboard (#23), which needs its own
  power or a powered hub. The screen and the screen reader name it when it connects. Hold again to go back
  to the computer. Never saved: every start is a USB device.

**Room**
- fm1-base's system fonts (the update, crash and safe-mode screens) are stored trimmed and compressed:
  the same pixels in far less flash, which makes room for new instruments. Text on those screens draws a
  little slower.

**Loop**
- Takes layered on the loop no longer go missing. The loop held 384 events (a press and a release a note),
  across every layer, and quietly dropped whatever came after: three light layers on eight bars filled it
  and the fourth was lost. It now holds 2,048 (1,024 notes), a note never goes in without room for its
  release, and when the loop is full the screen says **LOOP FULL** instead of dropping notes in silence.
- BARS goes up to **128** (1, 2, 4, 8, 16, 32, 64, 128). The bound on a loop is now its notes, not its bars:
  1,024 notes across all layers, so 128 bars holds eight notes a bar.
- A loop that is still playing is kept through a power-off (#39). It used to be saved only once it had been
  stopped with PLAY, because a save erases flash and that stalls the sound; a loop left playing was lost.
  It now goes into flash erased ahead of time (at start, or while nothing sounds), a little at a time while it
  plays, so nothing is heard. A power-off in the middle of a save leaves the loop before it.
  Saves go one after another into the space set aside, so a loop left playing has room for dozens of
  takes before it next stops; a big loop (over about 600 notes) has room for three. Once it stops, the
  space is cleared again.
  The instrument in hand and its knobs are saved once nothing sounds, as before.

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
- BARD's drone no longer cuts out when the playing stops. It went within about a tenth of a second of the
  last note dying away; it now holds for a little over a second (a rest between phrases keeps it going)
  and then dies away over a few seconds, the way a wheel runs down or a bag empties.
- ECHO no longer turns grainy the longer the unit is on (#31). Its read position was worked out in a
  float from a counter that never stops, so after about 13 minutes it lost the fraction of a sample,
  then whole samples, and only a restart brought it back. It now sounds the same after hours as at
  start.

**Name**
- h1 is also called handy-1. Listings read handy-1 (h1): `firmware.json` takes its name from a new
  `title` in `app/fm1.toml`. The id, the package's file names and the USB name stay `h1`.

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
