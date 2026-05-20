# PreAmpv2

Version: 0.3.15

Basic ATtiny1616 preamp controller firmware for Arduino IDE (megaTinyCore), focused on stable input relay selection, PGA2310 volume control, and 16x2 I2C LCD status.

## MCU / framework
- MCU: ATtiny1616
- Framework: Arduino (megaTinyCore)
- Logic rail: 3.3V

## Pin map
- `PB0`  - Relay 1 (`DAC`)
- `PC0`  - Relay 2 (`AUX 1`)
- `PC1`  - Relay 3 (`AUX 2`)
- `PC2`  - Relay 4 (`PHONO`)
- `PC3`  - Relay 5 (`OUTPUT`)
- `PB5`  - Motor drive IN1 (clockwise for volume up)
- `PB4`  - Motor drive IN2 (anti-clockwise for volume down)
- `PA1`  - I2C SDA
- `PA2`  - I2C SCL
- `PA6`  - TSOP2438 IR input
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
- Midpoint boundary decoding with sample debounce to reduce relay chatter.
- Exactly one input relay is energized at a time.
- Output relay (`PC3`) remains OFF on startup and enables after 1 second.
- PGA2310 stereo volume control from `PB1` potentiometer ADC.
- Volume command and display are capped at `+10.0 dB`.
- 16x2 LCD displays active input and actual dB sent to PGA.
- Apple IR volume control on `PA6`:
  - Protocol: `Apple`
  - Address: `0xAA`
  - Volume up command: `0x0B` (raw reference `0xAA0B87EE`)
  - Volume down command: `0x0D` (raw reference `0xAA0D87EE`)
  - Short press steps volume once.
  - Held button keeps the motor moving while valid repeat frames continue.
  - Unrelated commands are ignored.
- Motorized potentiometer drive on `PB5`/`PB4`:
  - Volume up drives clockwise (`PB5=HIGH`, `PB4=LOW`)
  - Volume down drives anti-clockwise (`PB5=LOW`, `PB4=HIGH`)
  - Motor stop is enforced by deadband target, held-button repeat timeout, short-step runtime timeout, and ADC end-stop checks.

## LCD library choice
This sketch uses `LiquidCrystal_I2C`.

I2C pins are explicitly forced in code with:

```cpp
Wire.pins(PIN_PA1, PIN_PA2);
```


## LCD diagnostics mode (optional, disabled by default)
- `PREAMPV2_LCD_DEBUG` defaults to `0` in this version. Set it to `1` when you want hardware diagnostics on the LCD.
- The LCD alternates once per second between two test pages:
  - **Page A (input/relay):** selected input (`S:`), decoded candidate (`C:`), raw input ADC (`Axxx`), and relay states (`R:1234 OUT:x`).
  - **Page B (volume):** raw volume ADC (`VOL:`), estimated input voltage (`x.xxV`), actual applied dB (`DB:`), and PGA code (`C:`).
- Relay state legend for `R:1234` is: DAC, AUX1, AUX2, PHONO (`1` = active output state, `0` = inactive output state).
- Set `PREAMPV2_LCD_DEBUG` to `0` to return to normal two-line user display mode.

- Normal (non-debug) LCD dB rendering avoids float `snprintf` and uses fixed-point formatting for reliable AVR display updates.
- Normal display centering avoids formatted width specifiers, improving compatibility on constrained AVR `printf` builds.
- Normal display updates only changed LCD character positions instead of blanking and redrawing whole rows, reducing visible flicker during remote volume changes.

## Latest hardware calibration
- Input ladder changeover thresholds were retuned from real measurements:
  - DAC: `167`
  - AUX 1: `364`
  - AUX 2: `608`
  - PHONO: `839`
- Midpoint boundaries now use:
  - `INPUT_BOUNDARY_1 = 266`
  - `INPUT_BOUNDARY_2 = 486`
  - `INPUT_BOUNDARY_3 = 724`

## AVR-safe diagnostics formatting
- The LCD diagnostics no longer rely on `%f` formatting in `snprintf` for voltage/dB output.
- Voltage is displayed from integer millivolt math, and dB is rendered from PGA code in fixed-point tenths.
- This avoids float-format limitations that can show `?` on AVR builds without float `printf` support.

## Default LCD display
- With `PREAMPV2_LCD_DEBUG = 0`, the display shows:
  - Top row: selected input (centered)
  - Bottom row: volume in dB (centered)
  - No additional labels/text

## Volume mapping strategy
- Pot ADC (`0..1023`) uses a 3-segment taper to improve low/mid listening control with a linear pot.
- Result is quantized to PGA2310 half-dB steps (device-native coding).
- Firmware displays the **actual quantized dB value** sent to the PGA2310.

## Arduino IDE dependencies
Install these libraries:
- `LiquidCrystal_I2C`

`Wire` and `Arduino` are from the core/toolchain.
