# PCM1704 DAC — DF1704 Configuration

## MCU
ATtiny1614 (U7 on the `Kicadprojects/PCM1704` board)

## Framework
Arduino with megaTinyCore

## Purpose
Configure the DF1704 digital filter at power-up, then sleep. The DF1704 MODE
pin is tied high (software mode), and its reset defaults are 16-bit input and
16-bit output. Without this firmware, the DAC does not play correctly.

## Pin map
Taken from `PCM1704.kicad_sch`.

- PA4 -> DF_RST, DF1704 pin 14 (R75 10k pull-down)
- PA3 -> ML, DF1704 pin 13 (R76 10k pull-up)
- PA2 -> MC, DF1704 pin 12 (R77 47k pull-down)
- PA1 -> MD, DF1704 pin 11 (R78 47k pull-down)
- PA0 -> UPDI, J3 pin 2 (J3: 1 = +5VDF, 2 = UPDI, 3 = GND)
- PA5–PA7, PB0–PB3 -> not connected (internal pull-ups enabled)

## Behaviour
1. Hold DF_RST low, ML high and MC/MD low for 500 ms while the rails and MCLK settle.
2. Release DF_RST and wait 10 ms (the DF1704 needs 1024 XTI clocks).
3. Write MODE0–MODE3.
4. Enter power-down sleep. The RTC periodic interrupt (PIT, 1.024 kHz ULP
   oscillator) wakes the CPU once a second to write MODE0–MODE3 again with the
   same values (about 0.4 ms of activity), then it sleeps again.

### Why the periodic rewrite (v1.1.0)
The DF1704's reset only completes after 1024 MCLK cycles (SBAS099A Figs. 2–3).
If the DAC powers up before the source sends MCLK, the DF1704 stays in
internal reset and ignores the register writes. When MCLK arrives it starts
on its 16-bit defaults, giving no usable output. Bench-confirmed 2026-11-24 on
v1.0.0: powering the DAC before the I2S source needed a DAC power cycle.

The MCU cannot see MCLK, so v1.1.0 rewrites the registers every second. Sound
starts within about 1 s of MCLK appearing, in any power-up order. Change
`REWRITE_PERIOD` to adjust the interval (0.5, 1, 2 or 4 s).

## DF1704 register settings
From TI SBAS099A, Figures 8–10 and Table V. Words are 16-bit, MSB first: MD is
clocked on each MC rising edge, and the word latches on the rising edge of an
ML low pulse after B0.

| Register | Word | Setting |
|---|---|---|
| MODE0 | `0x01FF` | Left attenuation 0 dB (LDL = 1) |
| MODE1 | `0x03FF` | Right attenuation 0 dB (LDR = 1) |
| MODE2 | `0x0469` | 24-bit output, 24-bit I2S input, de-emphasis off, soft mute off |
| MODE3 | `0x0601` | I2S format, LRP = 0, independent attenuation, sharp roll-off, CLKO = XTI |

If left and right come out swapped on the bench, change `DF_MODE3` to `0x0603` (LRP = 1).

## Build / upload notes
- Board: **ATtiny1614** in megaTinyCore. Use `PCM1704.ino`; no libraries are needed.
- Clock: **10 MHz internal or lower.** 20 MHz needs 4.5 V or more, which is above the BOD threshold below.
- BOD: **highest level (4.2/4.3 V), Enabled in both active and sleep modes.**
  Continuous BOD needs no sampling clock in sleep. A dip on +5VDF resets the MCU,
  which resets and reconfigures the DF1704.
- Run **Burn Bootloader** once over UPDI so the BOD fuses are written. Then upload over UPDI on J3.
- millis() can be left at its default; the CPU sleeps in power-down.

## Status
- v1.0.0: bench-tested working when MCLK is present at power-up. Fails if the
  DAC powers up before the source (needs a power cycle).
- v1.1.0: compiles cleanly with avr-gcc 7.3 for ATtiny1614 (492 bytes,
  standalone build at 3.33 MHz). Bench-tested 2026-11-24: sound starts in
  either power-up order with no power cycle.
