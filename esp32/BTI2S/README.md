# BTI2S (ESP32 Bluetooth Audio to I2S)

## Current version
0.9.0

## Summary

Arduino sketch for ESP32 that:
- receives Bluetooth A2DP audio (sink mode)
- outputs digital audio over I2S
- allows changing the advertised Bluetooth device name using a Serial command (saved in NVS)

## MCU / Framework

- MCU: ESP32
- Framework: Arduino

## Pin map

- `IO25` -> `LRCK` (I2S word select / WS)
- `IO26` -> `BCK` (I2S bit clock / SCK)
- `IO13` -> `DATA` (I2S serial data out)
- `IO35` -> `ENC-SW` (rotary encoder push switch)
- `IO32` -> `ENC-A` (rotary encoder channel A)
- `IO33` -> `ENC-B` (rotary encoder channel B)
- `IO34` -> `BATTERY_ADC` (4S battery divider monitor input)
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
  - `bat?` prints battery diagnostics: raw ADC average, ADC pin voltage, calculated pack voltage, estimated percentage, BLE advertising/reporting/client states
  - `blebat?` prints BLE battery diagnostics (service state, device name, advertising state, client state, reporting state)
  - `blebat=on|off` toggles BLE battery reporting + advertising at runtime
  - Requested volume above the cap is safely clamped before being applied to the A2DP sink.
- Serial connection-state logs are printed when source devices connect/disconnect.
- Prints runtime I2S sample-rate updates received from the Bluetooth stream.
- Encoder controls can be disabled in firmware (`ENABLE_ENCODER_CONTROLS = false`) for encoder-free serial-volume deployments.
- Battery monitor samples ADC on `IO34` using configurable averaging and reports estimated 4S pack voltage + smoothed percent.
- Battery percentage uses a tunable 4S lookup table with interpolation (not a simple linear mapping), then smooths output to reduce jumpy readings.
- Battery debug line can be toggled with `ENABLE_BATTERY_DEBUG`.
- BLE Battery Service support is present behind `ENABLE_BLE_BATTERY_SERVICE`; when enabled in code, reporting is runtime-toggleable from Serial with `blebat=on|off`.
- Battery ADC path uses ESP32 ADC1 legacy driver calls (`adc1_get_raw` + `esp_adc_cal`) for compatibility with builds that panic when mixing ADC legacy and ADC NG paths.
- BLE battery side remains optional and advertises with a separate name (`<BT_NAME>-BAT`) so the distinction from A2DP audio is explicit.
- BLE battery support logs BLE connect/disconnect events to Serial for easier diagnostics.
- Generic BLE Battery Service (0x180F/0x2A19) on ESP32 may not populate iPhone system battery UI for the A2DP speaker identity.
- Adds an optional diagnostic BLE service with text characteristics for pack voltage and percent, intended for BLE scanner apps when system UI battery is unavailable.

## External library

Install this Arduino library:
- `ESP32-A2DP` by pschatzmann (provides `BluetoothA2DPSink`)

`Preferences` is part of the ESP32 Arduino core.

## Build / Upload notes

- Select an ESP32 board in Arduino IDE.
- Ensure Bluetooth is supported/enabled for the selected board/core.
- Build and upload `BTI2S.ino`.

## Assumptions

- Receiver hardware connected to I2S pins accepts standard ESP32 I2S timing.
- No external MCLK is required by the downstream DAC/device.
- Bluetooth source device supports A2DP audio streaming.
- `IO35` is input-only and does not provide an internal pull-up, so ENC-SW needs suitable external biasing.
