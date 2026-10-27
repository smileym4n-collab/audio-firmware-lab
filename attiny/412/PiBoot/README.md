# PiBoot — ATtiny412 power controller for a Raspberry Pi 4B / Volumio streamer

Firmware v0.1.0. **Not yet tested on hardware.** Only the host-side state-machine tests have been run.

The ATtiny412 is always powered from 3V3-TINY. It switches the Pi's 5 V rail through a TPS22975, asks Linux to shut down through GPIO23, and removes power only after the Pi's `gpio-poweroff` overlay raises GPIO22.

## Files

| File | Purpose |
|---|---|
| `PiBoot.ino` | Pin setup, watchdog and pin I/O only |
| `pi_power_fsm.c/.h` | State machine with no hardware dependencies (shared with the tests) |
| `pi_power_config.h` | **All tunable constants and polarity options** |
| `tests/` | Host test suite (`make -C tests`) |

## Wiring (from schematic, U2)

| ATtiny pin | SOIC pin | Net | Notes |
|---|---|---|---|
| VCC | 1 | 3V3-TINY | C11 100 nF decoupling |
| PA6 | 2 | POWER_BUTTON | No pull resistor on this sheet; internal pull-up used |
| PA7 | 3 | STATUS_LED | No series resistor on this sheet; must be on the panel side |
| PA1 | 4 | TPS_EN | Direct to TPS22975 ON; **no pull-down shown** |
| PA2 | 5 | Q1 gate | R3 100k pull-down; Q1 drain = PI_SHUTDOWN_REQ (Pi GPIO23) |
| PA0 | 6 | PROG | UPDI only |
| PA3 | 7 | SAFE_TO_OFF_IN | R2 100k pull-down; direct to Pi GPIO22 |
| GND | 8 | GNDD | Must be common with Pi ground |

### Electrical risks and open questions

1. **TPS_EN floats during any ATtiny reset.** Reset tri-states PA1, and this sheet shows no pull-down on the TPS22975 ON pin. Check the TPS sheet. If there is none, fit about 100k from ON to GND so that reset, programming and an unprogrammed chip give a *defined* OFF rather than a floating input.
2. **Power-button polarity is unverified.** The firmware assumes the button shorts PA6 to GND and uses the internal pull-up (about 20–50k). If the button switches to 3V3, set `BUTTON_ACTIVE_LOW 0` and fit an external pull-down. Long front-panel wires may need an RC filter. Firmware debouncing handles bounce but not strong EMI.
3. **LED polarity and current limiting are unverified.** Set `LED_ACTIVE_HIGH` to match the panel. A tinyAVR pin can source or sink about 20 mA at most, so there must be a series resistor somewhere.
4. **The 2N7002 gate is driven at 3.3 V.** Vgs(th) is up to about 2.5 V depending on the vendor, which is marginal but fine for sinking the Pi's roughly 50k pull-up. On the bench, confirm GPIO23 drops below 0.8 V while PA2 is HIGH.
5. **GPIO22 connects directly to PA3 with no series resistor.** That is fine because PA3 is only ever an input. A 1k series resistor would protect both sides if either pin were ever misconfigured as an output while the other side is unpowered.
6. **Voltage levels.** All signals are in the 3.3 V domain on both sides, so no level shifting is needed. PI_SHUTDOWN_REQ is open-drain, so it cannot back-power an unpowered Pi.
7. **UPDI programming resets the ATtiny, which cuts Pi power.** Program only with the Pi shut down or disconnected.

## Building and programming

This is built and tested against megaTinyCore 2.6.9.

Board settings:

- Chip: ATtiny412
- Clock: **10 MHz internal or lower.** 16 and 20 MHz are out of spec at 3.3 V, and the sketch `#error`s above 10 MHz.
- BOD: 2.6 V, enabled (recommended)
- Reset pin: in 2.6.9 this is fixed to **UPDI** for 8-pin parts, so there is no menu option for it. Never change the reset-pin fuse: selecting GPIO or reset locks out UPDI without HV programming.
- WDT timeout/window menus: leave them at the default (disabled). The sketch enables the watchdog itself.
- Programmer: SerialUPDI, using a USB-serial adapter plus a 470 Ω–4.7k resistor to UPDI

