# CoolCube LM1971 Volume and Amplifier Control

## MCU
ATtiny1614

## Framework
Arduino with megaTinyCore

## Purpose
Control an LM1971 volume IC from a potentiometer, with safe startup and
power-fail shutdown control for TDA7396 amplifier mute and standby lines.

## Pin map
Pin assignments are taken from `PINOUT.md`.

- PA1 -> Volume pot wiper (ADC input)
- PA2 -> LM1971 DATA
- PA3 -> LM1971 CLOCK
- PA4 -> TDA7396 standby control
- PA5 -> TDA7396 mute control
- PA6 -> LM1971 LOAD (active-low shift enable / latch)
- PA7 -> Battery sense divider (ADC input)

## Amplifier control logic
The TDA7396 control lines are active-high safe states:

- `AMP_STBY_CTRL` HIGH = standby active / amplifiers off
- `AMP_STBY_CTRL` LOW = standby released / amplifiers on
- `AMP_MUTE_CTRL` HIGH = mute active
- `AMP_MUTE_CTRL` LOW = mute released

The firmware drives both lines HIGH as early as possible during boot and any
power-fail condition.

## LM1971 behaviour
The LM1971 volume logic is copied from the ATtiny412 firmware as closely as
possible.

- Startup writes LM1971 mute for a conservative safe state.
- Startup then applies the current potentiometer position.
- Startup repeats initial LM1971 frames in case the IC is still settling.
- Potentiometer position is read continuously.
- Potentiometer reads are lightly averaged to reduce startup and wiper noise.
- Tiny ADC changes are ignored to avoid volume chatter.
- Pot readings from ADC 0..20 send true mute, covering end-stop tolerance and
  ADC/wiper noise at minimum.
- Remaining range maps to LM1971 attenuation values.
- The current LM1971 setting is resent periodically so a missed frame does not
  leave the chip in the wrong state.

LM1971 command assumptions:

- `0x00` = 0 dB (max volume)
- `0x3E` = -62 dB (minimum non-mute)
- `0x3F` and above = mute
- ADC `0..20` = mute
- ADC `21..1023` = mapped from `0x3E` to `0x00`
- Each update sends a 16-bit serial transfer:
  - address byte `0x00`
  - attenuation byte `0x00` to `0x3F`
- LM1971 LOAD idles high and is pulled low only while clocking the transfer.

## Startup sequence
1. Preload amplifier control outputs HIGH.
2. Configure amplifier standby and mute pins as outputs.
3. Keep standby and mute HIGH.
4. Configure LM1971 and ADC pins.
5. Wait about 800 ms for rails, DAC, VREF and analogue sections to settle.
6. Write LM1971 mute.
7. Read the pot and set the initial LM1971 level.
8. Release standby by driving `AMP_STBY_CTRL` LOW.
9. Wait about 150 ms.
10. Release mute by driving `AMP_MUTE_CTRL` LOW.

## Battery / power-fail sense
`VBAT_SENSE` is read through this divider:

- 220k from `VBAT_IN` to sense node
- 47k from sense node to GND
- 100nF from sense node to GND

The firmware assumes the ATtiny1614 runs from 5 V and uses the default
`analogRead()` reference. With the documented divider:

- 16.8 V battery is about 2.96 V at the ADC input, raw ADC about 605
- 12.0 V battery is about 2.11 V at the ADC input, raw ADC about 432
- power-fail threshold is about 10.5 V battery input, raw ADC about 378
- recovery/hysteresis threshold is about 11.5 V battery input, raw ADC about 414

Power-fail detection uses a small average and debounce so noise does not trip
shutdown during normal battery operation. Once detected, the fault is latched
until reset.

On power-fail:

1. `AMP_MUTE_CTRL` is driven HIGH immediately.
2. `AMP_STBY_CTRL` is driven HIGH.
3. Normal volume updates stop.
4. The amplifier remains in the safe off/muted state.

## Assumptions
- Pot ends are connected to VCC and GND.
- LM1971, TDA7396 control inputs and ATtiny1614 logic levels are compatible.
- `analogRead()` uses VCC as its reference.
- Battery sense measures the incoming `VBAT_IN` side before the amplifier bulk
  rail collapses.
- The 10.5 V power-fail threshold is intentionally below normal 12 V operation.

## Build/upload
- Board package: megaTinyCore
- Board: ATtiny1614
- Upload method: UPDI
