# PreAmpv2

Version: 0.3.27

Basic ATtiny1616 preamp controller firmware for Arduino IDE (megaTinyCore), focused on stable input relay selection, PGA2310 volume control, XU208 USB/I2S status monitoring, and 16x2 I2C LCD status.

## MCU / framework
- MCU: ATtiny1616
- Framework: Arduino (megaTinyCore)
- Logic rail: 3.3V
- Clock: 10 MHz internal (Arduino IDE: Tools -> Clock -> 10 MHz internal). The ATtiny1616 is rated to 10 MHz below 4.5V, so 20 MHz is out of spec at 3.3V. The sketch stops with a compile error if the clock is set higher than 10 MHz.

## Pin map
- `PB0`  - Relay 1 (`STREAMER`)
- `PC0`  - Relay 2 (`AUX 1`)
- `PC1`  - Relay 3 (`AUX 2`)
- `PC2`  - Relay 4 (`PHONO`)
- `PC3`  - Relay 5 (`OUTPUT`)
- `PB5`  - Motor drive IN1 (HIGH for volume down)
- `PB4`  - Motor drive IN2 (HIGH for volume up)
- `PA1`  - I2C SDA
- `PA2`  - I2C SCL
- `PA6`  - TSOP2438 IR input
- `PA4`  - PGA2310 MUTE
- `PA5`  - PGA2310 SDI
- `PB3`  - PGA2310 SCLK
- `PA3`  - PGA2310 CS
- `PB1`  - Volume ADC input (`VOL IN`)
- `PA7`  - Input select ADC input (`INPUT IN`)

## I2C devices
- LCD: `0x27`
- MCP23008 GPIO expander: `0x20`
  - IO0..IO3: XU208 sample-rate code
  - IO4: XU208 mute/status output, HIGH while changing sample rate
  - IO5..IO7: currently unused inputs

## Implemented behavior
- 4-way input select from resistor ladder on `PA7`:
  - ~0.58V -> STREAMER
  - ~1.21V -> AUX 1
  - ~1.98V -> AUX 2
  - ~2.75V -> PHONO
- Midpoint boundary decoding with sample debounce to reduce relay chatter.
- Exactly one input relay is energized at a time.
- Output relay (`PC3`) remains OFF on startup and enables after 1 second.
- On STREAMER input, the PGA2310 mute is asserted if the MCP23008 is missing, the XU208 mute line is HIGH, or the detected USB sample rate is unsupported.
- PGA2310 stereo volume control from `PB1` potentiometer ADC.
- Volume command and display are capped at `+10.0 dB`.
- 16x2 LCD displays active input and actual dB sent to PGA.
- On STREAMER input, a USB sample-rate change temporarily replaces the dB display for 3 seconds.
- USB sample rates above 192 kHz are treated as unsupported and keep the PGA2310 muted.
- Apple IR volume control on `PA6`:
  - Protocol: `Apple`
  - Address: `0xAA`
  - Volume up command: `0x0B` (raw reference `0xAA0B87EE`)
  - Volume down command: `0x0D` (raw reference `0xAA0D87EE`)
  - Short press steps volume once.
  - Held button keeps the motor moving while valid repeat frames continue.
  - Unrelated commands are ignored.
- Motorized potentiometer drive on `PB5`/`PB4`:
  - Volume up: `PB5=LOW`, `PB4=HIGH`
  - Volume down: `PB5=HIGH`, `PB4=LOW`
  - Direction is set by `MOTOR_VOLUME_UP_POLARITY_NORMAL = false` (inverted for current hardware); set `true` to swap.
  - Volume down is refused while the pot ADC reads at the bottom end stop (`<= 1`), so a disconnected wiper or pot wire can block it.
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
- Relay state legend for `R:1234` is: STREAMER, AUX1, AUX2, PHONO (`1` = active output state, `0` = inactive output state).
- Set `PREAMPV2_LCD_DEBUG` to `0` to return to normal two-line user display mode.

## IR diagnostics mode (optional, disabled by default)
- `PREAMPV2_IR_DEBUG` defaults to `0`. Set it to `1` to show the last decoded IR frame on the bottom row for 5 seconds.
- Format: `P<protocol> A<address> C<command>`, plus ` R` for a repeat frame (for example `P3 A00AA C0D R`).
- Every decoded frame is shown, including ones the firmware ignores. With IRremote 4.x, protocol `3` = Apple, `7` = NEC.

- Normal (non-debug) LCD dB rendering avoids float `snprintf` and uses fixed-point formatting for reliable AVR display updates.
- Normal display centering avoids formatted width specifiers, improving compatibility on constrained AVR `printf` builds.
- Normal display updates only changed LCD character positions instead of blanking and redrawing whole rows, reducing visible flicker during remote volume changes.

## Latest hardware calibration
- Input ladder changeover thresholds were retuned from real measurements:
  - STREAMER: `167`
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
  - On STREAMER sample-rate change: detected USB rate for 3 seconds
  - On unsupported STREAMER rate: `UNSUPPORTED RATE`
  - No additional labels/text

## Volume mapping strategy
- The volume pot is linear (B taper). Pot ADC (`0..1023`) maps to dB through a piecewise-linear curve (`VOL_CURVE_ADC` / `VOL_CURVE_DB_TENTHS`):

  | Pot travel | Dial (approx.) | dB |
  |---|---|---|
  | 0% | 7 o'clock | -95.5 |
  | 10% | 8 o'clock | -60.0 |
  | 25% | 9:30 | -35.0 |
  | 50% | 12 o'clock | -20.0 |
  | 75% | 2:30 | -6.0 |
  | 100% | 5 o'clock | +10.0 |

- The quiet tail is compressed into the first quarter so most of the dial covers normal listening levels at about 0.6 dB per 1% of travel (the previous straight dB mapping was about 1 dB per 1%).
- Adjust the feel by editing the dB points; keep the ADC points ascending.
- Result is quantized to PGA2310 half-dB steps (device-native coding).
- Full volume sends PGA2310 code `212`, which is `+10.0 dB`.
- Firmware displays the **actual quantized dB value** sent to the PGA2310.

## Arduino IDE dependencies
Install these libraries:
- `LiquidCrystal_I2C`

`Wire` and `Arduino` are from the core/toolchain.
