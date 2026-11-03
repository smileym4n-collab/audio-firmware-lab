# NJW1195A test firmware (ESP32-S3)

Bench test firmware for the NJW1195A 4-channel volume / 4-input selector.
It controls input selection and volume from a serial console, to prove the
hardware before the final ATtiny firmware is written.

## Wiring (ESP32-S3 DevKitC-1)

| ESP32-S3 | NJW1195A | Notes |
|----------|----------|-------|
| GPIO4    | 14 DATA  | |
| GPIO5    | 15 CLOCK | idles high |
| GPIO6    | 16 LATCH | latches on rising edge |
| GND      | 0 V / GND | common ground with the audio board |

- DIFF (pin 22) low for 4-input single-ended mode.
- ADR0/ADR1 (pins 19/20) low for chip address 0 (`CHIP_ADDR` in `main.cpp`).
- Logic VIH min is 2.5 V, so the 3.3 V GPIOs drive it directly.

## Serial commands (115200 baud, newline terminated)

| Command | Action |
|---------|--------|
| `1`..`4` | input for Out1 |
| `i <0-4>` | input for Out1 (0 = mute) |
| `i2 <0-4>` | input for Out2 (0 = mute) |
| `v <dB>` | volume -95 to +31.5 dB, 0.5 dB steps |
| `+` / `-` | volume up / down 1 dB |
| `m` | mute toggle |
| `w` | re-send all registers |
| `r <hex>` | send raw 16-bit word |
| `s` / `h` | status / help |

Boots with Out1 on input 1, Out2 muted, volume muted at a -30 dB preset.

## Protocol summary (datasheet Ver 0.4E)

16-bit word, MSB first: D15-D8 data, D7-D4 select address, D3-D0 chip address.

| Select | Register |
|--------|----------|
| 0 / 1 | volume 1A / 1B |
| 2 / 3 | volume 2A / 2B |
| 4 / 5 | selector A / B (SEL1 in D15-D13, SEL2 in D12-D10) |

Volume data: `0x01` = +31.5 dB, `0x40` = 0 dB, `0xFE` = -95 dB, `0x00`/`0xFF` = mute.
Selector data: 0 = mute, 1-4 = input 1-4.

## Build / flash

Use the board's "UART"/"COM" USB port (CH343 USB-serial). Serial is on UART0
(`ARDUINO_USB_CDC_ON_BOOT=0`). Upload runs at 230400 because the CH343 gives
packet errors at 460800 and above.

```
pio run -t upload
pio device monitor
```
