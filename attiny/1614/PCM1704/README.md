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
4. Wait 1 s and write MODE0–MODE3 again. The MCU cannot see MCLK, so this is insurance.
5. Enter power-down sleep. Pin states are held, and nothing wakes the MCU.

The firmware assumes the XMOS board's MCLK is running when this board powers
up. Normally the XMOS board is always on and the DAC is switched on for
listening. If the DAC is ever powered more than about 1.5 s before MCLK
arrives, re-power the DAC.

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
Compiles cleanly with avr-gcc 7.3 for ATtiny1614: 422 bytes before the
megaTinyCore core, tested at 10 MHz and 20 MHz. Not yet tested on hardware.
