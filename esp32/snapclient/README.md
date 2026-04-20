# ESP32 Snapcast Client v3 (Standard ESP32 + PCM5102, PCM-First)

Version: **0.3.0**

This v3 prototype targets a **standard ESP32 dev board without PSRAM** driving a **PCM5102 DAC** over I2S, with the project tuned for **clean, stable playback** by removing Opus decode from the firmware path.

## Architecture decision (v3)

### Chosen path: keep `pschatzmann/arduino-snapclient`, switch the stream to PCM

The current checkout already had working Snapcast transport on ESP32, but it was built around `codec-opus`, which is the expensive part on this hardware class. For the v3 prototype, the firmware now keeps the proven transport path and changes the payload strategy:

- remove the Opus dependency from the build
- decode only Snapserver `pcm` streams using the lightweight WAV/PCM path already supported by the library
- keep the ESP32 locked at 240 MHz
- disable Wi-Fi sleep
- keep the dedicated Snap processing task
- increase I2S DMA buffering for cleaner playback on no-PSRAM boards
- keep the PCM5102 wiring and no-MCLK target unchanged

This avoids a bigger codebase migration while still replacing the part that is most likely to overload a plain ESP32.

## Hardware target

- Board: standard ESP32 dev board
- PSRAM: none
- DAC: PCM5102 external I2S DAC
- No MCLK

## Pin map

- **GPIO26** -> I2S BCLK
- **GPIO25** -> I2S LRCLK / WS
- **GPIO22** -> I2S DOUT

## Project files

- `platformio.ini` - PlatformIO environment and libraries
- `include/snapclient_config.h` - local Wi-Fi, server, audio, and DMA tuning
- `src/main.cpp` - v3 PCM-first firmware
- `docs/snapserver-v3.md` - required Snapserver-side configuration changes
- `CHANGELOG.md` - project change log

## Firmware behavior

- expects a Snapserver stream configured as **PCM**
- expects **48 kHz / 16-bit / stereo** by default
- uses the Snapclient library's WAV/PCM header path instead of Opus decode
- uses larger I2S DMA buffers to reduce underruns
- restarts on Wi-Fi loss rather than trying to limp along in a broken state

## Build / flash

```bash
cd esp32/snapclient
pio run
pio run -t upload
pio device monitor -b 115200
```

## Required local configuration

Edit [snapclient_config.h](/C:/audio-firmware-lab/esp32/snapclient/include/snapclient_config.h):

- `SNAP_WIFI_SSID`
- `SNAP_WIFI_PASSWORD`
- `SNAP_SERVER_IP`
- optionally `SNAP_HOST_NAME`
- optionally `SNAP_CLIENT_NAME`

## Required Snapserver-side changes

This firmware is no longer intended for an Opus stream on this ESP32 target. Your Snapserver source for this client should be configured as:

- codec: `pcm`
- sample format: `48000:16:2`

See [snapserver-v3.md](/C:/audio-firmware-lab/esp32/snapclient/docs/snapserver-v3.md) for concrete examples.

Important tradeoff:

- PCM removes the decode load from the ESP32
- PCM uses much more network bandwidth than Opus
- 48 kHz / 16-bit / stereo PCM is about **1.536 Mbit/s** before protocol overhead

For best results:

- keep the Snapserver on wired Ethernet if possible
- keep the ESP32 on strong 2.4 GHz Wi-Fi with a good signal
- avoid testing first bring-up through a congested AP

## Bench test checklist

1. Configure Snapserver for PCM as documented in `docs/snapserver-v3.md`.
2. Flash the firmware.
3. Open the serial monitor.
4. Confirm boot output shows:
   - ESP32 Snapclient v3
   - Wi-Fi connected
   - expected audio format `48000 Hz, 16-bit, 2 ch, codec=pcm`
5. Start playback on Snapserver.
6. Listen for at least 10 to 15 minutes and check for:
   - no persistent distortion
   - no repeated stutter bursts
   - no server-side fallback to Opus

## Known limits

- this remains a bench-focused, single-purpose player
- there is no UI or metadata support
- stability now depends more on Wi-Fi quality and available network bandwidth than on CPU decode headroom
- if PCM still glitches badly, the next step is likely transport/jitter tuning or moving to wired Ethernet rather than reintroducing Opus

## Change summary from v0.2.0 -> v0.3.0

- Replaced the firmware's Opus decoder path with the Snapclient WAV/PCM decoder path.
- Removed the Opus library dependency from the build.
- Added explicit I2S DMA tuning for the PCM5102 target.
- Documented the required Snapserver `codec=pcm` configuration.