### Installing the core

The usual route is the Boards Manager URL `http://drazzy.com/package_drazzy.com_index.json`. On 2026-10-27 that index failed to download because drazzy.com's TLS certificate had expired. Do not work around this by disabling certificate checks.

Instead, install the core manually from the official GitHub repo into the sketchbook. Clone it:

```bash
git clone --depth 1 --branch 2.6.9 https://github.com/SpenceKonde/megaTinyCore.git /tmp/mtc
```

Then move it into place:

```bash
mkdir -p ~/Documents/Arduino/hardware/megaTinyCore && mv /tmp/mtc/megaavr ~/Documents/Arduino/hardware/megaTinyCore/megaavr
```

The 2.6.9 `platform.txt` sets `version=2.6.9.1`, which arduino-cli rejects. Edit that line (line 19) to `version=2.6.9`.

A manual install uses the already-installed `arduino:avr-gcc 7.3.0-atmel3.6.1-arduino7` toolchain rather than the Azduino toolchain the Boards Manager would fetch. That toolchain supports the ATtiny412 and builds this sketch cleanly.

### Building and flashing

`arduino-cli` below is the CLI bundled with the Arduino IDE:

`/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli`

Compile:

```bash
arduino-cli compile -b "megaTinyCore:megaavr:atxy2:chip=412,clock=10internal,bodvoltage=2v6,bodmode=enabled" --warnings all .
```

Write fuses once. This sets the clock and BOD; UPDI is unaffected:

```bash
arduino-cli burn-bootloader -b "megaTinyCore:megaavr:atxy2:chip=412,clock=10internal,bodvoltage=2v6,bodmode=enabled" -P serialupdi57k -p /dev/cu.usbserial-XXXX
```

Upload:

```bash
arduino-cli upload -b "megaTinyCore:megaavr:atxy2:chip=412,clock=10internal,bodvoltage=2v6,bodmode=enabled" -P serialupdi57k -p /dev/cu.usbserial-XXXX .
```

In the Arduino IDE, the same choices are under **Tools** (Board, Clock, BOD, Programmer), followed by **Burn Bootloader** and then **Upload Using Programmer**.

### Memory

This is the full sketch: megaTinyCore 2.6.9, 10 MHz internal clock, avr-gcc 7.3.0, no warnings.

| | Used | Max | |
|---|---|---|---|
| Flash | 2232 B | 4096 B | 54% |
| RAM (globals) | 38 B | 256 B | 14%, leaving 218 B for the stack |

The state-machine core alone is 1162 B of that flash. The rest is megaTinyCore's `millis()`, `digitalWrite()`/`digitalRead()` and startup code.

## State machine

| State | TPS_EN | PA2 | LED | Leaves when |
|---|---|---|---|---|
| `STANDBY` | LOW | LOW | off | Debounced press, if at least `MIN_OFF_TIME_MS` (3 s) since power-off → `BOOTING` |
| `BOOTING` | HIGH | LOW | slow blink (1 Hz) | `BOOT_PROTECT_MS` (20 s) elapsed **and then** GPIO22 stable LOW for `SAFE_LOW_QUALIFY_MS` (1 s) → `RUNNING` (armed) |
| `RUNNING` | HIGH | LOW | solid | Short press → `SHUTTING_DOWN`; qualified GPIO22 HIGH → `POWER_OFF_SETTLE` (Volumio-initiated shutdown) |
| `SHUTTING_DOWN` | HIGH | HIGH for the first 500 ms | fast blink (5 Hz) | Qualified HIGH → `POWER_OFF_SETTLE`; `SHUTDOWN_TIMEOUT_MS` (120 s) → `SHUTDOWN_TIMEOUT` |
| `SHUTDOWN_TIMEOUT` | **HIGH** | LOW | double blink | Qualified HIGH → `POWER_OFF_SETTLE`; short press → `SHUTTING_DOWN` (request sent again) |
| `POWER_OFF_SETTLE` | HIGH | LOW | fast blink | `POWER_OFF_SETTLE_MS` (1 s) → `STANDBY` |
| *any powered state* | | | | Button held for `EMERGENCY_HOLD_MS` (8 s) → `STANDBY` immediately |

