# InputSwitch firmware

ATtiny412 / megaTinyCore firmware for the InputSwitch audio selector and P3A
automatic power controller. **Compiled and tested on the host; not bench tested.**

The maintained panel toggle selects preamp/Reference or Marantz/AV. Audio from
either channel of the selected source wakes the amplifier. Ten minutes without
qualified audio causes a muted shutdown. This operates in both source positions.
Changing source alone does not wake the amplifier.

## Files

| File | Purpose |
| --- | --- |
| `InputSwitch.ino` | Early output initialization, pin I/O, 1 ms polling, watchdog |
| `input_switch_config.h` | All adjustable timing constants |
| `input_switch_fsm.c/.h` | Hardware-independent source and amplifier state machines |
| `tests/` | Host tests for the same C code used on the chip |
| `build.sh` | Reproducible compile with the specified board/fuse options |

Open `InputSwitch.ino` in Arduino IDE, or use `sh build.sh` in this directory.
There are no extra Arduino libraries, dynamic allocation, EEPROM writes, or
serial debug. The UART pins are already used by this board.

## Fixed wiring

Verified against the saved KiCad project at
`/Users/tomwatson/Documents/GitHub/Kicadprojects/InputSwitch` on 27 October 2026:
31 firmware-related schematic assertions and 86 relevant PCB pad/net matches.
This is a firmware connectivity check, not a new complete hardware review.

| U2 SOIC pin | Port | Net | Meaning |
| --- | --- | --- | --- |
| 1 | VDD | +5V | Permanent controller supply |
| 2 | PA6 | P3A_ON | HIGH energizes external G7L power relay via U4 |
| 3 | PA7 | TOGGLE_AV | HIGH = AV; LOW = Reference; external 47k pull-down |
| 4 | PA1 | SOURCE_AV | HIGH = AV; LOW = Reference; drives K1/K2/K4 and LEDs |
| 5 | PA2 | MUTE | HIGH energizes K3, disconnecting source signals and shorting amp inputs to their returns |
| 6 | PA0 | UPDI | Programming only; never configured by this sketch |
| 7 | PA3 | AUDIO_PRESENT_N | LOW = recent audio; external 47k/2.2u RC envelope |
| 8 | GND | GND | Control ground |

J9: pin 1 Reference throw/GND, pin 2 toggle common, pin 3 AV throw/+5V.
An open/disconnected toggle defaults to Reference. Neither input uses an
internal pull-up, so the external pull-down and audio envelope remain effective.

K4 and both source LEDs follow SOURCE_AV in hardware. J13.1 therefore supplies
protected 12 V whenever AV is selected, including while muted or in amplifier
standby. J13.2 is control GND. The firmware cannot control K4 independently.
LEDs show the commanded source, not verified relay contact positions.

The two isolated detectors are combined into one active-low input. Either
selected channel can wake the amplifier; firmware cannot identify which channel
is active or detect an inactive source. Detectors tap before K3, so muting does
not prevent audio detection. Analogue detection sensitivity is set by hardware,
not a software level control.

## Behaviour and initial bench settings

| Setting | Default | Purpose |
| --- | --- | --- |
| `IS_TOGGLE_DEBOUNCE_MS` | 20 ms | Stable switch input before accepting a request |
| `IS_STARTUP_BLANK_MS` | 2000 ms | Ignore detector settling at controller startup |
| `IS_SOURCE_PRE_MUTE_MS` | 75 ms | Assert mute before changing SOURCE_AV |
| `IS_SOURCE_SETTLE_MS` | 150 ms | Keep muted after changing SOURCE_AV |
| `IS_SOURCE_BLANK_MS` | 1000 ms | Ignore detector transients after source settling |
| `IS_AUDIO_QUALIFY_MS` | 500 ms | Require sustained/repeated recent audio |
| `IS_AUDIO_MAX_GAP_MS` | 50 ms | Allowed brief HIGH gap within an audio activity train |
| `IS_MAX_SAMPLE_GAP_MS` | 20 ms | Discard audio qualification after a stalled polling loop |
| `IS_POWER_WARMUP_MS` | 2000 ms | Hold mute after applying amplifier power |
| `IS_SILENCE_TIMEOUT_MS` | 600000 ms | Ten minutes without qualified audio |
| `IS_POWER_OFF_MUTE_MS` | 200 ms | Hold mute before removing amplifier power |
| `IS_POST_OFF_LOCKOUT_MS` | 10000 ms | Ignore all audio after amplifier power-off |

