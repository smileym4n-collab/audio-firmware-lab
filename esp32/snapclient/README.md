# ESP32 Audio Client v4

Version: **0.4.0**

This prototype revision moves the project to an **ESP32-WROVER-IE-N16R8** target and keeps the firmware as a single codebase that boots into either:

- **Snapclient mode** for Wi-Fi Snapcast playback
- **Bluetooth mode** for standalone A2DP sink playback

The focus for v4 is stable audio on the WROVER hardware: PSRAM-backed buffering where it helps, a shared external I2S DAC path with **MCLK enabled**, and a simple boot-time mode select button.

## Target hardware

- Module: **ESP32-WROVER-IE-N16R8**
- Flash / PSRAM target: **16 MB flash / 8 MB PSRAM**
- Audio output: external I2S DAC only
- On-chip DAC: not used
- Antenna: external antenna version of WROVER-IE

## Pin map

Edit hardware assignments in [board_config.h](/C:/audio-firmware-lab/esp32/snapclient/include/board_config.h).

| Function | GPIO | Notes |
| --- | --- | --- |
| I2S BCLK | `GPIO26` | External DAC bit clock |
| I2S LRCLK / WS | `GPIO25` | External DAC word select |
| I2S DOUT | `GPIO22` | External DAC serial data input |
| I2S MCLK | `GPIO0` | Default MCLK output pin |
| Boot mode button | `GPIO32` | Active-low with internal pull-up |
| Status LED | `-1` | Disabled by default |

## Boot mode selection

The firmware reads one GPIO at startup and stays in that mode until the next reset.

| Button state at boot | GPIO32 level | Selected mode |
| --- | --- | --- |
| Released / open | `HIGH` | Snapclient over Wi-Fi |
| Pressed to GND | `LOW` | Bluetooth-only audio receiver |

Default wiring:

- connect one side of the button to `GPIO32`
- connect the other side to `GND`
- the firmware enables the internal pull-up, so no external pull-up is required for the default arrangement

The selection logic is implemented as a stable multi-sample read so brief contact bounce at power-up does not flip modes accidentally.

## Configuration files

- [snapclient_config.h](/C:/audio-firmware-lab/esp32/snapclient/include/snapclient_config.h) - Wi-Fi credentials, Snapserver address, Bluetooth device name, buffering, and versioned runtime settings
- [board_config.h](/C:/audio-firmware-lab/esp32/snapclient/include/board_config.h) - all user-editable board pin assignments
- [src/main.cpp](/C:/audio-firmware-lab/esp32/snapclient/src/main.cpp) - boot log, PSRAM setup, and mode selection
- [src/snapclient_mode.cpp](/C:/audio-firmware-lab/esp32/snapclient/src/snapclient_mode.cpp) - Wi-Fi Snapclient mode
- [src/bluetooth_mode.cpp](/C:/audio-firmware-lab/esp32/snapclient/src/bluetooth_mode.cpp) - Bluetooth A2DP sink mode
- [docs/snapserver.md](/C:/audio-firmware-lab/esp32/snapclient/docs/snapserver.md) - Snapserver-side recommendations for v4

## Firmware behavior

### Snapclient mode

- connects to Wi-Fi as a station
- starts the existing Snapclient transport path
- expects a **PCM** Snapserver stream
- uses larger buffering suited to the WROVER target
- enables PSRAM-backed allocation for larger buffers
- restarts on Wi-Fi loss instead of trying to continue in a bad state

### Bluetooth mode

- disables Wi-Fi and starts a Bluetooth A2DP sink only
- advertises as `ESP32 Audio Receiver v4` by default
- writes received stereo audio to the same external I2S DAC path
- updates the I2S sample rate if the Bluetooth source changes it

## MCLK note

Classic ESP32 hardware only supports I2S MCLK on **GPIO0**, **GPIO1**, or **GPIO3** with the Arduino / ESP-IDF driver used here.

For this revision the default MCLK pin is **GPIO0** because:

- `GPIO1` and `GPIO3` are UART0 and would interfere with the serial console
- `GPIO0` keeps boot logging available while still providing MCLK output

Important caveat:

- `GPIO0` is also a boot-strapping pin
- your DAC's MCLK input must not pull `GPIO0` low during reset
- use a DAC input that is high impedance at reset, or add buffering / series resistance if your hardware needs it

## PSRAM use

This version targets WROVER PSRAM intentionally.

- the PlatformIO target is configured for a PSRAM-capable WROVER board profile
- the firmware enables external-memory allocation for larger buffers
- Snapclient output queue buffering is increased to take advantage of the larger memory budget

## Snapserver recommendations

Snapclient mode is intended for a **PCM** stream rather than Opus on this ESP32 target.

Recommended stream settings:

- codec: `pcm`
- sample format: `48000:16:2`

See [snapserver.md](/C:/audio-firmware-lab/esp32/snapclient/docs/snapserver.md) for a concrete example.

Practical recommendations:

- keep the Snapserver on wired Ethernet if possible
- keep the ESP32 on strong 2.4 GHz Wi-Fi
- avoid testing initial bring-up on a congested access point

## Build / flash

The project now defaults to the WROVER target in `platformio.ini`.

```bash
cd esp32/snapclient
pio run
pio run -t upload
pio device monitor -b 115200
```

If you want to call the environment explicitly:

```bash
pio run -e esp32-wrover-ie-n16r8
```

## Limitations and caveats

- Snapclient mode and Bluetooth mode are **boot-selected**, not live-switchable
- only one audio mode is active per boot
- Bluetooth mode does not talk to Snapserver at all
- Snapclient mode still depends on good Wi-Fi even with larger buffering
- MCLK on `GPIO0` requires careful reset-time wiring because it is a strapping pin

## Change summary from v0.3.0 -> v0.4.0

- Retargeted the project from a plain ESP32 dev board to **ESP32-WROVER-IE-N16R8**.
- Added PSRAM-aware buffering and a WROVER-specific PlatformIO target.
- Added shared I2S output with **MCLK enabled** for an external DAC.
- Added boot-time mode selection between Wi-Fi Snapclient and Bluetooth A2DP sink modes.
- Centralized board pin assignments into `include/board_config.h`.
