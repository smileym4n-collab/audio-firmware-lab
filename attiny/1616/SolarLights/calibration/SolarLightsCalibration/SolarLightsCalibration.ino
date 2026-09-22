/*
  SolarLightsCalibration.ino

  Temporary ATtiny1616 firmware for setting the four LM2596 output voltages.
  All regulators are enabled and all four channel outputs are held continuously
  HIGH. There is no PWM modulation, dimming, RF control, or automatic timeout.

  IMPORTANT: This firmware leaves every lighting channel fully powered for as
  long as the board is powered. Install the normal SolarLights firmware after
  calibration.
*/

#include <Arduino.h>

// Fixed PCB pinout. LM2596 ON/OFF inputs are active LOW.
constexpr uint8_t CH1_ENABLE_PIN = PIN_PA4;
constexpr uint8_t CH2_ENABLE_PIN = PIN_PA5;
constexpr uint8_t CH3_ENABLE_PIN = PIN_PA6;
constexpr uint8_t CH4_ENABLE_PIN = PIN_PA7;

constexpr uint8_t CH1_OUTPUT_PIN = PIN_PB0;
constexpr uint8_t CH2_OUTPUT_PIN = PIN_PB1;
constexpr uint8_t CH3_OUTPUT_PIN = PIN_PB2;
constexpr uint8_t CH4_OUTPUT_PIN = PIN_PA3;

constexpr uint16_t LM2596_SETTLE_MS = 50;

void establishSafeOutputsImmediately() {
  // Load output latches before enabling the drivers to avoid startup glitches.
  PORTA.OUTSET = PIN4_bm | PIN5_bm | PIN6_bm | PIN7_bm;  // Regulators OFF.
  PORTA.OUTCLR = PIN3_bm;                                 // CH4 load OFF.
  PORTB.OUTCLR = PIN0_bm | PIN1_bm | PIN2_bm;            // CH1-3 loads OFF.

  PORTA.DIRSET = PIN3_bm | PIN4_bm | PIN5_bm | PIN6_bm | PIN7_bm;
  PORTB.DIRSET = PIN0_bm | PIN1_bm | PIN2_bm;
}

void setup() {
  establishSafeOutputsImmediately();

  // Enable all four active-low LM2596 ON/OFF inputs.
  digitalWrite(CH1_ENABLE_PIN, LOW);
  digitalWrite(CH2_ENABLE_PIN, LOW);
  digitalWrite(CH3_ENABLE_PIN, LOW);
  digitalWrite(CH4_ENABLE_PIN, LOW);

  // Keep the loads disconnected while the regulators start.
  delay(LM2596_SETTLE_MS);

  // Solid HIGH on all channels. No analogWrite() is used, so these are not
  // PWM waveforms; Channel 4 also receives continuous 100% electrical duty.
  digitalWrite(CH1_OUTPUT_PIN, HIGH);
  digitalWrite(CH2_OUTPUT_PIN, HIGH);
  digitalWrite(CH3_OUTPUT_PIN, HIGH);
  digitalWrite(CH4_OUTPUT_PIN, HIGH);
}

void loop() {
  // Calibration state is intentionally held indefinitely.
}
