# Changelog

All notable changes to this repository will be recorded here.

## [Unreleased]

### Fixed
- `attiny/1616/PreAmp` now explicitly forces `PA7` (`INPUT IN`) to high-impedance ADC mode (pull-up off, digital input buffer disabled)
- `attiny/1616/PreAmp` adds selectable LCD backpack bit-mapping profiles via `LCD_BACKPACK_PROFILE` for broader generic 16x2 I2C module compatibility
- `attiny/1616/PreAmp` version bumped to `0.1.13`

### Fixed
- `attiny/1616/PreAmp` removes `LiquidCrystal_I2C` dependency and now drives PCF8574/HD44780 LCD backpacks directly over `Wire`, avoiding megaAVR library-compatibility warnings
- `attiny/1616/PreAmp` version bumped to `0.1.12`

### Fixed
- `attiny/1616/PreAmp` LCD detection now scans full PCF8574/PCF8574A I2C ranges (`0x20..0x27`, `0x38..0x3F`) instead of only `0x27`/`0x3F`, improving compatibility with generic 16x2 backpacks
- `attiny/1616/PreAmp` version bumped to `0.1.11`

### Fixed
- `attiny/1616/PreAmp` now hard-selects Relay 1 (DAC) whenever `PA7` is below `0.8V` (`<=248` ADC @ 3.3V), addressing missed DAC selection
- `attiny/1616/PreAmp` version bumped to `0.1.10`

### Fixed
- `attiny/1616/PreAmp` reduces `PB1` (`VOL IN`) sampling load by using single-conversion reads and slower volume poll timing (`50ms`) instead of repeated averaged conversions
- `attiny/1616/PreAmp` version bumped to `0.1.9`

### Fixed
- `attiny/1616/PreAmp` now explicitly forces default megaTinyCore TWI pin mux (`Wire.swap(0)`) and configures `PA1/PA2` as input before `Wire.begin()`, matching the fixed I2C pin map
- `attiny/1616/PreAmp` version bumped to `0.1.8`

### Fixed
- `attiny/1616/PreAmp` input relay selection now uses nearest-measured ADC centers with multi-sample confirmation before switching, improving stability on `PA7` ladder noise/variance
- `attiny/1616/PreAmp` version bumped to `0.1.7`

### Fixed
- `attiny/1616/PreAmp` reverts `PB1` setup to plain `pinMode(INPUT)` only (no forced `PORTB.PIN1CTRL` overrides) after reports of in-circuit pin loading when flashed
- `attiny/1616/PreAmp` adds LCD startup settle delay and multi-attempt I2C address probe (`0x27` / `0x3F`) to improve display bring-up reliability
- `attiny/1616/PreAmp` version bumped to `0.1.6`

### Fixed
- `attiny/1616/PreAmp` improves ADC stability by using a throwaway conversion + 4-sample averaging for both `VOL IN` and input-ladder reads
- `attiny/1616/PreAmp` now probes LCD I2C addresses `0x27` and `0x3F`, reducing display bring-up failures on alternate backpacks
- `attiny/1616/PreAmp` removes runtime `String` usage in LCD updates to avoid small-MCU heap fragmentation issues
- `attiny/1616/PreAmp` version bumped to `0.1.5`

### Fixed
- `attiny/1616/PreAmp` input selector ADC mapping now follows measured ladder voltages (`0.541V`, `1.170V`, `1.940V`, `2.700V`) so relay selection order matches hardware
- `attiny/1616/PreAmp` version bumped to `0.1.4`

### Fixed
- `attiny/1616/PreAmp` now explicitly configures `PB1` (`VOL IN`) as high-impedance ADC input with pull-up disabled and digital input buffer disabled
- `attiny/1616/PreAmp` version bumped to `0.1.3`

### Fixed
- `attiny/1616/PreAmp` replaces TinyIRReceiver callback integration with an internal NEC IR decoder, resolving `undefined reference to handleReceivedTinyIRData()` link errors
- `attiny/1616/PreAmp` version bumped to `0.1.2`

### Added
- `attiny/1616/PreAmp` initial ATtiny1616 megaTinyCore sketch with PGA2310 (+10 dB cap), 4-way ULN2003 input relay selection, output relay startup delay, I2C 16x2 LCD status display, DRV8210 motorized pot drive, and IR volume control-only support

### Changed
- `attiny/1616/minipreamp` now drives paired ULN2003A relay outputs: `PB1` follows `RELAY1` and `PA3` follows `RELAY2`
- `attiny/1616/minipreamp` adds ADC-jump filtering and a safety mute trip on repeated suspicious volume readings
- `attiny/1616/minipreamp` version bumped to `0.5.0`

### Added
- Initial repository structure for ESP32 and ATtiny projects
- Root `AGENTS.md` with coding and PR workflow rules
- New `attiny/1616/minipreamp` ATtiny1616 megaTinyCore sketch for PGA2311 volume, two-relay input selection, and TLC5916 input LEDs

### Changed
- `attiny/1616/minipreamp` now supports configurable volume taper blending (`linear` to `log-like`) with `VOLUME_CURVE_BLEND_PERCENT`
- `attiny/1616/minipreamp` now drives a 3-digit AS1115 display over I2C (`PA1`/`PA2`) to show volume as `0..100%`

### Fixed
- `attiny/1616/minipreamp` swaps AS1115 outer digit mapping (digit 1/3) so the percentage reads correctly on the current display wiring

---

## [0.3.0] - 2026-03-15

### Changed
- `esp32/BTI2S` adds rotary encoder volume control (`IO32/IO33`) and mute toggle on encoder switch (`IO35`) while keeping the existing I2S pin map unchanged

---

## [0.2.0] - 2026-03-15

### Changed
- `esp32/BTI2S` now applies a short startup mute hold on I2S pins to reduce power-on pops while A2DP/I2S initializes

---

## [0.1.0] - 2026-03-15

### Added
- Initial ESP32 and ATtiny folder layout
- Project README files for:
  - `esp32/`
  - `attiny/`
  - `attiny/412/`
  - `attiny/1616/`
  - `docs/`

### Changed
- Added branch and PR workflow guidance to `AGENTS.md`

### Fixed
- None
