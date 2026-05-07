# OTA Firmware Update Contract

This contract describes the first OTA firmware-update API expected by SnapControl and the ESP32 Snapclient firmware.

SnapControl does not compile firmware on iOS. Firmware is built outside the app, for example with PlatformIO, and SnapControl uploads the resulting `.bin` app image to a compatible ESP32 client over the local network.

When Codex is building the SnapControl app, use this file as the source of truth for the app-side OTA workflow. The app should be conservative: discover support from the device, upload only a user-supplied `.bin`, show clear failure messages, and confirm the device comes back after reboot.

## Firmware Identity

The firmware image must target:

- board/module: ESP32-WROVER-IE-N16R8
- flash size: 16 MB
- app: Snapclient ESP32 firmware
- transport: local HTTP API on port `8080`

USB flashing is still required for first-time OTA enablement, partition-table changes, bootloader recovery, and recovery from firmware that breaks Wi-Fi or the OTA endpoint.

SnapControl should not offer OTA for unknown ESP devices. It should first confirm that `GET /api/status` looks like this Snapclient firmware and explicitly reports OTA support.

Recommended app-side identity checks:

- require a reachable local HTTP API at `http://<esp-ip>:8080`
- require `firmwareVersion` to be present
- require `runtime_mode` to be `snapclient`
- require one OTA support signal from the status response
- treat missing or false OTA support as "firmware update unavailable"

Optional future status fields may include board or partition metadata. SnapControl may display these fields when present, but the first app implementation must not require them:

- `board`: string, for example `ESP32-WROVER-IE-N16R8`
- `flash_size_mb`: number, for example `16`
- `ota_partition_size`: number of bytes available for the inactive OTA app slot

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

- `ota_supported`: boolean
- `update_in_progress`: boolean

The firmware should emit the snake_case field names above. SnapControl may also accept these aliases for compatibility with early or experimental firmware:

- `otaSupported`: boolean
- `updateInProgress`: boolean

SnapControl also accepts OTA support reported through capabilities. The canonical capability is `capabilities.firmware_update`; the other names are compatibility aliases:

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
  "version": "0.14.0",
  "firmwareVersion": "0.14.0",
  "board": "ESP32-WROVER-IE-N16R8",
  "flash_size_mb": 16,
  "ota_partition_size": 6553600,
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

SnapControl must not block normal channel-mode, Bluetooth-name, or status controls just because OTA is unavailable.

## SnapControl App Workflow

The first SnapControl OTA implementation should use this workflow:

1. Read `GET /api/status` before showing firmware-update controls for a client.
2. Show the firmware update control only when OTA is supported and no update is already in progress.
3. Let the user choose a prebuilt PlatformIO `.bin` app image.
4. Reject an empty file before upload.
5. Upload the raw file body to `POST /api/firmware`.
6. Treat an accepted upload as a rebooting device, not as an immediately finished update.
7. Poll `GET /api/status` for about one minute after acceptance.
8. Show success when the device comes back and reports status again.
9. If the device does not come back in time, report that the firmware was accepted but could not be confirmed.

The first app version may show indeterminate progress. Percentage progress is optional because the firmware contract does not require resumable or chunked uploads.

If the app knows the target firmware version from release metadata, it may compare that expected version with the post-reboot `firmwareVersion`. If it does not know the target version, it should still treat a successful reboot and status response as a completed update, while displaying the reported firmware version.

## Firmware Upload

```http
POST /api/firmware
Content-Type: application/octet-stream
Content-Length: <firmware size in bytes>
X-Firmware-Filename: firmware.bin
```

Request body:

- raw PlatformIO firmware `.bin` app image
- no multipart wrapper
- no JSON envelope

SnapControl sends the whole file as the HTTP body. The firmware should require a valid `Content-Length` and reject missing, zero, unsupported, or oversized uploads before writing flash when possible.

Recommended request headers:

| Header | Required | Notes |
| --- | --- | --- |
| `Content-Type` | yes | Must be `application/octet-stream` |
| `Content-Length` | yes | Must match the uploaded body size |
| `X-Firmware-Filename` | recommended | Display/debug aid only; firmware must not trust the name for validation |

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

Recommended HTTP status codes:

| Status | Meaning |
| --- | --- |
| `200 OK` or `202 Accepted` | Firmware accepted and reboot is expected |
| `400 Bad Request` | Missing headers, empty body, wrong content type, truncated body, or invalid image |
| `409 Conflict` | Device is busy, already updating, or not in a safe state to update |
| `413 Payload Too Large` | Image is larger than the inactive OTA app partition |
| `500 Internal Server Error` | Flash write, OTA validation, or finalization failed |

SnapControl should parse the JSON body when present for an `error` or `message` string. If there is no useful body, it should display a short generic message based on the HTTP status.

## Expected Firmware Behavior

The firmware should:

- accept uploads only when OTA is supported, running in Snapclient mode, and no update is already active
- reject missing `Content-Length`, zero-length, unsupported, oversized, truncated, or invalid images
- reject uploads when the inactive OTA partition cannot fit the image with reasonable headroom
- write accepted images to the inactive OTA partition
- reboot only after the write and validation are complete
- keep the old firmware running when an upload fails before reboot
- report the new `firmwareVersion` from `GET /api/status` after reboot
- keep normal Snapclient behavior usable when OTA upload fails before reboot

The firmware should validate at least the ESP image header before accepting the update. If practical, it should also validate that the image targets this app/board family before booting it.

During an update, `GET /api/status` should report:

```json
{
  "ota_supported": true,
  "update_in_progress": true
}
```

After SnapControl receives an accepted upload response, it expects the ESP32 may reboot and temporarily disappear. SnapControl waits briefly, then retries `GET /api/status` for about one minute. If the device comes back, the app shows the update as complete. If it does not come back in time, the app reports that the firmware was accepted but the new version could not be confirmed.

## Partitioning Expectations

The ESP32-WROVER-IE-N16R8 has 16 MB flash, but PlatformIO and the partition table must be configured to use it.

Recommended firmware-side requirements:

- configure PlatformIO for 16 MB flash
- use an OTA-capable partition table with two app slots
- ensure each OTA app slot is larger than the built `.bin` image with headroom
- include `otadata`
- preserve enough NVS/storage space for existing firmware settings
- keep USB flashing available for recovery

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

Until authentication is added, OTA should be treated as a trusted local-network feature only. Do not expose the ESP32 HTTP API to the public internet.

Future authenticated uploads should add a required request header rather than changing the upload body shape. For example:

```http
X-OTA-Token: <token>
```

SnapControl should be written so adding a header later is straightforward.

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
- current non-OTA firmware causes SnapControl to hide update controls
- upload rejects a non-firmware or oversized file
- upload rejects an empty file
- upload rejects a request without `Content-Length`
- upload rejects a second upload while `update_in_progress` is true
- successful upload reboots into the new firmware
- `GET /api/status` reports the new `firmwareVersion` after reboot
- failed upload leaves the old firmware running
- USB recovery remains available
