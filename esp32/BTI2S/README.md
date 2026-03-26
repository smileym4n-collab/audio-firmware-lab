# BTI2S (ESP32 Bluetooth Audio to I2S)

## Summary

Arduino sketch for ESP32 that:
- receives Bluetooth A2DP audio (sink mode)
- outputs digital audio over I2S
- allows changing the advertised Bluetooth device name via Serial command (stored in NVS)
- allows local volume and mute control using a rotary encoder

## MCU / Framework

- MCU: ESP32
- Framework: Arduino

## Pin map

I2S:
- `IO25` -> `LRCK` (I2S WS)
- `IO26` -> `BCK` (I2S SCK)
- `IO13` -> `DATA` (I2S data out)
- MCLK: not used

Rotary encoder:
- `IO35` -> `ENC-SW` (switch)
- `IO32` -> `ENC-A`
- `IO33` -> `ENC-B`

## Behaviour

- On boot, loads Bluetooth name from NVS (`BTI2S` default if empty).
- Starts Bluetooth A2DP sink and sends audio out over I2S.
- Encoder controls output level:
  - rotate: volume up/down
  - push: mute toggle
- Serial rename command:
  - baud: `115200`
  - command: `name=YourNewName`
  - device saves the new name and reboots.

## External library

Install in Arduino IDE:
- `ESP32-A2DP` by pschatzmann (provides `BluetoothA2DPSink`)

`Preferences` is included with ESP32 Arduino core.

## Build / Upload notes

- Select an ESP32 board in Arduino IDE.
- Ensure Bluetooth is available for that board/core config.
- Build and upload `BTI2S.ino`.

## Assumptions

- I2S receiver hardware accepts ESP32 I2S output without MCLK.
- Bluetooth source device supports A2DP.
- `IO35` is input-only and has no internal pull-up on ESP32, so the encoder switch line requires an external bias resistor.


## UART troubleshooting sketch

If your custom ESP32-WROOM board is not showing BTI2S logs, use:
- `esp32/BTI2S/serial_output_test/serial_output_test.ino`

This sketch only verifies UART output/input over FT232:
- prints boot banner and periodic heartbeat
- echoes received bytes in decimal/hex

Recommended monitor settings:
- baud: `115200`
- line ending: `No line ending` (or test with `Newline`)

If this sketch does not print, solve UART wiring/power/reset first before testing BTI2S.
