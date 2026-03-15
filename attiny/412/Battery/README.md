# Battery undervoltage LED warning (ATtiny412)

## Summary
Simple Arduino/megaTinyCore sketch for ATtiny412 that monitors battery voltage through a resistor divider and blinks an LED with PWM when battery voltage drops below **12.0 V**.

## MCU / framework
- MCU: ATtiny412
- Framework: Arduino (megaTinyCore)

## Pin map
- `PA3` -> LED cathode (active LOW output, PWM capable)
- `PA6` -> Battery sense ADC input from divider

## Hardware assumptions
- Divider: `270k` from battery+ to `PA6`, `47k` from `PA6` to GND
- Battery full voltage around `16.8 V`
- ADC reference assumed to be `Vcc = 5.0 V` (default `analogRead` reference in this sketch)
- LED is wired to sink current on PA3 (anode to Vcc through resistor)

## Behaviour
- Samples battery sense input continuously.
- Uses a small sample-confirmation filter to avoid threshold chatter.
- If battery voltage is below `12.0 V`, LED on `PA3` blinks with PWM brightness.
- If battery voltage is at/above threshold, LED stays off.

## Build / upload notes
- Select **ATtiny412** in Arduino IDE with megaTinyCore installed.
- Use `Battery.ino`.
- No external libraries required.
