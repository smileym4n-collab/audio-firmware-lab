# PreAmp

Version: 0.1.13

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
  - firmware forces default megaTinyCore TWI pin mux (PA1/PA2), scans PCF8574 address ranges (`0x20..0x27` and `0x38..0x3F`), and includes a selectable backpack bit-mapping profile (`LCD_BACKPACK_PROFILE`)
- Supports adjustable volume taper blending with `VOLUME_CURVE_BLEND_PERCENT`:
  - `0` = linear
  - `100` = log-like (square-law)
- IR input is used for volume control only (`VOL+`/`VOL-` commands).
- Drives motorized potentiometer via DRV8210 toward an IR-adjusted target ADC value.

## Input ladder assumption (`INPUT IN`)
Selector mapping now uses measured ladder voltages (at `VDD = 3.3V`), and `PA7` is explicitly forced to high-impedance ADC mode in firmware:
- `0.541V` -> Relay 1 (`DAC`)
- `1.170V` -> Relay 2 (`AUX 1`)
- `1.940V` -> Relay 3 (`AUX 2`)
- `2.700V` -> Relay 4 (`PHONO`)

Firmware forces Relay 1 (DAC) whenever selector voltage is below `0.8V` (`<=248` ADC @ 3.3V), then uses nearest measured ADC center for the remaining relays with repeated confirmation before switching.

`INPUT IN` uses throwaway + 4-sample averaging for selector stability, while `VOL IN` uses a single direct conversion at a slower poll rate to minimize loading on `PB1`.

Firmware keeps `PB1` as a plain high-impedance analog input (`pinMode(INPUT)` only), with no forced low-level `PORTB.PIN1CTRL` bit changes.

## External libraries
- No LCD library required (firmware includes a direct PCF8574 + HD44780 4-bit driver)
- No IR decode library required (firmware includes a small NEC decoder)

## Build / upload notes
- Open `PreAmp.ino` in Arduino IDE.
- Board package: megaTinyCore, target ATtiny1616.
- Configure board clock/fuses per your hardware.
- Confirm IR command constants in `PreAmp.ino` match your handset.
- If motor direction is reversed, swap the direction branches in `motorDriveTowardTarget()`.
