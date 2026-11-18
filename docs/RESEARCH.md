# What the FM-1 is, and what makes firmware for it good

Researched November 2026 from the community's reverse-engineering work: `cicloid/awesome-fm-1`,
`aroum/fm1-custom-fw`, `hugelton/felucca-bsp`, `charlesvestal/fm1-base`, `zvenson/dinghy` and the
firmwares built on them.

## The hardware

| | |
|---|---|
| Chip | JieLi AC7911B (AC791N family), two pi32v2 cores at 320 to 360 MHz |
| Maths | single-precision FPU only: no doubles, no libm |
| Flash | 1 MB, about 581 KB for the app, one bank |
| RAM | 96 KB data and bss, plus a 336 KB pool for big buffers |
| Screen | ST7789V, 240 x 240 IPS, RGB565 over SPI, 60 Hz, no tearing-effect line |
| Panel | 27 keys (F3 to G5), 7 detented encoders, MASTER pot, 14 buttons, LEDs |
| Audio | 24-bit I2S at about 44.1 kHz, 256-frame blocks (5.8 ms), speaker, jack, USB audio |
| MIDI | TRS in, USB both ways, Bluetooth on some platforms |

The screen link is the number that decides smoothness. A full frame is 115 KB. fm1-base drives
SPI at 60 MHz (about 44 Mbit/s measured), so a full repaint takes about 21 ms, longer than one
60 Hz refresh. The older 12 MHz setup takes 77 ms. So a smooth firmware never repaints the whole
screen: it sends only what changed.

## Flashing and safety

Firmware goes in over USB-MIDI SysEx as an unsigned package, from a web page in Chrome or Edge.
There is one flash bank and no debug header. Recovery is holding OCT- at power-on, then the
chip's mask-ROM USB mode, then an RP2040 dongle as the last resort. Units sold since autumn 2026
carry a different flash chip (UC25HQ80 instead of Puya P25Q80H), and a platform has to know both
or saving and updating fail. Custom firmwares expect stock V15 underneath.

## What the good firmwares do

**Architecture.** The best ones split a thin hardware layer from a portable app, and run the app
on a computer as well as the device. fm1-base is the cleanest example: one `plat.h`, a host
simulator fed by scripts, a browser build, tests. Audio runs in the interrupt, the UI in the main
loop, with small lock-free hand-offs between them. Big buffers live in the pool RAM. Saves wait
for silence because a flash erase stalls the sound for about 50 ms.

**Sound.** Float DSP at -O2 with the FPU switched on, band-limited oscillators, parameters
smoothed per block, and a soft limiter before the DAC. The weak ones alias, zipper when a knob
turns, and click when voices are stolen.

**Smoothness.** Draw only what changed, in strips, and let the last strip leave by DMA while the
next frame is computed. Firmwares that redraw whole screens sit at 12 to 15 frames a second.
TripDeck, the closest thing to H1's idea, renders its plasma at 120 x 120 and gets about 12.

**Chip traps the community found.** Inline 64-bit multiplies next to 16-bit loads corrupt
registers; there is no 64-bit divide; the watchdog bites at 8 seconds.

## Decisions for H1

- Build on fm1-base (MPL-2.0, and the app may carry any licence). Copy no GPL code.
- One freestanding C99 core, no libc, compiled three ways: browser, host tests, device.
- An 8-bit ink framebuffer and a per-instrument palette on true black, with dirty bands sent to
  the panel, so a frame costs only what moved.
