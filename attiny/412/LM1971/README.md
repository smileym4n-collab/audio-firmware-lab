# LM1971 Volume Control

## MCU
ATtiny412

## Framework
Arduino with megaTinyCore

## Purpose
Single-channel or stereo LM1971 volume control using an ATtiny412.

## Pin map
- PA0 -> UPDI header
- PA1 -> Potentiometer wiper
- PA2 -> LM1971 DATA
- PA3 -> LM1971 CLOCK
- PA6 -> LM1971 LOAD

## Electrical assumptions
- LM1971 logic powered from 5 V
- ATtiny412 powered from 5 V
- Push button(s) use internal pull-ups unless noted otherwise
- Pot wired between 5 V and GND, wiper to ADC pin

## Behaviour
- Power up muted
- Read pot and set attenuation
- Optional soft-start fade
- Button toggles mute

## Notes
- Preserve existing volume behaviour unless explicitly asked to change it
- Keep code simple and compile-ready
