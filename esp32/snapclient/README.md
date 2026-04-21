# ESP32 Audio Client v9.10

Version: **0.9.10**

This revision keeps the **ESP32-WROVER-IE-N16R8** target, keeps **I2S MCLK optional**, and switches the Snapclient path to **Opus** while leaving the runtime mode switch and Bluetooth receiver behavior unchanged.

Default behavior after this change:

- **cold boot always starts in Snapclient mode**
- **press the mode button while running to reboot into Bluetooth mode**
- **MCLK is disabled by default**

That default suits many common **PCM5102-style DAC modules**, which usually do not require a separate MCLK line.

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
| I2S DOUT | `GPIO13` | External DAC serial data input |
| I2S MCLK | `GPIO0` | Optional only, used only when `I2S_MCLK_ENABLED = true` |
| Mode button | `GPIO32` | Runtime momentary mode-toggle button, active low with internal pull-up |
| Mode LED | `GPIO33` | Single mode-status LED, active high by default |

## MCLK configuration

MCLK is now controlled entirely from [board_config.h](/C:/audio-firmware-lab/esp32/snapclient/include/board_config.h).

Edit these fields:

- `I2S_MCLK_ENABLED`
- `I2S_MCLK_PIN`

### Default setting

```cpp
static constexpr bool I2S_MCLK_ENABLED = false;
static constexpr int I2S_MCLK_PIN = 0;
```

What that means:

- by default, the firmware does **not** drive an MCLK pin
- `GPIO0` is **not used by default**
- the shared I2S setup passes **no MCLK pin** to the driver when MCLK is disabled

### For common PCM5102 builds

For many PCM5102-based modules, leave:

- `I2S_MCLK_ENABLED = false`

That keeps wiring simpler and avoids using `GPIO0`.

### For DACs that require MCLK

If your DAC explicitly requires MCLK:

- set `I2S_MCLK_ENABLED = true`
- set `I2S_MCLK_PIN` to the pin you want to use

Current ESP32 caveat:

- classic ESP32 only supports I2S MCLK on **GPIO0**, **GPIO1**, or **GPIO3**

Practical recommendation:

- `GPIO0` is the least disruptive default here because `GPIO1` and `GPIO3` are UART0
- but `GPIO0` is also a boot-strapping pin, so the DAC must not pull it low during reset

## Mode button behavior

The mode button is a **runtime mode-toggle button**, not a boot selector.

Actual behavior:

- power-up / cold boot: **Snapclient mode**
- while running in Snapclient: press button -> store Bluetooth request -> reboot -> **Bluetooth mode**
- while running in Bluetooth: press button -> store Snapclient request -> reboot -> **Snapclient mode**

The runtime mode button logic is handled in [mode_switch_controller.cpp](/C:/audio-firmware-lab/esp32/snapclient/src/mode_switch_controller.cpp).

Important detail:

- the firmware ignores a button that is already held during startup
- it waits for an initial release before arming the runtime press detection
- this prevents the button from acting like a boot-time selector

Recommended wiring for the mode button:

- connect one side of the push button to `GPIO32`
- connect the other side to `GND`
- the firmware enables the internal pull-up, so no external pull-up is required for the default arrangement

If you do not have the button connected yet, you can also switch modes from the serial monitor:

- send `b` to reboot into **Bluetooth** mode
- send `s` to reboot into **Snapclient** mode
- send `t` to toggle to the opposite mode
- send `?` to print the help line again

## Mode LED behavior

The mode LED behavior is intentionally simple:

- **Snapclient mode**: LED **solid ON**
- **Bluetooth mode**: LED **blinks continuously**

The LED logic is implemented in [mode_led_controller.cpp](/C:/audio-firmware-lab/esp32/snapclient/src/mode_led_controller.cpp).

Recommended default LED wiring:

- connect `GPIO33` through a resistor to the LED anode
- connect the LED cathode to `GND`
- this matches the default active-high configuration

If your LED is wired differently, change `MODE_STATUS_LED_ACTIVE_HIGH` in [board_config.h](/C:/audio-firmware-lab/esp32/snapclient/include/board_config.h).

## Configuration files

- [snapclient_config.h](/C:/audio-firmware-lab/esp32/snapclient/include/snapclient_config.h) - Wi-Fi credentials, Snapserver address, Bluetooth device name, runtime tuning, mode-switch timing, and visible version values
- [board_config.h](/C:/audio-firmware-lab/esp32/snapclient/include/board_config.h) - all user-editable hardware pin assignments, including optional MCLK control
- [src/audio_output_controller.cpp](/C:/audio-firmware-lab/esp32/snapclient/src/audio_output_controller.cpp) - shared I2S output setup for both Snapclient and Bluetooth modes, including the single MCLK enable/disable decision
- [src/main.cpp](/C:/audio-firmware-lab/esp32/snapclient/src/main.cpp) - boot log, PSRAM setup, runtime mode setup, and LED initialization
- [src/boot_mode_selector.cpp](/C:/audio-firmware-lab/esp32/snapclient/src/boot_mode_selector.cpp) - boot-time mode resolution for cold boot vs requested software restart
- [src/mode_switch_controller.cpp](/C:/audio-firmware-lab/esp32/snapclient/src/mode_switch_controller.cpp) - runtime button press detection, debounce, mode toggle request, and reboot
- [src/mode_led_controller.cpp](/C:/audio-firmware-lab/esp32/snapclient/src/mode_led_controller.cpp) - mode LED behavior
- [src/snapclient_mode.cpp](/C:/audio-firmware-lab/esp32/snapclient/src/snapclient_mode.cpp) - Wi-Fi Snapclient mode
- [src/bluetooth_mode.cpp](/C:/audio-firmware-lab/esp32/snapclient/src/bluetooth_mode.cpp) - Bluetooth A2DP sink mode
- [docs/snapserver.md](/C:/audio-firmware-lab/esp32/snapclient/docs/snapserver.md) - Snapserver-side recommendations

