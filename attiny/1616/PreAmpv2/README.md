# PreAmpv2

Version: 0.2.4

Basic ATtiny1616 preamp controller firmware for Arduino IDE (megaTinyCore), focused on stable input relay selection, PGA2310 volume control, and 16x2 I2C LCD status.

## MCU / framework
- MCU: ATtiny1616
- Framework: Arduino (megaTinyCore)
- Logic rail: 3.3V
- Board option note: set `Tools -> Wire` to `PA1/PA2` to match this pin map

## Pin map
- `PB0`  - Relay 1 (`DAC`)
- `PC0`  - Relay 2 (`AUX 1`)
- `PC1`  - Relay 3 (`AUX 2`)
- `PC2`  - Relay 4 (`PHONO`)
- `PC3`  - Relay 5 (`OUTPUT`)
- `PB5`  - Motor 1 (placeholder only)
- `PB4`  - Motor 2 (placeholder only)
- `PA1`  - I2C SDA
- `PA2`  - I2C SCL
- `PA6`  - IR input (placeholder only)
- `PA4`  - PGA2310 MUTE
- `PA5`  - PGA2310 SDI
- `PB3`  - PGA2310 SCLK
- `PA3`  - PGA2310 CS
- `PB1`  - Volume ADC input (`VOL IN`)
- `PA7`  - Input select ADC input (`INPUT IN`)

## Implemented behavior
- 4-way input select from resistor ladder on `PA7`:
  - ~0.58V -> DAC
  - ~1.21V -> AUX 1
  - ~1.98V -> AUX 2
  - ~2.75V -> PHONO
- Midpoint boundary decoding with ADC hysteresis + sample debounce to reduce relay chatter.
- Exactly one input relay is energized at a time.
- Output relay (`PC3`) remains OFF on startup and enables after 1 second.
- PGA2310 stereo volume control from `PB1` potentiometer ADC.
- Volume command and display are capped at `+10.0 dB`.
- 16x2 LCD displays active input and actual dB sent to PGA.

## LCD library choice
This sketch uses **Bill Perry's `hd44780` library** (`hd44780_I2Cexp` I/O class) instead of many `LiquidCrystal_I2C` forks, because it is typically more reliable and portable across Arduino cores (including megaavr / megaTinyCore).

## Volume mapping strategy
- Pot ADC (`0..1023`) uses a 3-segment taper to improve low/mid listening control with a linear pot.
- Result is quantized to PGA2310 half-dB steps (device-native coding).
- Firmware displays the **actual quantized dB value** sent to the PGA2310.

## Not implemented yet (placeholders only)
- IR control logic on `PA6`
- Motorized potentiometer control behavior on `PB5`/`PB4`

## Arduino IDE dependencies
Install these libraries:
- `hd44780` by Bill Perry (for `hd44780_I2Cexp`)

`Wire` and `Arduino` are from the core/toolchain.
