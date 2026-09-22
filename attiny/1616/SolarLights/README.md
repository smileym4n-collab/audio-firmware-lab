# ATtiny1616 Solar Lights Controller

`SolarLights.ino` implements the four-channel controller for the fixed PCB pinout.

## Recommended megaTinyCore build settings

- Board: `ATtiny3226/3216/1626/1616/...` (`megaTinyCore:megaavr:atxy6`)
- Chip: `ATtiny1616`
- Clock: `20 MHz internal`
- millis()/micros() timer: `TCD0` (the 1-series default)
- PWM pins: `PB0-2, PA3-5, 1-series: PC0/1 (default)`
- UPDI/reset pin: `UPDI`
- EEPROM retained: enabled

Example compile command with megaTinyCore 2.6.11:

```sh
arduino-cli compile \
  --fqbn 'megaTinyCore:megaavr:atxy6:chip=1616,clock=20internal,millis=timerd,PWMmux=A_default,resetpin=UPDI,eesave=enable' \
  SolarLights.ino
```

PB0, PB1 and PB2 are TCA0 WO0, WO1 and WO2 respectively and use hardware PWM.
PA3 is capable of TCA0 WO3, but this firmware deliberately treats it as digital
GPIO so Channel 4 is always fully off or continuously high.

Although the default megaTinyCore menu also exposes possible TCD PWM pins, this
firmware never calls `analogWrite()` on PC0/PC1. TCD0 remains the timekeeping
timer and PC0 remains a high-impedance RF input.

## Calibration

The ADC uses VDD as its reference. Set `ADC_REFERENCE_VOLTS` to the measured 5 V
rail if better battery-voltage accuracy is needed, then adjust
`BATTERY_CALIBRATION` only for residual divider/ADC error. `getBatteryVoltage()`
returns the most recently filtered estimate; it does not control the lights in
this firmware version.

## RF learning

Hold LEARN for about two seconds. Each channel flashes twice before waiting for
its physical remote button, then three times after a valid repeated code is
accepted. All channels flash twice after all four buttons are learned. Hold
LEARN for eight seconds to erase the learned mappings; five all-channel flashes
confirm the erase. Each learning step times out after 60 seconds.

All LEARN, confirmation, timeout, and erase flashes use hard full-duty ON/OFF
switching. Channels 1-3 do not use PWM or the normal two-second fades for these
indications.

The receiver implementation learns normalized short/long pulse timing rather
than assuming one named protocol. It requires three consistent frames and
latches each press until transmissions stop, which supports typical EV1527 and
PT2262-style fixed-code OOK remotes while rejecting isolated RXB6 noise.

With no manual override, all four channels follow the dusk sensor automatically:
they turn on after three continuous dark minutes and return to day mode after 15
continuous light minutes. A learned remote button reverses the current automatic
state for its channel, including during daylight or after the six-hour schedule.
Further presses toggle that override. Overrides are RAM-only and clear at the
next confirmed day/night transition or reset, returning the channel to AUTO.

Channels 1-3 use a fixed two-second perceptual fade when switching on or off.
Channel 4 always switches cleanly at full duty and is never PWM-dimmed.

No serial debug port is enabled: both hardware USART TX routes conflict with the
fixed PCB allocation (PB2 is CH3 PWM and PA1 is DUSK_SENSE).

## LM2596 calibration firmware

`calibration/SolarLightsCalibration/SolarLightsCalibration.ino` is a separate,
temporary sketch for setting the regulator output voltages. It enables all four
LM2596 regulators, waits 50 ms, and then holds all four channel outputs solid
HIGH. It has no fades, PWM modulation, RF control, or automatic timeout. Replace
it with the normal `SolarLights.ino` firmware after calibration.
