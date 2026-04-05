# BTI2S (ESP32 Bluetooth Audio to I2S)

## Current version
0.7.0

## Summary

Arduino sketch for ESP32 that:
- receives Bluetooth A2DP audio (sink mode)
- outputs digital audio over I2S
- allows changing the advertised Bluetooth device name using a Serial command (saved in NVS)
- applies a user-settable firmware output cap to reduce downstream DAC clipping on hot source material

## MCU / Framework

- MCU: ESP32
- Framework: Arduino

## Pin map

- `IO25` -> `LRCK` (I2S word select / WS)
- `IO26` -> `BCK` (I2S bit clock / SCK)
- `IO13` -> `DATA` (I2S serial data out)
- MCLK: not used (`I2S_PIN_NO_CHANGE`)

## Behaviour

- On boot, sketch loads Bluetooth name from NVS.
- If no saved name exists, default name is `BTI2S`.
- Startup applies a short mute hold by driving I2S output pins low before A2DP/I2S start.
- Uses explicit ESP-IDF I2S driver setup on `IO26/IO25/IO13` and feeds PCM via A2DP stream callback for deterministic output routing.
- Rotary encoder support has been removed.
- Bluetooth source volume can still be used normally up to max.
- Firmware applies a user-settable output cap (`cap=0..100`, stored in NVS; default `85`) by attenuating outgoing PCM before I2S write.
- Serial commands:
  - baud: `115200`
  - `name=YourNewName` saves new BT name and reboots to apply it
  - `cap=0..100` sets and stores the output cap percent immediately
  - `cap?` prints the current output cap percent
- Serial connection-state logs are printed when source devices connect/disconnect.
- Prints runtime I2S sample-rate updates received from the Bluetooth stream.

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
