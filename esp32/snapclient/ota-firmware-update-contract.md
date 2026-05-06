# OTA Firmware Update Contract

This contract describes the first OTA firmware-update API expected by SnapControl.

SnapControl does not compile firmware on iOS. Firmware is built outside the app, for example with PlatformIO, and SnapControl uploads the resulting `.bin` file to a compatible ESP32 client over the local network.

## Firmware Identity

The firmware image must target:

- board/module: ESP32-WROVER-IE-N16R8
- flash size: 16 MB
- app: Snapclient ESP32 firmware
- transport: local HTTP API on port `8080`

USB flashing is still required for first-time OTA enablement, partition-table changes, bootloader recovery, and recovery from firmware that breaks Wi-Fi or the OTA endpoint.

## Status Endpoint

```http
GET /api/status
```

SnapControl uses this endpoint to decide whether firmware updates should be shown for a client.

Required existing fields:

- `firmwareVersion`: string
- `runtime_mode`: string
- `channel_mode`: string

Required OTA fields:

- `ota_supported` or `otaSupported`: boolean
- `update_in_progress` or `updateInProgress`: boolean

SnapControl also accepts OTA support reported through capabilities:

- `capabilities.firmware_update`: boolean
- `capabilities.firmwareUpdate`: boolean
- `capabilities.ota_firmware_update`: boolean
- `capabilities.ota_supported`: boolean
- `capabilities.otaSupported`: boolean
- `capabilities.firmware_upload`: boolean
- `capabilities.firmwareUpload`: boolean

Recommended response:

```json
{
  "project": "ESP32 Audio Client",
  "version": "0.12.0",
  "firmwareVersion": "0.12.0",
  "ota_supported": true,
  "update_in_progress": false,
  "runtime_mode": "snapclient",
  "channel_mode": "stereo",
  "capabilities": {
    "channel_modes": ["stereo", "left", "right"],
    "bluetooth_name": true,
    "firmware_update": true
  }
}
```

If OTA is not available, report `false` or omit the OTA fields. SnapControl will keep standard Snapcast controls available and hide the upload button.

## Firmware Upload

```http
POST /api/firmware
Content-Type: application/octet-stream
X-Firmware-Filename: firmware.bin
```

Request body:

- raw PlatformIO firmware `.bin` app image
- no multipart wrapper
- no JSON envelope

SnapControl currently sends the whole file as the HTTP body. The first app version shows an indeterminate upload spinner rather than percentage progress.

Successful response:

```json
{
  "ok": true,
  "message": "Firmware accepted. Rebooting."
}
```

Failure response:

```json
{
  "ok": false,
  "error": "Image too large"
}
```

SnapControl treats a successful 2xx response with an empty body as accepted. If the JSON response has `"ok": false`, SnapControl shows the returned `error` or `message`.

## Expected Firmware Behavior

The firmware should:

- accept uploads only when OTA is supported and the device is in a safe state to update
- reject unsupported, oversized, truncated, or invalid images
- write accepted images to the inactive OTA partition
- reboot only after the write and validation are complete
- keep the old firmware running when an upload fails before reboot
- report the new `firmwareVersion` from `GET /api/status` after reboot
- keep normal Snapclient behavior usable when OTA upload fails before reboot

After SnapControl receives an accepted upload response, it expects the ESP32 may reboot and temporarily disappear. SnapControl waits briefly, then retries `GET /api/status` for about one minute. If the device comes back, the app shows the update as complete. If it does not come back in time, the app reports that the firmware was accepted but the new version could not be confirmed.

## Partitioning Expectations

The ESP32-WROVER-IE-N16R8 has 16 MB flash, but PlatformIO and the partition table must be configured to use it.

Recommended firmware-side requirements:

- configure PlatformIO for 16 MB flash
- use an OTA-capable partition table with two app slots
- ensure each OTA app slot is larger than the built `.bin` image with headroom
- include `otadata`
- preserve enough NVS/storage space for existing firmware settings

Example intent, not a final required partition table:

```text
nvs       ~20 KB
otadata    8 KB
app0       4 MB
app1       4 MB
storage    remaining flash
```

## Security

The first SnapControl app-side implementation does not add authentication headers. Before exposing OTA broadly, consider adding a firmware-side token, pairing step, or local-only protection and then extending this contract with the required request header.

## Manual Test Notes

Before testing through SnapControl, verify the firmware endpoint manually with a known-good `.bin` file.

Example:

```bash
curl -X POST \
  -H "Content-Type: application/octet-stream" \
  -H "X-Firmware-Filename: firmware.bin" \
  --data-binary @firmware.bin \
  http://<esp-ip>:8080/api/firmware
```

Manual checks:

- `GET /api/status` reports OTA support before upload
- upload rejects a non-firmware or oversized file
- successful upload reboots into the new firmware
- `GET /api/status` reports the new `firmwareVersion` after reboot
- failed upload leaves the old firmware running
- USB recovery remains available
