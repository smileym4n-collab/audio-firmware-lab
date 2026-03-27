# Changelog

All notable changes to this repository will be recorded here.

## [Unreleased]

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
- `esp32/BTI2S` fixes a startup `LoadProhibited` crash by using stable pre-start I2S output configuration with the current ESP32-A2DP API
- `esp32/BTI2S` replaces `set_pin_config(...)` with AudioTools `I2SStream` pin configuration so builds succeed with ESP32-A2DP versions that removed `set_pin_config`
- `esp32/BTI2S` version bumped to `0.3.3`

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
