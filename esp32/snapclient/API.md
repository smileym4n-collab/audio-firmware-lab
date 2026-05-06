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
  "version": "0.11.0",
  "firmwareVersion": "0.11.0",
  "runtime_mode": "snapclient",
  "channel_mode": "stereo",
  "battery": {
    "available": true,
    "voltage": 16.42,
    "percent": 95
  },
  "capabilities": {
    "channel_modes": ["stereo", "left", "right"]
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
| `battery.available` | boolean | `true` when battery sensing is enabled and a reading is available |
| `battery.voltage` | number | Reconstructed 4S pack voltage in volts, not ADC divider voltage |
| `battery.percent` | number | Estimated 4S battery percentage, `0..100` |
| `capabilities.channel_modes` | string array | Channel modes accepted by `POST /api/channel-mode` |

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
