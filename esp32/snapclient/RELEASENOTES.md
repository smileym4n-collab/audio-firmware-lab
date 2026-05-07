# Release Notes - ESP32 Audio Client v0.14.0

Release date: 2026-05-07

Target hardware:

- ESP32-WROVER-IE-N16R8
- 16 MB flash / 8 MB PSRAM
- External I2S DAC
- PCM Snapserver stream

## Summary

This release adds the first OTA-capable firmware for the ESP32 Snapclient build. Install this version over USB once, then future compatible firmware builds can be uploaded from SnapControl over the local network.

The normal audio behavior is intended to remain unchanged from the previous Snapclient PCM build.

## What This Firmware Does

This firmware turns an ESP32-WROVER module into a Wi-Fi Snapcast audio client with a small local control API for companion apps.

In normal use, the device boots into Snapclient mode, connects to Wi-Fi, connects to the configured Snapserver, receives a PCM Snapcast stream, and outputs stereo audio through an external I2S DAC. It is intended for DIY whole-home or multi-room audio builds where Snapserver provides synchronized audio and the ESP32 board acts as a local network speaker endpoint.

The firmware also includes an alternate Bluetooth mode. Pressing the runtime mode button stores a Bluetooth-mode request and reboots the device. In Bluetooth mode, the ESP32 works as a simple Bluetooth A2DP audio sink using the same external I2S DAC output path.

SnapControl, the companion iOS app, can use the local HTTP API while the device is running in Snapclient mode to:

- read firmware identity and status
- show battery voltage/percentage when battery sensing is wired and enabled
- change local output routing between stereo, left-to-both, and right-to-both
- save the Bluetooth device name used on later Bluetooth-mode boots
- upload future firmware `.bin` builds over OTA after this release has first been installed by USB

Bluetooth mode does not expose the local HTTP API. App control and OTA update are Snapclient/Wi-Fi-mode features.

## Required Hardware

Minimum hardware required:

- ESP32-WROVER-IE-N16R8 module or board
- 16 MB flash / 8 MB PSRAM target
- external I2S DAC module
- stable 3.3 V power supply for the ESP32
- 2.4 GHz Wi-Fi network with access to the Snapserver
- Snapserver configured to provide a PCM stream

Default I2S DAC wiring:

| Function | GPIO | Notes |
| --- | --- | --- |
| I2S BCLK | `GPIO26` | External DAC bit clock |
| I2S LRCLK / WS | `GPIO25` | External DAC word select |
| I2S DOUT | `GPIO13` | External DAC serial data input |
| I2S MCLK | `GPIO0` | Optional only; disabled by default |

Optional hardware:

- momentary mode button on `GPIO23` to toggle between Snapclient and Bluetooth mode by reboot
- Wi-Fi status LED on `GPIO32`
- Bluetooth status LED on `GPIO33`
- battery voltage divider connected to `GPIO34`

Default battery-sense assumption:

- battery positive -> `270k` resistor -> `GPIO34` sense input -> `47k` resistor -> `GND`
- intended for a 4S lithium pack

Hardware notes:

- Common PCM5102-style DAC modules usually do not require MCLK, so MCLK is disabled by default.
- If MCLK is enabled, classic ESP32 MCLK routing is limited and `GPIO0` is a boot-strapping pin.
- The ESP32 must be on the same trusted local network as SnapControl for OTA upload.
- USB access should remain available for first flash and recovery.

## Highlights

- Added local OTA firmware upload support in Snapclient mode.
- Added `POST /api/firmware` for raw PlatformIO `.bin` app-image uploads.
- Added OTA discovery fields to `GET /api/status`.
- Kept channel routing, Bluetooth-name control, battery reporting, and existing Snapclient behavior available through the local API.
- Kept USB flashing as the required recovery path.

## Firmware Version

- Previous version: `0.13.4`
- New version: `0.14.0`

Visible firmware version fields:

- `version`: `0.14.0`
- `firmwareVersion`: `0.14.0`

## OTA Update Workflow

First install this release by USB flashing.

After this release is running on the ESP32:

1. Build future firmware with PlatformIO.
2. Use the generated file:
   `C:\audio-firmware-lab\esp32\snapclient\.pio\build\esp32-wrover-ie-n16r8\firmware.bin`
3. Upload that file from SnapControl to:
   `POST http://<esp-ip>:8080/api/firmware`
4. Wait for the ESP32 to reboot.
5. Confirm the device returns through `GET /api/status`.

## API Changes

`GET /api/status` now reports OTA support and target metadata:

```json
{
  "board": "ESP32-WROVER-IE-N16R8",
  "flash_size_mb": 16,
  "ota_partition_size": 6553600,
  "ota_supported": true,
  "update_in_progress": false,
  "capabilities": {
    "firmware_update": true
  }
}
```

New firmware upload endpoint:

```http
POST /api/firmware
Content-Type: application/octet-stream
Content-Length: <firmware size in bytes>
X-Firmware-Filename: firmware.bin
```

The request body must be the raw PlatformIO firmware `.bin` app image. Do not use multipart upload or a JSON wrapper.

Successful response:

```json
{
  "ok": true,
  "message": "Firmware accepted. Rebooting."
}
```

## Important Notes

- This release must be flashed over USB before OTA updates can be used.
- OTA updates cannot change the partition table or bootloader.
- USB recovery is still required if Wi-Fi, Snapclient mode, or the OTA endpoint stops working.
- OTA upload is intended for trusted local-network use only.
- Do not expose the ESP32 HTTP API to the public internet.

## Build Notes

Build command:

```bash
pio run -e esp32-wrover-ie-n16r8
```

The build uses PlatformIO's `default_16MB.csv` partition table, which provides two OTA app slots.

The current `0.14.0` build output size is comfortably below the OTA slot limit:

- App slot size: `6553600` bytes
- Built firmware image: about `1911141` bytes

## Manual Test Checklist

- Flash `0.14.0` by USB.
- Confirm `GET /api/status` reports `firmwareVersion` as `0.14.0`.
- Confirm `ota_supported` is `true`.
- Confirm `capabilities.firmware_update` is `true`.
- Confirm normal Snapclient audio behavior still works.
- Confirm channel routing still works.
- Confirm Bluetooth-name saving still works.
- Upload a known-good future `firmware.bin` through SnapControl.
- Confirm the ESP32 reboots and returns to `/api/status`.
- Confirm failed uploads leave the old firmware running.
