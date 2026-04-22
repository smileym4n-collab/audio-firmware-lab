# ClassicCube

Version: 0.1.0

ATtiny1616 Arduino sketch for a simple stereo preamp controller with:
- PGA2311 volume control from a `0V..3.3V` potentiometer
- PGA2311 mute pin handling for quiet startup and source changes
- one relay-based input select driven through a `ULN2003A`
- one toggle-switch input that decides whether the relay is energized

The PGA2311 control path intentionally follows the same serial-write and mute approach used in [PreAmpv2.ino](C:/audio-firmware-lab/attiny/1616/PreAmpv2/PreAmpv2.ino), because the PGA2310 and PGA2311 are programming-compatible.

## MCU / framework
- MCU: `ATtiny1616`
- Framework: Arduino (`megaTinyCore`)
- IDE: Arduino IDE
- Logic rail: `3.3V`

## Pin map
All pin assignments are grouped at the top of [ClassicCube.ino](C:/audio-firmware-lab/attiny/1616/ClassicCube/ClassicCube.ino) so you can retarget them quickly.

Default placeholders are:
- `PA1` - relay driver output to `ULN2003A`
- `PA2` - input toggle switch
- `PA4` - PGA2311 `MUTE`
- `PA5` - PGA2311 `SDI`
- `PB3` - PGA2311 `SCLK`
- `PA3` - PGA2311 `CS`
- `PB1` - volume pot ADC input

## Behavior
- Startup is muted by default.
- The sketch reads the current toggle-switch position during `setup()` and applies the relay state before unmuting.
- The relay logic is reversible in code with `INPUT_SWITCH_HIGH_SELECTS_RELAY_ON`.
- The relay output polarity is also easy to change with `RELAY_ACTIVE_STATE` / `RELAY_INACTIVE_STATE`.
- Volume pot readings are averaged before being mapped to PGA2311 gain codes.
- The volume taper reuses the same 3-segment mapping style as `PreAmpv2`, tuned for a linear pot to feel more natural.
- The default maximum gain cap is `0.0 dB` for conservative bring-up; change `PGA_MAX_DB` if you want more headroom.
- During a debounced input change, the PGA mute pin is asserted briefly, the relay is switched, then audio is restored.

## Hardware assumptions
- The toggle switch presents a solid logic `LOW` or `HIGH` at the MCU pin and does not float.
- The default switch configuration therefore uses `INPUT`, not `INPUT_PULLUP`.
- The `ULN2003A` input is treated as active-high by default.
- The PGA2311 mute pin is treated as active-low, matching the behavior used in `PreAmpv2`.

## Arduino IDE notes
- Board package: `megaTinyCore`
- Board: `ATtiny1616`
- No external Arduino libraries are required for this sketch.

## Bring-up checklist
1. Set the real pin assignments at the top of [ClassicCube.ino](C:/audio-firmware-lab/attiny/1616/ClassicCube/ClassicCube.ino).
2. Confirm the relay polarity with your `ULN2003A` stage.
3. Confirm whether switch-high should energize or de-energize the relay.
4. If your switch wiring needs it, set `INPUT_SWITCH_USE_INTERNAL_PULLUP = true` and adjust the logic sense.
5. If full-scale volume is too low or too hot, retune `PGA_MAX_DB`.
