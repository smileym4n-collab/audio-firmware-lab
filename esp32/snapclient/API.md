# Snapclient Companion App API

Base URL:

```text
http://<esp-ip>:8080
```

Snapclient mode exposes this HTTP API. Bluetooth mode does not expose it.

## GET /api/status

Returns the current local ESP32 firmware state for SnapApp.

Example response:

```json
{
  "project": "ESP32 Audio Client v9.30",
  "version": "0.13.0",
  "firmwareVersion": "0.13.0",
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
    "bluetooth_name": true
  }
}
```

Fields:

| Field | Type | Notes |
| --- | --- | --- |
| `project` | string | Human-readable firmware/project name |
| `version` | string | Legacy firmware version field, same value as `firmwareVersion` |
| `firmwareVersion` | string | Firmware version to display in SnapApp |
| `runtime_mode` | string | Current mode; `/api/status` is available in Snapclient mode |
| `channel_mode` | string | Current local output routing: `stereo`, `left`, or `right` |
| `bluetooth_name` | string | Saved Bluetooth device name used on later Bluetooth-mode boots |
| `battery.available` | boolean | `true` when battery sensing is enabled and a reading is available |
| `battery.voltage` | number | Reconstructed 4S pack voltage in volts, not ADC divider voltage |
| `battery.percent` | number | Estimated 4S battery percentage, `0..100` |
| `capabilities.channel_modes` | string array | Channel modes accepted by `POST /api/channel-mode` |
| `capabilities.bluetooth_name` | boolean | `true` when `POST /api/bluetooth-name` is available |

When battery sensing is unavailable:

```json
{
  "battery": {
    "available": false
  }
}
```

## POST /api/channel-mode

Sets local Snapclient output routing.

Request:

```http
POST /api/channel-mode
Content-Type: application/json
```

```json
{
  "channel_mode": "left"
}
```

Allowed `channel_mode` values:

| Value | Behavior |
| --- | --- |
| `stereo` | Left input to left DAC channel, right input to right DAC channel |
| `left` | Left input duplicated to both DAC channels |
| `right` | Right input duplicated to both DAC channels |

Successful responses return the same shape as `GET /api/status`.

Invalid requests return:

```json
{
  "error": "invalid_channel_mode",
  "allowed": ["stereo", "left", "right"]
}
```

## POST /api/bluetooth-name

Sets the Bluetooth device name that will be used the next time the firmware boots into Bluetooth mode. This endpoint is only available while the device is running in Snapclient mode on Wi-Fi.

Request:

```http
POST /api/bluetooth-name
Content-Type: application/json
```

```json
{
  "bluetooth_name": "CoolCube Kitchen"
}
```

Allowed `bluetooth_name` value:

| Rule | Value |
| --- | --- |
| Length | 1 to 31 characters |
| Characters | Printable ASCII, excluding `"` and `\` |

Successful responses return the same shape as `GET /api/status`.

Invalid requests return:

```json
{
  "error": "invalid_bluetooth_name",
  "max_length": 31
}
```
