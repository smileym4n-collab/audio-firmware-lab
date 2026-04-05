# BTI2S (ESP32 Bluetooth Audio to I2S)

## Current version
0.8.0

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

## AirPlay mode (real backend integration)

### Important architectural note

In this codebase, AirPlay mode now uses a **real backend initialization path** based on `rbouteiller/airplay-esp32` (PTP clock, HAP init, audio receiver/output init, mDNS AirPlay announce, RTSP server start).

Reason:
- For ESP32, mature AirPlay receiver implementations are typically ESP-IDF-based and significantly different from Arduino A2DP-sink sketches.
- This staged approach keeps your known-good Bluetooth path live while creating a clear insertion point for a proven AirPlay backend.


### AirPlay mode requirements

AirPlay mode (`AUDIO_SOURCE_MODE_AIRPLAY`) now attempts to start backend services directly and will reboot on failure instead of silently falling back to Bluetooth.

Required backend dependency in build:
- `rbouteiller/airplay-esp32` sources/components providing:
  - `ptp_clock_init`, `hap_init`, `audio_receiver_init`, `audio_output_init`, `audio_output_start`, `mdns_airplay_init`, `rtsp_server_start`

For debugging over Serial (115200):
- `status` prints requested mode, active mode, backend flag, and I2S sample-rate state
- `Audio flow: ...` lines show whether PCM is reaching I2S

### Practical build note

Because `rbouteiller/airplay-esp32` is ESP-IDF-oriented, AirPlay mode in this sketch is realistically built in an Arduino+ESP-IDF capable workflow (for example PlatformIO with appropriate component/source integration), not plain Arduino IDE alone.

## External libraries / dependencies

### Bluetooth sketch dependencies

Install this Arduino library:
- `ESP32-A2DP` by pschatzmann (provides `BluetoothA2DPSink`)

`Preferences` and `driver/i2s.h` are provided by ESP32 Arduino core.

### AirPlay dependencies (required for AirPlay mode)

- `rbouteiller/airplay-esp32` backend sources/components available to the build
- The sketch's `startAirPlaySource()` calls backend init/start functions directly
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

### AirPlay mode

1. Set `AUDIO_SOURCE_MODE` to `AUDIO_SOURCE_MODE_AIRPLAY`.
2. Ensure `rbouteiller/airplay-esp32` backend headers/sources are present in your build environment.
3. Build with an ESP-IDF-capable workflow that can resolve those backend symbols.
4. Flash and monitor serial logs for backend init status.

## Limitations

- This sketch does not run Bluetooth + AirPlay simultaneously.
- AirPlay mode no longer falls back to Bluetooth automatically; it requires backend startup success.
- AirPlay backend selection is intentionally left explicit to avoid silent protocol/stack changes.

## Assumptions

- Receiver hardware connected to I2S pins accepts standard ESP32 I2S timing.
- No external MCLK is required by the downstream DAC/device.
- Bluetooth source device supports A2DP audio streaming.
- `IO35` is input-only and does not provide an internal pull-up, so ENC-SW needs suitable external biasing.
