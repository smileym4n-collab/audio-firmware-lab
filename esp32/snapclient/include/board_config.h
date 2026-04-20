#pragma once

#include <Arduino.h>

namespace board_config {

// ---------- User-editable hardware pin assignments ----------
static constexpr int I2S_BCLK_PIN = 26;   // I2S bit clock to external DAC BCLK/SCK
static constexpr int I2S_LRCLK_PIN = 25;  // I2S word select / LRCLK to DAC WS
static constexpr int I2S_DOUT_PIN = 22;   // I2S serial data output to DAC DIN

// Classic ESP32 only supports I2S MCLK on GPIO0, GPIO1, or GPIO3.
// GPIO0 is the least disruptive default here because GPIO1/GPIO3 are UART0.
static constexpr int I2S_MCLK_PIN = 0;  // I2S master clock to external DAC MCLK/XTI

// Boot-time mode select button. Default wiring is a simple switch to GND.
// Hold or press this momentary button during boot to force Bluetooth mode.
static constexpr int BOOT_MODE_BUTTON_PIN = 32;
static constexpr bool BOOT_MODE_BUTTON_USE_PULLUP = true;
static constexpr int BOOT_MODE_BUTTON_ACTIVE_LEVEL = LOW;

// Single mode-status LED. Default wiring is LED + resistor from GPIO to GND.
// Change MODE_STATUS_LED_ACTIVE_HIGH if your LED is wired to 3V3 instead.
static constexpr int MODE_STATUS_LED_PIN = 33;
static constexpr bool MODE_STATUS_LED_ACTIVE_HIGH = true;

}  // namespace board_config