```
STANDBY --press--> BOOTING --protect + LOW--> RUNNING --press--> SHUTTING_DOWN --timeout--> SHUTDOWN_TIMEOUT
   ^                                            |                   |                        |   ^
   |                                            | GPIO22 HIGH       | GPIO22 HIGH            |   |press (retry)
   +-------------- POWER_OFF_SETTLE <-----------+-------------------+------------------------+---+
   +<============ 8 s hold from any powered state (emergency) ============
```

Rules:

- **Qualified HIGH** means GPIO22 stable HIGH for `SAFE_HIGH_QUALIFY_MS` (500 ms), and only after detection was armed by a qualified LOW. GPIO22 is treated as "not proven LOW" at every power-on and is ignored entirely during the protection window, so a startup HIGH or a glitch can never cut power.
- **Short press** means a debounced press-then-release that started in the current state and was released before 8 s. A long hold in `RUNNING` sends **no** graceful request; it reaches the emergency off instead.
- **Button presses are ignored** during `BOOTING`, `SHUTTING_DOWN` and `POWER_OFF_SETTLE`, so repeated presses can't interrupt a boot or a shutdown. Only the 8 s hold works there.
- **Every power-on press is consumed.** A button held after powering on must be released before it can count towards an emergency hold, so a long power-on press can't immediately kill the Pi.
- **A button held while the ATtiny powers up** is ignored until it is released.
- **Unknown state values** (RAM corruption, for example) fall back to `STANDBY`.

## Limitations and hardware assumptions

- **There is no boot-complete signal.** `RUNNING` (solid LED) means only that the 20 s protection window has passed and GPIO22 has read LOW. It does **not** prove Linux has booted. If the button is pressed before `gpio-shutdown` has loaded, the request is lost, and the Pi stays powered with the LED in fast blink and then double blink. Press again from the timeout state to retry.
- **The timeout never cuts power.** If the Pi hangs or crashes, it never raises GPIO22, and power stays on until you use the 8 s emergency hold.
- **The emergency off is a hard power cut.** It can corrupt the SD card or Volumio's data partition. Use it for recovery only.
- **ATtiny reset behaviour.** Any reset (watchdog, brownout, UPDI, glitch on VCC) tri-states PA1. `setup()` then drives TPS_EN LOW, so **any ATtiny reset cuts Pi power.** Firmware cannot prevent this. Keeping the Pi up through a reset would need a hardware latch or hold-on circuit, which this PCB does not have. Even with a latch, the firmware has no way to tell after a reset whether the Pi was running, because GPIO22 is LOW both when the Pi is off and when it is running.
- **Watchdog.** The watchdog is enabled with a timeout of about 1 s, and it is kicked every `loop()` (which runs in microseconds). It fires only if the firmware hangs, and the result is a power cut, as above. Without it, a hung ATtiny would leave the Pi powered but impossible to shut down or power-cycle from the button. Set `ENABLE_WATCHDOG 0` if you prefer that failure mode.
- **Brownout detection.** BOD at 2.6 V holds the ATtiny in reset during a 3V3-TINY sag instead of letting it execute garbage. That still cuts Pi power (see the reset item above).
- **No EEPROM writes**, and no serial debug: the 412's UART pins are all in use.

## Raspberry Pi / Volumio configuration

Volumio regenerates `/boot/config.txt` on updates. Put user overlays in **`/boot/userconfig.txt`**, and check that this mechanism still applies on your Volumio 4 image.

```ini
dtoverlay=gpio-shutdown,gpio_pin=23,active_low=1,gpio_pull=up,debounce=200
dtoverlay=gpio-poweroff,gpiopin=22
```

