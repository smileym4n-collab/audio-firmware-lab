# Changelog

All notable changes to this repository will be recorded here.

## [Unreleased]

### Added
- Initial repository structure for ESP32 and ATtiny projects
- Root `AGENTS.md` with coding and PR workflow rules

### Changed
- None yet

### Fixed
- None yet

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
