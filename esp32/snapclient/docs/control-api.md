# Local control API

Snapclient mode exposes a small HTTP API for companion apps. This API is for ESP32-specific controls that Snapserver does not expose, such as local channel routing, the saved Bluetooth device name, and local OTA firmware uploads.

Bluetooth mode does not expose this API and ignores the saved channel-routing preference.

## Base URL

```text
http://<client-ip>:8080
```

The companion app can get `<client-ip>` from Snapserver status using the Snapcast client host IP.

## Status

```text
GET /api/status
```

Example response:

```json
{
  "project": "ESP32 Audio Client v9.30",
  "version": "0.14.0",
  "firmwareVersion": "0.14.0",
  "board": "ESP32-WROVER-IE-N16R8",
  "flash_size_mb": 16,
  "ota_partition_size": 6553600,
  "ota_supported": true,
  "update_in_progress": false,
  "runtime_mode": "snapclient",
  "channel_mode": "stereo",
  "bluetooth_name": "CoolCube",
  "battery": {
    "available": true,
    "voltage": 16.42,
    "percent": 95
  },
  "capabilities": {
    "channel_modes": ["stereo", "left", "right"],
    "bluetooth_name": true,
    "firmware_update": true
  }
}
```

Battery fields:

- `available`: `true` when battery sensing is enabled and a reading has been taken
- `voltage`: reconstructed 4S pack voltage, not the ADC divider voltage
- `percent`: estimated 4S state of charge from the firmware lookup curve

OTA fields:

- `ota_supported`: `true` when the firmware upload endpoint is available
- `update_in_progress`: `true` while a firmware upload is active
- `ota_partition_size`: inactive OTA app partition size in bytes, or `0` if unavailable
- `capabilities.firmware_update`: `true` when `POST /api/firmware` is available

If battery sensing is disabled or the configured pin is not ADC1-capable, the response includes:

```json
{
  "battery": {
    "available": false
  }
}
```

## Set Channel Mode

```text
POST /api/channel-mode
Content-Type: application/json
```

Request body:

```json
{
  "channel_mode": "left"
}
```

Allowed values:

- `stereo`: left DAC channel plays left, right DAC channel plays right
- `left`: both DAC channels play the left input channel
- `right`: both DAC channels play the right input channel

The selected mode is saved in ESP32 preferences and restored on later Snapclient boots.

Successful responses return the same shape as `GET /api/status`.

## Upload Firmware

```text
POST /api/firmware
Content-Type: application/octet-stream
Content-Length: <firmware size in bytes>
X-Firmware-Filename: firmware.bin
```

Request body:

- raw PlatformIO firmware `.bin` app image
- no multipart wrapper
- no JSON envelope

The firmware writes the image to the inactive OTA partition, validates it, marks it as the next boot partition, returns success, and then reboots.

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
  "error": "invalid_image",
  "message": "Firmware image has an invalid ESP header"
}
```

## Set Bluetooth Name

```text
POST /api/bluetooth-name
Content-Type: application/json
```

Request body:

```json
{
  "bluetooth_name": "CoolCube Kitchen"
}
```

The selected name is saved in ESP32 preferences and used the next time the device boots into Bluetooth mode. It does not change Snapclient channel routing, and Bluetooth mode still does not expose this API.

Allowed value:

- `bluetooth_name`: 1 to 31 printable ASCII characters, excluding `"` and `\`

Successful responses return the same shape as `GET /api/status`.