- **`gpio-shutdown`** reports a power-key press when GPIO23 goes LOW, and systemd-logind then runs a poweroff. `gpio_pull=up` uses the Pi's own internal pull-up to its switched 3.3 V rail, which is what the open-drain Q1 needs. The ATtiny's 500 ms pulse is well above the 200 ms debounce and well below logind's long-press time.
- **`gpio-poweroff`** holds GPIO22 LOW (inactive) from kernel start. At the very end of poweroff, after filesystems are unmounted, it drives the pin HIGH for about 100 ms, LOW for about 100 ms, then HIGH and leaves it there. The firmware's 500 ms HIGH qualification rejects the first pulse and accepts the final level. The overlay does **not** assert the pin on reboot, so `sudo reboot` keeps power on.
- **Before the kernel loads, GPIO22 is an input** held LOW by the default pull-down and R2. That is why the firmware never trusts it during the boot window.
- **Pi 4 bootloader EEPROM:** leave `POWER_OFF_ON_HALT=0` (the default), so the Pi's 3.3 V rail, and therefore the GPIO22 HIGH, stays up after halt. Check with `rpi-eeprom-config`.

Verify on the Pi:

- `dmesg | grep -i gpio` should show both overlays.
- Tying GPIO23 to GND briefly should start a shutdown; `journalctl -b -1` should show "Power key pressed".
- `pinctrl get 22` should read `lo` while running. Measure the pin with a meter after halt to check it reads HIGH.

## Testing without a Pi

### Host tests (automated)

```bash
make -C tests
```

The suite steps the state machine once per simulated millisecond and covers:

- normal power-on, normal shutdown and Volumio-initiated shutdown
- boot-time GPIO22 glitches, and GPIO22 stuck HIGH during boot
- arming timing
- 100 ms GPIO22 pulses (rejected)
- short presses during boot and shutdown
- button bounce, a button held at power-up, and the minimum-off lockout
- shutdown timeout with indefinite power hold, retry and late completion
- the 8 s emergency hold from running and from timeout, and a 7 s hold (graceful, not forced)
- ATtiny reset and corrupted state
- 50 repeated power cycles
- `millis()` wrap

Tunables come from `pi_power_config.h`, so the tests follow any changes you make there.

### Bench checklist (dummy load, no Pi)

Setup:

- On the TPS output, use a 5 V LED with a resistor, or a power resistor of about 10 Ω / 5 W (about 0.5 A) for a load test.
- **GPIO22 simulator:** a switch from SAFE_TO_OFF_IN through 1k to 3.3 V. R2 provides the LOW.
- **GPIO23 indicator:** pull PI_SHUTDOWN_REQ up to 3.3 V through 10k, and put a scope or LED on it.

Checklist:

- [ ] Before programming: measure TPS_EN with the ATtiny unprogrammed or held in reset. It should be LOW, or else floating, which confirms risk 1.
- [ ] At power-up: TPS off, LED off, PI_SHUTDOWN_REQ high (inactive).
- [ ] Button polarity: confirm a press is detected. Otherwise flip `BUTTON_ACTIVE_LOW`.
- [ ] LED polarity: confirm the LED is off in standby. Otherwise flip `LED_ACTIVE_HIGH`.
- [ ] Press: the load turns on and the LED blinks slowly.
- [ ] Switch GPIO22 HIGH immediately after power-on: there should be no power-off at any point during boot.
- [ ] GPIO22 LOW: after about 21 s, the LED goes solid.
- [ ] Short press while running: one LOW pulse of about 500 ms on PI_SHUTDOWN_REQ, then fast blink. Further presses should make no new pulse.
- [ ] GPIO22 HIGH for under 0.5 s: nothing happens. Held HIGH: power goes off about 1.5 s later.
- [ ] Running, then GPIO22 HIGH without pressing the button (Volumio path): power goes off.
- [ ] Shutdown with GPIO22 left LOW: after 120 s, a double blink, and the load **stays on** for at least 10 minutes.
- [ ] Short press in timeout: the pulse is sent again.
- [ ] 8 s hold, in each powered state: the load goes off after 8 s and stays off while the button is held.
- [ ] Hold the button for 10 s during power-on: the load stays on.
- [ ] Power on again within 3 s of power-off: ignored.
- [ ] Short UPDI or VCC briefly to force a reset while running: confirm the load drops. This is expected.
- [ ] Sag 3V3-TINY: the load drops cleanly with no chatter (BOD).
- [ ] 50 power cycles of 30 s each.
- [ ] Then with a real Pi: a Volumio menu shutdown, a button shutdown, `sudo reboot` (power must stay on), and GPIO23 measured below 0.8 V during the pulse.
