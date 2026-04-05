# BTI2S (ESP32 Bluetooth Audio to I2S)

## Current version
0.7.1

## Summary

Arduino sketch for ESP32 that:
- receives Bluetooth A2DP audio (sink mode)
- outputs digital audio over I2S
- allows changing the advertised Bluetooth device name using a Serial command (saved in NVS)

This project now also documents a **smallest-viable AirPlay path** while preserving the known-good Bluetooth sketch unchanged by default.

## MCU / Framework

- MCU: ESP32
- Framework: Arduino (current working sketch)
- AirPlay note: practical ESP32 AirPlay receivers are typically ESP-IDF centric; see **AirPlay mode** section below.

## Pin map

- `IO25` -> `LRCK` (I2S word select / WS)
- `IO26` -> `BCK` (I2S bit clock / SCK)
- `IO13` -> `DATA` (I2S serial data out)
- `IO35` -> `ENC-SW` (rotary encoder push switch)
- `IO32` -> `ENC-A` (rotary encoder channel A)
- `IO33` -> `ENC-B` (rotary encoder channel B)
- MCLK: not used (`I2S_PIN_NO_CHANGE`)

## Behaviour

- On boot, sketch loads Bluetooth name from NVS.
- If no saved name exists, default name is `BTI2S`.
- Startup applies a short mute hold by driving I2S output pins low before A2DP/I2S start.
- BT sink object is constructed on first use in `setup()` (not as a global static object) to reduce startup crashes from early initialization ordering.
- Uses explicit ESP-IDF I2S driver setup on `IO26/IO25/IO13` and feeds PCM via A2DP stream callback for deterministic output routing.
- Rotary encoder controls volume in 2% steps.
- Firmware applies a configurable output volume cap (`MAX_OUTPUT_VOLUME_PERCENT`, default `85`) to reduce clipping in downstream DACs on hot source material.
- Pressing the encoder switch toggles mute/unmute.
- Turning the encoder while muted unmutes and applies the new volume.
- ESP32 starts A2DP sink and outputs I2S audio on the pins above.
- Serial commands:
  - baud: `115200`
  - `name=YourNewName` saves new BT name and reboots to apply it
  - `vol=0..100` (or `volume=0..100`) sets runtime volume immediately and clears mute
  - Requested volume above the cap is safely clamped before being applied to the A2DP sink.
- Serial connection-state logs are printed when source devices connect/disconnect.
- Prints runtime I2S sample-rate updates received from the Bluetooth stream.
- Encoder controls can be disabled in firmware (`ENABLE_ENCODER_CONTROLS = false`) for encoder-free serial-volume deployments.

## Bluetooth mode (current sketch)

- Default compile mode is Bluetooth A2DP sink.
- Audio path:
  - iPhone/phone/computer -> Bluetooth A2DP
  - ESP32 A2DP decode callback -> shared I2S driver
  - I2S out (`IO26/IO25/IO13`) -> your existing DAC/amplifier chain
- This path is intentionally kept as the primary, known-good behavior.

## AirPlay mode (minimal integration stage)

### Important architectural note

In this codebase, AirPlay is added as a **conservative integration point**:
- `AUDIO_SOURCE_MODE_AIRPLAY` exists and initializes an AirPlay startup hook.
- If no AirPlay backend is linked yet, startup safely falls back to Bluetooth so existing playback still works.

Reason:
- For ESP32, mature AirPlay receiver implementations are typically ESP-IDF-based and significantly different from Arduino A2DP-sink sketches.
- This staged approach keeps your known-good Bluetooth path live while creating a clear insertion point for a proven AirPlay backend.

### Recommended practical approach for full AirPlay

Use two firmware targets on the same hardware/I2S pin map:
1. **Bluetooth firmware** (this Arduino sketch, unchanged path)
2. **AirPlay firmware** (ESP-IDF AirPlay-capable project, configured for the same I2S pins)

This gives reliable operation with a simple “flash desired mode” workflow, instead of fragile simultaneous multi-stack runtime behavior.

If you want one-button mode switching later, add a small boot-selector strategy (NVS flag + OTA partitions), but that is intentionally out-of-scope for this minimal stability-first change.

## External libraries / dependencies

### Bluetooth sketch dependencies

Install this Arduino library:
- `ESP32-A2DP` by pschatzmann (provides `BluetoothA2DPSink`)

`Preferences` and `driver/i2s.h` are provided by ESP32 Arduino core.

### AirPlay dependencies (when enabling real AirPlay backend)

- Use an ESP-IDF AirPlay-capable project or backend (RAOP/AirPlay receiver implementation)
- Connect that backend to the sketch's shared PCM/I2S path (`startAirPlaySource()` in `BTI2S.ino`)
- Keep I2S output pins:
  - BCK: `IO26`
  - LRCK: `IO25`
  - DATA: `IO13`

## Build / Flash

### Bluetooth mode (Arduino)

1. Open `BTI2S.ino` in Arduino IDE.
2. Select an ESP32 board.
3. Install `ESP32-A2DP`.
4. Build and flash.

### AirPlay mode (recommended separate ESP-IDF firmware)

1. Open chosen ESP-IDF AirPlay project.
2. Set Wi-Fi credentials and device name in that project.
3. Set I2S pins to match this hardware (`26/25/13`).
4. Build/flash with `idf.py`.

## Limitations

- This sketch does not run Bluetooth + AirPlay simultaneously.
- In the current stage, AirPlay mode is a startup hook with safe Bluetooth fallback until a real AirPlay backend is linked.
- AirPlay backend selection is intentionally left explicit to avoid silent protocol/stack changes.

## Assumptions

- Receiver hardware connected to I2S pins accepts standard ESP32 I2S timing.
- No external MCLK is required by the downstream DAC/device.
- Bluetooth source device supports A2DP audio streaming.
- `IO35` is input-only and does not provide an internal pull-up, so ENC-SW needs suitable external biasing.
