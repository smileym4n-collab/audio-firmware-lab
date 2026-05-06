# Local control API

Snapclient mode exposes a small HTTP API for companion apps. This API is for ESP32-specific controls that Snapserver does not expose, such as local channel routing.

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
  "version": "0.10.1",
  "firmwareVersion": "0.10.1",
  "runtime_mode": "snapclient",
  "channel_mode": "stereo",
  "capabilities": {
    "channel_modes": ["stereo", "left", "right"]
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
