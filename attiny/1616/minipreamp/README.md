# minipreamp

Version: 0.2.0

ATtiny1616 mini preamp controller sketch for megaTinyCore.

## MCU / framework
- MCU: ATtiny1616
- Framework: Arduino (megaTinyCore)

## Pin map (fixed)
- `PA4` - `IN_SEL_ADC` (logic input for source selection)
- `PB0` - `VOL_ADC` (potentiometer wiper for volume)
- `PB3` - `PGA2311_MUTE`
- `PB4` - `RELAY1` (ULN2003A channel input)
- `PB5` - `RELAY2` (ULN2003A channel input)
- `PC0` - shared clock for TLC5916 + PGA2311
- `PC1` - shared data for TLC5916 + PGA2311
- `PC2` - `LE` for TLC5916
- `PC3` - `CS` for PGA2311

## Behaviour summary
- Reads `IN_SEL_ADC` as a digital level:
  - LOW selects input 1 (`RELAY1` active, `RELAY2` inactive)
  - HIGH selects input 2 (`RELAY2` active, `RELAY1` inactive)
- Reads `VOL_ADC` and maps it to PGA2311 code range `0x00..0xCF`.
- Volume is capped at `0 dB` (`0xCF`), so positive gain is never commanded.
- Supports adjustable taper with `VOLUME_CURVE_BLEND_PERCENT` in `minipreamp.ino`:
  - `0` = linear mapping
  - `100` = fully log-like mapping (square-law audio taper)
  - values in-between blend linear and log-like responses
- Drives one TLC5916 LED per selected input.
- Uses safe startup defaults (mute asserted and relays off before applying initial state).

## Assumptions
- `PGA2311_MUTE` is active-low (`LOW = mute`, `HIGH = unmute`).
- ULN2003A channel input HIGH energizes the associated relay.
- TLC5916 output bit `1` enables current sink for that LED channel.

## Build / upload notes
- Open `minipreamp.ino` in Arduino IDE configured with megaTinyCore.
- Select target board with ATtiny1616 and your clock/fuse settings.
- No external libraries are required.
