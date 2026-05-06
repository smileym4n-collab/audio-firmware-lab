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

Battery fields:

- `available`: `true` when battery sensing is enabled and a reading has been taken
- `voltage`: reconstructed 4S pack voltage, not the ADC divider voltage
- `percent`: estimated 4S state of charge from the firmware lookup curve

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
