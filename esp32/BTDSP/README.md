# BTDSP (ESP-IDF)

Minimal ESP-IDF project for **ESP32-WROOM-32UE-N4CT** that receives Bluetooth Classic A2DP audio from a phone, runs it through a DSP stage (currently pass-through), and outputs stereo PCM over I2S to an external DAC.

## Project summary

- **Framework:** ESP-IDF
- **Target:** `esp32`
- **Input:** Bluetooth Classic A2DP Sink
- **Pipeline:** `Bluetooth PCM -> DSP stage -> I2S output`
- **Output:** I2S stereo transmit (master mode)
- **No Wi-Fi / no UI / no networking extras**

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

Future PEQ settings are represented by:

```c
typedef struct {
    bool enabled;
    float center_frequency_hz;
    float gain_db;
    float q;
} dsp_peq_settings_t;
```

## Build / flash / monitor

From this folder:

```bash
cd esp32/BTDSP
idf.py set-target esp32
idf.py build
idf.py -p /dev/ttyUSB0 flash
idf.py -p /dev/ttyUSB0 monitor
```

Use your serial port in place of `/dev/ttyUSB0`.

## UART boot/reset procedure (typical ESP32 dev boards)

If auto-reset/auto-boot does not work:

1. Hold **BOOT**.
2. Press and release **EN/RESET**.
3. Release **BOOT** after the flash tool starts connecting.

Then reset once (EN/RESET) after flashing to boot the app.

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
