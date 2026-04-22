# Changelog

All notable changes to this repository will be recorded here.

## [Unreleased]

### Added
- `attiny/1616/ClassicCube` initial `0.1.0` ATtiny1616 Arduino sketch for PGA2311 volume control, mute handling, and single-relay toggle-switch input selection through a `ULN2003A`

### Changed
- `esp32/snapclient` stability-first v2 update for standard ESP32 (no PSRAM): dedicated Snap processing task, Wi-Fi sleep disabled, CPU fixed to 240 MHz, and simplified runtime supervision for cleaner playback
- `esp32/snapclient` version bumped to `0.2.0`

### Added
- `esp32/snapclient` new generic ESP32 dev-board Snapcast bench prototype (PlatformIO + Arduino) using `pschatzmann/arduino-snapclient` with configurable Wi-Fi, Snapserver IP, and I2S pins for external line-level DAC bring-up

### Changed
- `esp32/BTI2S` removes rotary encoder input handling and encoder pin usage; volume control remains available via Serial commands (`vol=...`)
- `esp32/BTI2S` version bumped to `0.12.0`
- `esp32/BTI2S` adds user-editable `BATTERY_CAPACITY_TEXT` and exposes it over BLE diagnostic characteristic `12345678-1234-5678-1234-56789abcdef3` for per-device capacity labeling
- `esp32/BTI2S` version bumped to `0.11.0`
- `esp32/BTI2S` adds fake battery test mode commands (`batfake?`, `batfake=0..100`, `batfake=on|off`) to simulate battery percentage when ADC input is disconnected
- `esp32/BTI2S` fake battery mode reuses the existing 4S curve to derive pack voltage and continues BLE battery updates from the simulated SOC
- `esp32/BTI2S` version bumped to `0.10.0`
- `esp32/BTI2S` adds a configurable firmware output volume cap (`MAX_OUTPUT_VOLUME_PERCENT`) to clamp applied sink volume and reduce downstream DAC clipping on hot source material
- `esp32/BTI2S` version bumped to `0.6.0`
- `esp32/BTI2S` adds 4S battery monitoring on a configurable ADC pin with resistor-divider scaling, multi-sample averaging, interpolation-based SOC lookup, and smoothed 0..100% reporting
- `esp32/BTI2S` keeps BLE battery service controllable behind a compile-time flag (`ENABLE_BLE_BATTERY_SERVICE`)
- `esp32/BTI2S` version bumped to `0.7.0`
- `esp32/BTI2S` enables BLE Battery Service support by default and adds Serial runtime toggle command `blebat=on|off` plus `bat?` status command
- `esp32/BTI2S` version bumped to `0.8.0`
- `esp32/BTI2S` now advertises BLE battery service with name `<BT_NAME>-BAT` and iOS-friendly advertising hints to improve discoverability in iPhone BLE scanner apps
- `esp32/BTI2S` version bumped to `0.8.2`

- `esp32/BTI2S` improves battery diagnostics with `bat?` detailed output (raw ADC average, ADC pin volts, pack volts, percent, BLE state) and startup first-sample battery print
- `esp32/BTI2S` adds BLE battery diagnostics commands (`blebat?`, `blebat=on|off`) plus BLE client connect/disconnect serial logs
- `esp32/BTI2S` keeps BLE battery optional and adds explicit iPhone UI limitation notes plus optional custom BLE diagnostic characteristics (pack voltage text and percent text)
- `esp32/BTI2S` version bumped to `0.9.0`

### Fixed
- `esp32/BTI2S` removes helper-function name collision risk by inlining `bat?` status print logic (fixes `redefinition of 'void printBatteryStatus()'` build error in some Arduino IDE sketch states)
- `esp32/BTI2S` version bumped to `0.10.1`
- `esp32/BTI2S` now always recognizes `blebat?` and `blebat=on|off` serial commands; when BLE battery is compile-time disabled it returns an explicit disabled message instead of `Unknown command`
- `esp32/BTI2S` version bumped to `0.9.2`
- `esp32/BTI2S` fixes BLE battery feature gating by defining `ENABLE_BLE_BATTERY_SERVICE` as a preprocessor macro so `#if`-guarded BLE code is actually compiled
- `esp32/BTI2S` restores `blebat=on|off` and `blebat?` command handling in builds with BLE battery enabled
- `esp32/BTI2S` version bumped to `0.9.1`
- `esp32/BTI2S` resolves ESP32 boot crash (`ADC: CONFLICT! driver_ng is not allowed to be used with the legacy driver`) by switching battery sampling to ADC1 legacy API (`adc1_get_raw`/`adc1_config_*`) with `esp_adc_cal` conversion
- `esp32/BTI2S` version bumped to `0.8.1`
- `attiny/1616/PreAmpv2` fixes persistent blank normal display by removing formatted-width rendering from LCD updates and writing centered text directly
- `attiny/1616/PreAmpv2` scopes debug-only LCD helper functions behind `PREAMPV2_LCD_DEBUG` to avoid unused-function warnings
- `attiny/1616/PreAmpv2` version bumped to `0.3.8`
- `attiny/1616/PreAmpv2` fixes blank LCD after init by removing float `%f` formatting from the normal (non-debug) display path and using fixed-point dB text
- `attiny/1616/PreAmpv2` version bumped to `0.3.7`
- `attiny/1616/PreAmpv2` adds LCD bring-up diagnostics (I2C route scan + detected address/count + LCD init status) to help debug blank-screen issues without UART
- `attiny/1616/PreAmpv2` now scans both megaTinyCore TWI routes at startup (when supported) and selects the route where an I2C device is detected
- `attiny/1616/PreAmpv2` version bumped to `0.3.0`
- `attiny/1616/PreAmpv2` updates pin mapping to final hardware assignment: I2C moved to `PA1`/`PA2`, IR moved to `PA6`, and PGA2310 SCLK moved to `PB3`
- `attiny/1616/PreAmpv2` removes runtime `Wire.swap(1)` route override and now expects/uses the `PA1`/`PA2` Wire route directly
- `attiny/1616/PreAmpv2` version bumped to `0.2.4`
- `attiny/1616/PreAmpv2` fixes `snprintf` float-format build warning by casting displayed dB value to `double` for `%f`
- `attiny/1616/PreAmpv2` version bumped to `0.2.3`
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
