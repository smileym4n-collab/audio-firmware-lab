# BTI2S (ESP32 Bluetooth Audio to I2S)

## Current version
0.3.3

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
- MCLK: not used (`I2S_PIN_NO_CHANGE`)

## Behaviour

- On boot, sketch loads Bluetooth name from NVS.
- If no saved name exists, default name is `BTI2S`.
- Startup applies a short mute hold by driving I2S output pins low before A2DP/I2S start.
- Startup configures AudioTools `I2SStream` pins (`IO26/IO25/IO13`) before A2DP start for compatibility with newer ESP32-A2DP APIs.
- Rotary encoder controls volume in 2% steps.
- Pressing the encoder switch toggles mute/unmute.
- Turning the encoder while muted unmutes and applies the new volume.
- ESP32 starts A2DP sink and outputs I2S audio on the pins above.
- Serial command can rename the Bluetooth device:
  - baud: `115200`
  - command: `name=YourNewName`
  - device saves name and reboots to apply it.

## External library

Install this Arduino library:
- `ESP32-A2DP` by pschatzmann (provides `BluetoothA2DPSink`)

Also required by current BTI2S code path:
- `AudioTools` (usually installed automatically as an `ESP32-A2DP` dependency)

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
