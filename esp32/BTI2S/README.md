# BTI2S (ESP32 Bluetooth Audio to I2S)

## Current version
0.1.0

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
- MCLK: not used (`I2S_PIN_NO_CHANGE`)

## Behaviour

- On boot, sketch loads Bluetooth name from NVS.
- If no saved name exists, default name is `BTI2S`.
- ESP32 starts A2DP sink and outputs I2S audio on the pins above.
- Serial command can rename the Bluetooth device:
  - baud: `115200`
  - command: `name=YourNewName`
  - device saves name and reboots to apply it.

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
