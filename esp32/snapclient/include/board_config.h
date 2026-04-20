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
static constexpr int BOOT_MODE_BUTTON_PIN = 32;
static constexpr bool BOOT_MODE_BUTTON_USE_PULLUP = true;
static constexpr int BOOT_MODE_BUTTON_ACTIVE_LEVEL = LOW;

// Optional status LED. Set to -1 when the module/carrier has no usable LED.
static constexpr int STATUS_LED_PIN = -1;
static constexpr bool STATUS_LED_ACTIVE_HIGH = true;

}  // namespace board_config