Startup/source blanking durations and the allowed audio gap are explicit initial
firmware choices. All settings require confirmation on the actual board/amplifier.

1. Before core initialization, preload MUTE HIGH and source/power LOW, then enable
   those three outputs. Debounce the toggle and select the requested source under
   mute. Keep power off through startup blanking and any active source sequence.
2. In standby, hold MUTE HIGH and P3A_ON LOW. A LOW audio sample starts a fresh
   500 ms qualification period. HIGH gaps up to 50 ms are tolerated; longer gaps
   discard qualification. Only a LOW sample can qualify audio or refresh the
   running silence timer. The existing hardware envelope has a nominal 103 ms
   release RC; an isolated click should not qualify.
3. On qualified audio, keep mute asserted and set P3A_ON HIGH. After 2 seconds,
   unmute only if no source sequence is in progress. Audio qualification is cleared
   at each power transition. A disappearing signal during warm-up does not abort
   power-on; the full silence window begins when warm-up finishes.
4. Source changes follow mute -> 75 ms -> source command -> 150 ms -> conditional
   unmute. Amplifier power stays on if already on. Debounced requests received
   during settling are serialized; a pre-mute request that returns to the current
   source is cancelled before changing the relay command. No unmuted interval is
   inserted between queued source changes. Detector blanking restarts after final
   settling. Each completed source change while running grants a fresh ten-minute
   silence window. The 1-second detector blank does not unnecessarily extend mute.
5. After ten minutes without qualified audio, assert mute, wait 200 ms, then remove
   amplifier power. Once shutdown starts, finish it even if audio returns. Remain
   muted during the ten-second lockout, then require a new full qualification.
   Source selection still works during shutdown/lockout and never bypasses it.

The loop has no blocking delays. Time comparisons use unsigned elapsed intervals
and are tested across the 32-bit `millis()` rollover.

## Build and programming

Verified toolchain: Arduino CLI 1.4.1, locally installed megaTinyCore 2.6.9,
avr-gcc 7.3.0-atmel3.6.1-arduino7. Full sketch: **1862 / 4096 bytes flash,
57 / 256 bytes static RAM**, leaving 199 bytes for stack/local variables.
Those figures exclude run-time stack usage; this is not a measured stack peak.

Board settings:

- Board: ATtiny412/212 without Optiboot; chip ATtiny412.
- Clock: **10 MHz internal**. This leaves CPU operating-voltage margin below the
  nominal 4.2 V brownout threshold; the sketch rejects clocks above 10 MHz.
- millis: Enabled, default TCD0 timer. Startup time: 8 ms.
- Brownout voltage: **4.2 V**; mode **Enabled/Enabled**.
- PA0: **UPDI**. Keep the core's default; do not enable GPIO/reset expert options.
- WDT timeout/window fuses: Disabled. The sketch enables a normal approximately
  one-second watchdog in setup. It is serviced after each completed control tick.
- Other menu selections are recorded in `build.sh`.

The verified build options expand to BODCFG `0xE5`, OSCCFG `0x02`,
SYSCFG0 `0xC5` (UPDI retained), SYSCFG1 `0x04`, and WDTCFG `0x00`.
These are expected fuse settings for readback, not a claim that a chip has
been programmed. The runtime clock prescaler gives 10 MHz from the selected
20 MHz oscillator.

Compile:

```sh
sh build.sh
```

Set `ARDUINO_CLI=/path/to/arduino-cli` if necessary. `BUILD_DIR=/some/path`
overrides the default ignored `build/` directory. Its `InputSwitch.ino.hex`
is regenerated from the current source; always rebuild after changing constants.

**Compilation/upload alone does not apply the BOD fuses.** In Arduino IDE use
Tools -> Burn Bootloader once with the settings above (this configures the fuses
for this no-bootloader board), then Upload Using Programmer. Reapply fuses if
clock/BOD settings change.

Equivalent CLI commands, after selecting the actual programmer and port:

