# OTA Firmware Update Contract

## Status endpoint

GET /api/status

Required fields:
- firmwareVersion: string
- otaSupported: boolean
- updateInProgress: boolean

## Firmware upload

POST /api/firmware

Request:
- Content-Type: application/octet-stream
- Body: raw firmware .bin

Response success:
{
  "ok": true,
  "message": "Firmware accepted. Rebooting."
}

Response failure:
{
  "ok": false,
  "error": "Image too large"
}

Expected behavior:
- write image to inactive OTA slot
- reject oversized or invalid images
- reboot only after successful write
- continue standard Snapclient behavior if update fails before reboot


## Firmware identity

The firmware image must target:
- board/module: ESP32-WROVER-IE-N16R8
- flash size: 16 MB
- app: Snapclient ESP32 firmware

