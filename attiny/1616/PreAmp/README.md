# PreAmp

Version: 0.1.6

ATtiny1616 preamp controller for a PGA2310-based analog preamp with relay input selection, motorized potentiometer support, and 16x2 LCD status display.

## MCU / framework
- MCU: ATtiny1616
- Framework: Arduino (megaTinyCore)

## Pin map (fixed)
- `PB0` - Relay 1 (`DAC`) via ULN2003
- `PC0` - Relay 2 (`AUX 1`) via ULN2003
- `PC1` - Relay 3 (`AUX 2`) via ULN2003
- `PC2` - Relay 4 (`PHONO`) via ULN2003
- `PC3` - Output relay via ULN2003
- `PB5` - DRV8210 motor input 1
- `PB4` - DRV8210 motor input 2
- `PA1` - I2C SDA (16x2 LCD)
- `PA2` - I2C SCL (16x2 LCD)
- `PA6` - IR receiver input (volume control only)
- `PA4` - PGA2310 mute
- `PA5` - PGA2310 SDI
- `PB3` - PGA2310 SCLK
- `PA3` - PGA2310 CS
- `PB1` - Volume ADC input (`VOL IN`)
- `PA7` - Input select ADC input (`INPUT IN` resistor ladder)
- `PB2` - unused
- `PA0` - UPDI

## Behaviour summary
- Controls PGA2310 stereo volume and caps gain at `+10.0 dB`.
- Selects one of 4 inputs (`DAC`, `AUX 1`, `AUX 2`, `PHONO`) using ULN2003-driven relays.
- Controls output relay with safe startup delay:
  - output relay stays off for the first 1 second after power-on
  - PGA mute stays asserted during this delay
  - output relay then engages and mute is released
- Displays:
  - top LCD row: active input name in uppercase, centered
  - bottom LCD row: current volume in dB, centered
  - firmware probes LCD addresses `0x27` then `0x3F` at startup (with startup settle/retry delay) and uses the one that responds
- Supports adjustable volume taper blending with `VOLUME_CURVE_BLEND_PERCENT`:
  - `0` = linear
  - `100` = log-like (square-law)
- IR input is used for volume control only (`VOL+`/`VOL-` commands).
- Drives motorized potentiometer via DRV8210 toward an IR-adjusted target ADC value.

## Input ladder assumption (`INPUT IN`)
Selector mapping now uses measured ladder voltages (at `VDD = 3.3V`):
- `0.541V` -> Relay 1 (`DAC`)
- `1.170V` -> Relay 2 (`AUX 1`)
- `1.940V` -> Relay 3 (`AUX 2`)
- `2.700V` -> Relay 4 (`PHONO`)

Firmware uses midpoint ADC thresholds between those measured levels.

ADC reads for both `VOL IN` and `INPUT IN` use a throwaway first conversion plus 4-sample averaging for better stability when channels switch.

Firmware keeps `PB1` as a plain high-impedance analog input (`pinMode(INPUT)` only), with no forced low-level `PORTB.PIN1CTRL` bit changes.

## External libraries
- `LiquidCrystal_I2C` (for 16x2 LCD)
- No IR decode library required (firmware includes a small NEC decoder)

## Build / upload notes
- Open `PreAmp.ino` in Arduino IDE.
- Board package: megaTinyCore, target ATtiny1616.
- Configure board clock/fuses per your hardware.
- Confirm IR command constants in `PreAmp.ino` match your handset.
- If motor direction is reversed, swap the direction branches in `motorDriveTowardTarget()`.
