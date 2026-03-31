# Changelog

All notable changes to this repository will be recorded here.

## [Unreleased]

### Fixed
- `attiny/1616/PreAmpv2` now explicitly configures I2C for LCD on `PB2/PB3` at runtime (`Wire.swap(1)` when supported) before LCD init, improving display bring-up on mismatched board menu settings
- `attiny/1616/PreAmpv2` version bumped to `0.2.2`
- `attiny/1616/PreAmpv2` updates input-ladder thresholds to measured selector voltages (`0.58V`, `1.21V`, `1.98V`, `2.75V`) and increases hysteresis for cleaner relay selection
- `attiny/1616/PreAmpv2` explicitly configures `PB1` (`VOL IN`) and `PA7` (`INPUT IN`) as high-impedance analog inputs (pull-up disabled, digital input buffer disabled)
- `attiny/1616/PreAmpv2` version bumped to `0.2.1`

### Added
- `attiny/1616/PreAmpv2` initial `0.1.0` ATtiny1616 basic firmware with resistor-ladder input selection, one-hot relay control, PGA2310 pot volume control (capped at `+10 dB`), 16x2 I2C LCD status, and 1 second delayed output relay startup

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
- `esp32/BTI2S` fixes a startup `LoadProhibited` crash by using stable pre-start I2S output configuration with the current ESP32-A2DP API
- `esp32/BTI2S` replaces `set_pin_config(...)` with AudioTools `I2SStream` pin configuration so builds succeed with ESP32-A2DP versions that removed `set_pin_config`
- `esp32/BTI2S` constructs AudioTools/A2DP objects in `setup()` to avoid early boot initialization panics
- `esp32/BTI2S` avoids AudioTools-backed sink construction (crash point) and uses deferred default `BluetoothA2DPSink` construction in `setup()`
- `esp32/BTI2S` adds serial runtime volume control commands (`vol=0..100` and `volume=0..100`) for encoder-free deployments
- `esp32/BTI2S` adds A2DP connection-state serial logs (connecting/connected/disconnecting/disconnected)
- `esp32/BTI2S` adds `ENABLE_ENCODER_CONTROLS` (default `false`) so encoder-free builds avoid floating-input behavior
- `esp32/BTI2S` switches to explicit ESP-IDF I2S driver output + A2DP PCM stream callback to stabilize playback and avoid `btController` watchdog stalls
- `esp32/BTI2S` version bumped to `0.5.1`

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
