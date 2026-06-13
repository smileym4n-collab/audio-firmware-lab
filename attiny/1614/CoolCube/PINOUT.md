# CoolCube ATtiny1614 Pinout

## Used pins

- PA1 -> Volume pot wiper (ADC input)
- PA2 -> LM1971 DATA
- PA3 -> LM1971 CLOCK
- PA4 -> TDA7396 STANDBY control
- PA5 -> TDA7396 MUTE control
- PA6 -> LM1971 LOAD (active-low shift enable / latch)
- PA7 -> Battery sense divider (ADC input)

## Control logic

- PA4 / `AMP_STBY_CTRL` HIGH = standby active / amplifiers off
- PA4 / `AMP_STBY_CTRL` LOW = standby released / amplifiers on
- PA5 / `AMP_MUTE_CTRL` HIGH = mute active
- PA5 / `AMP_MUTE_CTRL` LOW = mute released

The firmware preloads PA4 and PA5 HIGH before enabling them as outputs, so the
amplifier starts in the safe off/muted state.

## Analogue inputs

- PA1 expects the volume pot ends at VCC and GND.
- PA1 ADC readings from 0..20 are treated as true LM1971 mute.
- PA7 reads `VBAT_IN` through a 220k / 47k divider with 100nF from the sense
  node to GND.
