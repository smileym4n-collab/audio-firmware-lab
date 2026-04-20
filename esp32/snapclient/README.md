# ESP32 Audio Client v5

Version: **0.5.0**

This revision keeps the **ESP32-WROVER-IE-N16R8** target and refines startup behavior for a **momentary boot button**:

- **default boot = Snapclient mode**
- **hold / press the boot button during startup = Bluetooth mode**

It also adds a single, centralized **mode-status LED** so the active mode is obvious without needing the serial console.

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
| Mode button | `GPIO32` | Momentary boot button, active low with internal pull-up |
| Mode LED | `GPIO33` | Single mode-status LED, active high by default |

## Boot mode selection

The startup decision is made in [boot_mode_selector.cpp](/C:/audio-firmware-lab/esp32/snapclient/src/boot_mode_selector.cpp).

Default behavior:

- if the button is **not** pressed during startup, boot **Snapclient**
- if the momentary button **is** detected active during startup, boot **Bluetooth**

At boot the firmware performs a short stable-read check rather than a single raw GPIO sample, so the decision is still simple but less sensitive to switch bounce.

| Button state during startup | GPIO32 level | Selected mode |
| --- | --- | --- |
| Not pressed | `HIGH` | Snapclient mode |
| Pressed / held to GND | `LOW` | Bluetooth mode |

Recommended wiring for the momentary boot button:

- connect one side of the push button to `GPIO32`
- connect the other side to `GND`
- the firmware enables the internal pull-up, so no external pull-up is required for the default arrangement

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

- [snapclient_config.h](/C:/audio-firmware-lab/esp32/snapclient/include/snapclient_config.h) - Wi-Fi credentials, Snapserver address, Bluetooth device name, runtime tuning, boot defaults, LED timing, and visible version values
- [board_config.h](/C:/audio-firmware-lab/esp32/snapclient/include/board_config.h) - all user-editable hardware pin assignments
- [src/main.cpp](/C:/audio-firmware-lab/esp32/snapclient/src/main.cpp) - boot log, PSRAM setup, startup decision, and LED initialization
- [src/boot_mode_selector.cpp](/C:/audio-firmware-lab/esp32/snapclient/src/boot_mode_selector.cpp) - momentary boot-button read and explicit mode selection
- [src/mode_led_controller.cpp](/C:/audio-firmware-lab/esp32/snapclient/src/mode_led_controller.cpp) - mode LED behavior
- [src/snapclient_mode.cpp](/C:/audio-firmware-lab/esp32/snapclient/src/snapclient_mode.cpp) - Wi-Fi Snapclient mode
- [src/bluetooth_mode.cpp](/C:/audio-firmware-lab/esp32/snapclient/src/bluetooth_mode.cpp) - Bluetooth A2DP sink mode
- [docs/snapserver.md](/C:/audio-firmware-lab/esp32/snapclient/docs/snapserver.md) - Snapserver-side recommendations

## Firmware behavior

### Snapclient mode

- this is the normal default boot path
- connects to Wi-Fi as a station
- starts the existing Snapclient transport path
- expects a **PCM** Snapserver stream
- uses larger buffering suited to the WROVER target
- enables PSRAM-backed allocation for larger buffers
- restarts on Wi-Fi loss instead of trying to continue in a bad state

### Bluetooth mode

- entered only when the momentary button is detected during startup
- disables Wi-Fi and starts a Bluetooth A2DP sink only
- advertises as `ESP32 Audio Receiver v5` by default
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

- Snapclient mode and Bluetooth mode are still **boot-selected**, not live-switchable
- only one audio mode is active per boot
- Bluetooth mode does not talk to Snapserver at all
- Snapclient mode still depends on good Wi-Fi even with larger buffering
- MCLK on `GPIO0` requires careful reset-time wiring because it is a strapping pin

## Change summary from v0.4.0 -> v0.5.0

- Changed the startup control to a **momentary** boot button with **Snapclient as the default path**.
- Made the boot decision explicit in code: button active at boot selects Bluetooth, otherwise Snapclient.
- Added a dedicated mode LED pin and simple LED indication for each mode.
- Updated documentation and centralized hardware definitions for the boot button and mode LED.
