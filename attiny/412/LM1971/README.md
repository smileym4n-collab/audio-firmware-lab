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
- PA6 -> LM1971 LOAD

## Behaviour
- Startup writes mute for a conservative safe state.
- Potentiometer position is read continuously.
- Pot near minimum sends mute.
- Remaining range maps to LM1971 attenuation values.

## Assumptions
- Pot ends are connected to VCC and GND.
- LM1971 and ATtiny412 logic levels are compatible.
- LM1971 commands used by this sketch:
  - `0x00` = 0 dB (max volume)
  - `0x4F` = -79 dB (minimum non-mute)
  - `0x50` = mute

## Build/upload
- Board package: megaTinyCore
- Board: ATtiny412
- Upload method: UPDI