## Firmware behavior

### Snapclient mode

- this is the normal cold-boot default path
- connects to Wi-Fi as a station
- starts the existing Snapclient transport path
- expects an **Opus** Snapserver stream
- uses larger buffering suited to the WROVER target
- enables PSRAM-backed allocation for larger buffers
- uses `OpusAudioDecoder` in the shared AudioTools/Snapclient path
- currently keeps a fixed `1.0` Snapclient playback factor for simpler Opus bring-up on this hardware
- uses a larger RTOS output queue and larger I2S DMA buffers to better absorb Wi-Fi jitter
- restarts on Wi-Fi loss instead of trying to continue in a bad state

### Bluetooth mode

- entered after a runtime mode-button press and reboot
- disables Wi-Fi and starts a Bluetooth A2DP sink only
- advertises as `CoolCube` by default
- writes received stereo audio to the same external I2S DAC path
- updates the I2S sample rate if the Bluetooth source changes it

## Snapserver recommendations

Snapclient mode is now intended for an **Opus** stream on this ESP32-WROVER build.

Recommended stream settings:

- codec: `opus`
- sample format: `48000:16:2`

See [snapserver.md](/C:/audio-firmware-lab/esp32/snapclient/docs/snapserver.md) for a concrete example.

Practical recommendations:

- keep the Snapserver on wired Ethernet if possible
- keep the ESP32 on strong 2.4 GHz Wi-Fi
- avoid testing initial bring-up on a congested access point

## Snapclient codec note

This build uses the same Opus decoder path shown by the upstream Arduino Snapclient I2S example instead of the earlier PCM wrapper workaround.

Why:

- Bluetooth playback is already clean on the same shared I2S/DAC path
- the remaining distortion was isolated to the Snapclient-only PCM path
- the bundled Snapclient library is better aligned with `codec=opus` on ESP32

For playback stability during bring-up, the current build uses a fixed Snapclient time-sync factor instead of dynamic resampling drift correction. That setting lives in [snapclient_config.h](/C:/audio-firmware-lab/esp32/snapclient/include/snapclient_config.h).

## Build / flash

The project defaults to the WROVER target in `platformio.ini`.

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

- mode changes are done by **software reboot**, not by hot-swapping the stacks live
- only one audio mode is active per boot
- Bluetooth mode does not talk to Snapserver at all
- Snapclient mode still depends on good Wi-Fi even with larger buffering
- Opus moves some decode work back onto the ESP32, so Wi-Fi quality and CPU headroom still matter
- when MCLK is enabled, classic ESP32 routing is limited and `GPIO0` needs careful reset-time wiring

## Change summary from v0.9.1 -> v0.9.2

- Reverted the experimental dynamic Opus sync change after it caused silence on hardware.
- Restored the fixed Snapclient playback factor used by the earlier working Opus build.
- Kept the Bluetooth mode, runtime mode switching, and shared I2S output path unchanged.

## Change summary from v0.9.4 -> v0.9.5

- Reduced the Snapclient queue activation threshold from `75%` to `40%` so playback starts earlier with the larger queue.
- Added explicit serial logging for the Snapclient queue activation percentage.
- Kept the larger queue and safe `24 x 1024` I2S DMA configuration from `v0.9.4`.

## Change summary from v0.9.5 -> v0.9.6

- Enabled info-level ESP32 logging so the built-in Snapclient queue and synchronization messages are visible during Opus bring-up.
- Kept the `v0.9.5` audio behavior unchanged so the next playback log is easier to interpret.

## Change summary from v0.9.6 -> v0.9.7

- Increased the Snapclient RTOS queue entry count from the upstream default so Opus packet writes do not hit `size_queue full` long before the PSRAM byte buffer is actually full.
- Added startup logging for the configured Snapclient queue entry slot count.

## Change summary from v0.9.9 -> v0.9.10

- Replaced the temporary Snapclient `Print` probe path with a project-local Snapclient output wrapper that keeps the direct `AudioStream` path.
- Added explicit Snapclient logs for the actual Opus audio format and clear decoder startup failures instead of silently returning zero-byte writes.
- Added a safe fallback to the configured `48 kHz / 16-bit / stereo` audio format when the upstream Opus header or output info arrives invalid.
