# ESP32 Snapcast Client Prototype (Generic Dev Board + External I2S DAC)

Status: bench bring-up project for first-audio success.

## Selected starting codebase

This project uses **`pschatzmann/arduino-snapclient`** (Arduino library port of the ESP32 Snapclient work) as the base.

Why this was chosen for your first milestone:

- Fastest path to sound on a **generic ESP32 dev board** with a simple `platformio.ini` + single `main.cpp` flow.
- Built around Arduino + AudioTools with straightforward I2S pin assignment in sketch code.
- Minimal feature surface for bring-up (Wi-Fi + Snapserver + I2S output), matching your “simplicity first” requirement.
- Reuses existing Snapclient implementation (not inventing a Snapclient from scratch).

Alternatives considered briefly:

- `jorgenkraghjakobsen/snapclient` (ESP-IDF): very capable, but heavier bring-up and older workflow for a first bench prototype.
- `sonocotta/esparagus-snapclient`: active and feature-rich, but optimized around multiple board presets/web installer flow rather than the smallest generic-dev-board prototype.

## Framework/tooling

- MCU: ESP32 dev board (`esp32dev` in PlatformIO)
- Framework: Arduino
- Build system: PlatformIO

## Pin map (default wiring)

- **BCLK**: GPIO26
- **LRCLK / WS**: GPIO25
- **DOUT**: GPIO22
- **MCLK**: not used

External DAC assumptions:

- I2S DAC accepts 16-bit stereo I2S stream.
- DAC module provides line-level output.
- DAC can operate without ESP32 MCLK for this first prototype.

## Audio format target

- Default set to **48 kHz, 16-bit, stereo**.

Why 48 kHz here:

- This aligns with common Snapcast ESP32 paths and avoids sample-format friction in initial bring-up.
- If you specifically need 44.1 kHz later, we can tune server stream/sample format and retest after first success.

## Configuration

Edit:

- `include/snapclient_config.h`

Set at minimum:

- `SNAP_WIFI_SSID`
- `SNAP_WIFI_PASSWORD`
- `SNAP_SERVER_IP`

Optional:

- `SNAP_SERVER_PORT` (default `1704`)
- `SNAP_CLIENT_NAME`
- I2S pins/sample-rate constants

## Dependencies

Defined in `platformio.ini`:

- `pschatzmann/arduino-audio-tools`
- `pschatzmann/arduino-snapclient`
- `pschatzmann/arduino-libopus`

## Build and flash

From this folder:

```bash
cd esp32/snapclient
pio run
pio run -t upload
pio device monitor -b 115200
```

## First power-up test procedure

1. Wire ESP32 -> DAC with the pin map above.
2. Confirm snapserver is reachable from ESP32 network.
3. Edit Wi-Fi + server IP in `include/snapclient_config.h`.
4. Build and flash.
5. Open serial monitor and confirm:
   - Wi-Fi connected and IP printed
   - Snapclient running
6. In snapserver/snapweb, confirm new client appears.
7. Start playback to snapserver stream and verify audible stereo output.

## Snapserver-side note

For the first pass, use an Opus/PCM stream mode that your existing snapserver already serves successfully to other clients.
If needed, keep stream format at 48 kHz stereo for easiest interoperability during bring-up.

## Troubleshooting (most likely issues)

1. **No client appears in Snapserver**
   - Verify `SNAP_SERVER_IP` and port `1704`.
   - Confirm ESP32 and snapserver are on routable networks/VLANs.

2. **Client appears but no sound**
   - Recheck I2S wiring (BCLK/WS/DOUT swapped is common).
   - Confirm DAC power/ground and analog output path.
   - Verify DAC module supports no-MCLK operation.

3. **Crackling/dropouts**
   - Improve Wi-Fi signal and reduce AP congestion.
   - Keep bench test near AP for first validation.

4. **Boot loops at Wi-Fi stage**
   - Recheck SSID/password.
   - Ensure 2.4 GHz AP compatibility for ESP32.

## Branch/repo steps used for this repo task

```bash
git checkout -b feat/esp32-snapclient-prototype
# add files under esp32/snapclient
# commit and open PR
```

(If your current local branch name differs, use an equivalent feature branch.)
