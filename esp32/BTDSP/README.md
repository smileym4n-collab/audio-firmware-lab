# BTDSP (ESP-IDF)

Minimal ESP-IDF project for **ESP32-WROOM-32UE-N4CT** that receives Bluetooth Classic A2DP audio from a phone, runs it through a DSP stage (currently pass-through), and outputs stereo PCM over I2S to an external DAC.

## Project summary

- **Framework:** ESP-IDF
- **Target:** `esp32`
- **Input:** Bluetooth Classic A2DP Sink
- **Pipeline:** `Bluetooth PCM -> DSP stage -> I2S output`
- **Output:** I2S stereo transmit (master mode)
- **No Wi-Fi / no UI / no networking extras**
- **No rotary encoder support in this project**

## I2S pin map (external DAC)

- **BCK**: GPIO26
- **LRCK/WS**: GPIO25
- **DATA (DOUT)**: GPIO13
- **MCLK**: not used

Edit these in `main/main.c` (`I2S_BCK_GPIO`, `I2S_LRCK_GPIO`, `I2S_DOUT_GPIO`).

## DSP structure

The DSP module is in:

- `main/dsp.h`
- `main/dsp.c`

Key functions:

- `dsp_init()`
- `dsp_process_stereo_int16(...)`
- `dsp_set_peq(...)`

`dsp_process_stereo_int16(...)` is currently pass-through and is the intended place to add:

- boxiness reduction EQ (start around **350-400 Hz**, about **-3 dB**, **Q ~1.0**)
- bass / treble shelves
- subwoofer low-pass

### Output volume cap (anti-clipping safety)

The DSP stage includes a simple output cap that scales PCM samples after Bluetooth volume is applied by the source.

- Set the cap in `main/main.c`:
  - `#define OUTPUT_VOLUME_CAP_PERCENT 85U`
- Range is `0..100`.
  - `100` = no cap (full scale)
  - `85` = output limited to 85% of full scale

This means you can still control volume from your phone normally, but even at phone max the ESP32 output remains capped to reduce clipping risk.

Future PEQ settings are represented by:

```c
typedef struct {
    bool enabled;
    float center_frequency_hz;
    float gain_db;
    float q;
} dsp_peq_settings_t;
```

## Build / flash / monitor (Windows + VS Code ESP-IDF extension)

### Recommended flow (beginner-friendly)

1. Install **ESP-IDF VS Code extension** and complete its setup wizard.
2. In VS Code, open this folder as the project root:
   - `.../audio-firmware-lab/esp32/BTDSP`
3. Press `Ctrl+Shift+P` and run:
   - `ESP-IDF: Set Espressif Device Target` -> select `esp32`
   - `ESP-IDF: Build your project`
   - `ESP-IDF: Flash your project`
   - `ESP-IDF: Monitor your device`

### Command-line flow (ESP-IDF PowerShell / ESP-IDF CMD)

```powershell
cd esp32/BTDSP
idf.py set-target esp32
idf.py build
idf.py -p COM5 flash
idf.py -p COM5 monitor
```

Replace `COM5` with your board's actual COM port.

## UART boot/reset procedure (typical ESP32 dev boards)

If auto-reset/auto-boot does not work:

1. Hold **BOOT**.
2. Press and release **EN/RESET**.
3. Release **BOOT** after the flash tool starts connecting.

Then reset once (EN/RESET) after flashing to boot the app.

Tip for Windows: if flashing fails with a COM-port error, close any open Serial Monitor window first, then retry flash.

## Expected serial logs

You should see logs indicating:

- Bluetooth sink initialized
- Bluetooth connection state changes
- A2DP sample rate/config when provided by source
- I2S started/reconfigured
- DSP stage active (pass-through)

## Assumptions

- Source audio codec from phone is SBC in standard ESP-IDF A2DP sink flow.
- ESP-IDF provides decoded interleaved stereo 16-bit PCM to the A2DP sink data callback.
- External DAC accepts standard I2S (`BCK/LRCK/DATA`) without MCLK.
- No persistent runtime settings are used yet (no NVS menus/control plane).

## Quick edit map (where to change things later)

- **I2S pins:** `main/main.c`
  - `I2S_BCK_GPIO`
  - `I2S_LRCK_GPIO`
  - `I2S_DOUT_GPIO`
- **DSP less-boxy tuning placeholder:** `main/dsp.c` in `dsp_init()` and `dsp_process_stereo_int16()`
  - look for comment: `FUTURE "LESS BOXY" STARTING POINT`
- **Output volume cap:** `main/main.c`
  - `OUTPUT_VOLUME_CAP_PERCENT`
- **Runtime DSP hook usage example:** `main/main.c` near the commented `dsp_set_peq(...)` example in `app_main()`
