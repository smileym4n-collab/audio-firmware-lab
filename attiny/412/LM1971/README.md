# LM1971 Volume Control

## MCU
ATtiny412

## Framework
Arduino with megaTinyCore

## Purpose
Control an LM1971 volume IC from a potentiometer.

## Pin map
- PA0 -> UPDI header
- PA1 -> Potentiometer wiper (ADC input)
- PA2 -> LM1971 DATA
- PA3 -> LM1971 CLOCK
- PA6 -> LM1971 LOAD (active-low shift enable / latch)

## Behaviour
- Startup writes mute for a conservative safe state.
- Startup then applies the current potentiometer position immediately.
- Startup waits briefly for supply/reference settling and repeats initial LM1971 frames.
- Potentiometer position is read continuously.
- Potentiometer reads are lightly averaged to reduce startup and wiper noise.
- Pot near minimum sends mute.
- Remaining range maps to LM1971 attenuation values.
- The current LM1971 setting is resent periodically so a missed startup frame does not leave the chip muted.

## Assumptions
- Pot ends are connected to VCC and GND.
- LM1971 and ATtiny412 logic levels are compatible.
- LM1971 supply and reference rails have settled before the first unmute command is needed.
- LM1971 commands used by this sketch:
  - `0x00` = 0 dB (max volume)
  - `0x3E` = -62 dB (minimum non-mute)
  - `0x3F` and above = mute
- Each LM1971 update sends a 16-bit serial transfer:
  - address byte `0x00`
  - attenuation byte `0x00` to `0x3F`
- LM1971 LOAD idles high and is pulled low only while clocking the 16-bit transfer.

## Build/upload
- Board package: megaTinyCore
- Board: ATtiny412
- Upload method: UPDI
