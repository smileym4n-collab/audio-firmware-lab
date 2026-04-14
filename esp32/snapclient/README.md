# ESP32 Snapcast Client v2 (Standard ESP32 + PCM5102, Stability-First)

Version: **0.2.0**

This v2 is tuned for **first-success clean playback** on a **standard ESP32 dev board without PSRAM**.

## Architecture decision (v2)

### Chosen path: keep `pschatzmann/arduino-snapclient`, but harden runtime behavior

For your exact bench target (generic ESP32 dev board, already connecting to Snapserver, external PCM5102), the fastest path to stable audio is to keep the current stack and remove obvious runtime instability causes:

- Wi-Fi power save disabled (reduces bursty packet latency)
- Snap processing moved into a dedicated FreeRTOS task (fewer loop-starvation events)
- CPU fixed at 240 MHz
- conservative loop/check timing and restart-on-link-loss policy
- simple, explicit I2S output config for external DAC, no MCLK

Why not switch codebase immediately:

- A full repo migration (ESP-IDF/other Snapclient ports) is higher risk for first bench success and takes longer to validate.
- You already have discovery/connection working with this stack, so we optimize the path that is closest to “clean audio quickly”.

## Codec recommendation for non-PSRAM ESP32

Current code remains Opus-compatible because that is what your current implementation uses.

For **best reliability on non-PSRAM ESP32**, prefer a Snapserver stream profile that lowers decoder pressure:

1. Keep stereo, 16-bit.
2. Prefer 48 kHz (or 44.1 kHz if your source chain is native 44.1).
3. Use conservative Opus settings if Opus is retained (lower complexity / lower bitrate).
4. If your Snapserver/client stack supports it cleanly, test PCM stream mode for this ESP32 client profile (higher network use, lower decode complexity).

In practice, do this as an A/B bench test:
- profile A: current Opus stream (tuned for low complexity)
- profile B: PCM stream (if supported by your exact server/client combination)
- choose whichever produces fewer dropouts on your actual Wi-Fi environment

## Hardware target (unchanged)

- Board: standard ESP32 dev board (no PSRAM)
- DAC: PCM5102 external I2S DAC
- Wi-Fi only
- No MCLK

## Pin map

- **GPIO26** -> I2S BCLK
- **GPIO25** -> I2S LRCLK / WS
- **GPIO22** -> I2S DOUT

## Project files

- `platformio.ini` - release build config + library deps
- `include/snapclient_config.h` - user-editable Wi-Fi/server/pin/runtime tuning
- `src/main.cpp` - stability-first runtime implementation

## Build flags / memory-related settings used

- `build_type = release`
- `-O2`
- `-DCORE_DEBUG_LEVEL=1`
- `-DCONFIG_SNAPCLIENT_USE_MDNS=false`
- `-DCONFIG_NVS_FLASH=false`

Runtime memory/CPU strategy:

- dedicated Snap processing task stack: `8192` words
- fixed CPU frequency: `240 MHz`
- Wi-Fi sleep disabled

## Flash / build procedure

```bash
cd esp32/snapclient
pio run
pio run -t upload
pio device monitor -b 115200
```

## Required local configuration

Edit `include/snapclient_config.h`:

- `SNAP_WIFI_SSID`
- `SNAP_WIFI_PASSWORD`
- `SNAP_SERVER_IP`
- optionally `SNAP_CLIENT_NAME`

## Recommended Snapserver-side checks/changes

1. Confirm stream format for this bench client is 16-bit stereo and 48 kHz (or 44.1 kHz if your chain is 44.1-native).
2. If staying on Opus, set a conservative encoding profile first (do not optimize for minimum latency yet).
3. If your stack supports PCM stream mode cleanly, test it for this client and compare glitch rate.
4. Keep server and AP wired/nearby for initial validation to reduce Wi-Fi variability during tuning.

## Test procedure (bench)

1. Place ESP32 near AP to remove weak-signal effects.
2. Flash firmware and open serial monitor.
3. Confirm boot log shows:
   - CPU 240 MHz
   - Wi-Fi connected
   - Snapclient running
4. Start a known clean audio source on Snapserver.
5. Listen for at least 10-15 minutes:
   - no persistent distortion
   - no repeated stutter bursts
6. Optional stress pass:
   - run moderate Wi-Fi traffic in background and verify playback remains acceptable.

## Known limits of this v2

- still focused on single-purpose playback only (no UI/buttons/metadata)
- no automatic adaptive codec switching in firmware
- if distortion persists under all stream profiles, next step is controlled migration to an ESP-IDF-focused Snapclient implementation

## Change summary from v0.1.0 -> v0.2.0

- Added stability-first tasking model (dedicated Snap processing task).
- Disabled Wi-Fi sleep.
- Fixed runtime CPU frequency to 240 MHz.
- Simplified watchdog-friendly supervision loop.
- Kept hardware wiring and no-MCLK PCM5102 target unchanged.
