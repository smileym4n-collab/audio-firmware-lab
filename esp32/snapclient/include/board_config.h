#pragma once

#include <Arduino.h>

namespace board_config {

// ---------- User-editable hardware pin assignments ----------
static constexpr int I2S_BCLK_PIN = 26;   // I2S bit clock to external DAC BCLK/SCK
static constexpr int I2S_LRCLK_PIN = 25;  // I2S word select / LRCLK to DAC WS
static constexpr int I2S_DOUT_PIN = 13;   // I2S serial data output to DAC DIN

// Optional I2S MCLK output.
// Set I2S_MCLK_ENABLED to false for PCM5102-style DAC modules that do not need MCLK.
// Set it to true only for DACs that explicitly require a dedicated MCLK signal.
static constexpr bool I2S_MCLK_ENABLED = false;

// Classic ESP32 only supports I2S MCLK on GPIO0, GPIO1, or GPIO3.
// GPIO0 is the least disruptive default here because GPIO1/GPIO3 are UART0.
// This pin is only used when I2S_MCLK_ENABLED is true.
static constexpr int I2S_MCLK_PIN = 0;  // I2S master clock to external DAC MCLK/XTI

// Runtime mode-toggle button. Default wiring is a simple momentary switch to GND.
// Cold boot always starts in Snapclient mode.
// Pressing this button while the firmware is running toggles mode and reboots.
static constexpr int BOOT_MODE_BUTTON_PIN = 32;
static constexpr bool BOOT_MODE_BUTTON_USE_PULLUP = true;
static constexpr int BOOT_MODE_BUTTON_ACTIVE_LEVEL = LOW;

// Single mode-status LED. Default wiring is LED + resistor from GPIO to GND.
// Change MODE_STATUS_LED_ACTIVE_HIGH if your LED is wired to 3V3 instead.
static constexpr int MODE_STATUS_LED_PIN = 33;
static constexpr bool MODE_STATUS_LED_ACTIVE_HIGH = true;

// 4S battery monitor input. Use ADC1-capable pins while Wi-Fi is active.
// Good choices on classic ESP32 are GPIO34, GPIO35, GPIO36, and GPIO39.
static constexpr bool BATTERY_SENSE_ENABLED = true;
static constexpr int BATTERY_SENSE_PIN = 34;  // Battery divider output to ADC1 input

}  // namespace board_config