```sh
CLI='/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli'
FQBN='megaTinyCore:megaavr:atxy2:chip=412,clock=10internal,millis=enabled,startuptime=8,bodvoltage=4v2,bodmode=enabled,eesave=enable,printf=default,wiremode=mors,WDTtimeout=disabled,WDTwindow=disabled,PWMmux=default,attach=allenabled'
# serialupdi57k is an example; use the programmer that matches your hardware.
"$CLI" burn-bootloader -b "$FQBN" -P serialupdi57k -p /dev/cu.usbserial-XXXX
"$CLI" upload -b "$FQBN" -P serialupdi57k -p /dev/cu.usbserial-XXXX --input-dir build .
```

J8 is pin 1 UPDI, pin 2 target +5 V reference, pin 3 GND. Power the board from its
normal supply; the programmer must not back-power the board. No programming or
fuse writes were performed as part of this firmware delivery.

Core documentation: https://github.com/SpenceKonde/megaTinyCore

Device datasheet (pinout, BOD/fuse and watchdog chapters):
https://ww1.microchip.com/downloads/aemDocuments/documents/MCU08/ProductDocuments/DataSheets/ATtiny212-214-412-414-416-DataSheet-DS40002287A.pdf

## Automated tests

```sh
make -C tests test
make -C tests sanitize
```

Both pass on the Mac: 15 scenarios, with output invariants checked on every
simulated millisecond. Coverage includes both startup positions, silence, isolated
clicks, allowed/excessive audio gaps, exact relay sequencing, toggle bounce,
cancelled/queued source requests, source blanking, switching during warm-up,
shutdown and lockout, activity timeout refresh, full new-source silence windows,
delayed polling, software reset/invalid states, repeated cycles and timer rollover.
Sanitizers check memory access and undefined behaviour in the shared C controller.
These tests do not simulate relay contacts, detector analogue behaviour, MCU
interrupt timing, watchdog operation, or power rails.

## Bench checks

Start by checking the logic and coil commands with the amplifier mains relay
contacts disconnected. Monitor PA1, PA2, PA6, and PA3 with a scope/logic analyser
referenced to **control GND**. Keep the isolated audio returns isolated.

- Power-up in each toggle position: P3A_ON stays LOW, MUTE goes HIGH promptly,
  correct source is selected under mute, and no wake occurs from startup clicks.
- In standby, briefly pull AUDIO_PRESENT_N to control GND through 1k: a roughly
  100 ms pulse must not wake it. Hold LOW: wake occurs after blanking plus 500 ms;
  mute remains asserted for another 2 seconds. This tests logic, not sensitivity.
- Check real audio independently on left and right, including quiet audio, bass,
  pauses and abrupt starts. Observe whether the 50 ms gap allowance needs tuning.
- Switch sources while playing: verify the 75/150 ms sequence, pickup/release of
  both selector relays, correct LEDs, and K4/J13 following AV. Try rapid toggling.
- Remove audio: verify ten-minute timeout, mute 200 ms before power-off, ten-second
  lockout, and fresh qualification afterwards. An isolated click must not restart
  the silence timer. For quick initial tests you can temporarily reduce
  IS_SILENCE_TIMEOUT_MS to 10000; restore 600000 and rebuild before normal use.
- Confirm AV's subwoofer trigger remains active during mute and standby, and
  disconnects when selecting Reference.
- Check reset, programming and supply sag behaviour before connecting the actual
  amplifier. Confirm BOD fuses by programmer readback and test watchdog recovery.
- With the real amplifier connected, confirm that 2 s warm-up and 200 ms shutdown
  mute actually cover its transients; adjust from measurements.

## Hardware limits firmware cannot remove

- **Reset/power loss releases K3.** Hardware defaults to Reference pass-through,
  power command off and sub-trigger disconnected. The early initialization hook
  minimizes the software delay before muting but cannot mute during reset or
  absent controller power. Watchdog/brownout/UPDI reset can therefore make an
  audible transient and drop amplifier power; pop-free reset is not guaranteed.
- No feedback exists for amplifier rail readiness, relay contacts, temperature or
  actual power state. Delays indicate commands and elapsed time only.
- A stuck-LOW detector is indistinguishable from sustained audio and will keep
  the amplifier on. Actual threshold/noise immunity must be bench verified.
- Existing ULN2003A/G6K pickup margin and independent selector-relay timing remain
  hardware qualification items. Muting signal contacts cannot prevent a transient
  source-return connection through externally joined L/R returns.

The KiCad schematic, PCB and project settings were not modified for this firmware.
