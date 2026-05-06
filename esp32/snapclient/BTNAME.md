# Bluetooth Name API Handoff

Firmware version: 0.13.0+

The ESP32 Snapclient firmware supports setting the Bluetooth device name from
SnapApp while the device is running in Snapclient mode.

## Important behavior

- The local HTTP API is only available in Snapclient mode over Wi-Fi.
- Bluetooth mode does not expose the HTTP API.
- `Stereo`, `Left`, and `Right` channel routing remain Snapclient-only.
- The Bluetooth name is saved in ESP32 preferences.
- The saved Bluetooth name is used the next time the device boots into Bluetooth mode.
- Changing the Bluetooth name does not affect Snapclient display name or channel routing.

## Status

```http
GET http://<device-ip>:8080/api/status
```

Response includes:

```json
{
  "bluetooth_name": "CoolCube",
  "capabilities": {
    "channel_modes": ["stereo", "left", "right"],
    "bluetooth_name": true
  }
}
```

## Set Bluetooth Name

```http
POST http://<device-ip>:8080/api/bluetooth-name
Content-Type: application/json
```

Body:

```json
{
  "bluetooth_name": "CoolCube Kitchen"
}
```

Rules:

- `bluetooth_name` must be 1 to 31 characters.
- Printable ASCII only.
- Double quote `"` and backslash `\` are not allowed.

Successful response returns the same shape as `GET /api/status`.

Invalid request:

```json
{
  "error": "invalid_bluetooth_name",
  "max_length": 31
}
```

## Suggested app behavior

When editing a Snapclient display name, SnapApp may optionally send the same or
derived value to `/api/bluetooth-name`.

This should be treated as a best-effort Snapclient-mode setting. If the device is
in Bluetooth mode or offline, the endpoint will not be reachable.
