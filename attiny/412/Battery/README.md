# Battery divider LED status monitor (ATtiny412)

## Summary
Simple Arduino/megaTinyCore sketch for ATtiny412 that monitors the divided battery-sense voltage on `PA6` and drives a status LED on `PA3`.

## MCU / framework
- MCU: ATtiny412
- Framework: Arduino (megaTinyCore)

## Pin map
- `PA3` -> LED cathode (active LOW output, PWM capable)
- `PA6` -> Divider output ADC input

## Hardware assumptions
- `PA6` sees the divider-node voltage (not raw battery voltage)
- ADC reference assumed to be `Vcc = 5.0 V` (default `analogRead` reference in this sketch)
- LED is wired to sink current on `PA3` (anode to Vcc through resistor)

## Behaviour
- Samples `PA6` continuously.
- Uses sample-confirmation and small hysteresis to reduce threshold chatter.
- If `PA6` is at/below **1.75 V**, LED enters warning mode: PWM pulse with **2 s ON / 2 s OFF**.
- If `PA6` is at/below **1.60 V**, LED enters critical mode: **solid ON**.
- If `PA6` rises above thresholds (with hysteresis), mode steps back to warning or off.

## Build / upload notes
- Select **ATtiny412** in Arduino IDE with megaTinyCore installed.
- Use `Battery.ino`.
- No external libraries required.
