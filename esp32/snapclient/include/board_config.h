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
static constexpr int BOOT_MODE_BUTTON_PIN = 23;
static constexpr bool BOOT_MODE_BUTTON_USE_PULLUP = true;
static constexpr int BOOT_MODE_BUTTON_ACTIVE_LEVEL = LOW;

// Dedicated mode-status LEDs. Default wiring is LED + resistor from GPIO to GND.
static constexpr int WIFI_STATUS_LED_PIN = 32;
static constexpr bool WIFI_STATUS_LED_ACTIVE_HIGH = true;
static constexpr int BT_STATUS_LED_PIN = 33;
static constexpr bool BT_STATUS_LED_ACTIVE_HIGH = true;

// Backward-compatible aliases for older code/docs that refer to the single
// status LED. The Wi-Fi LED is the normal Snapclient-mode status indicator.
static constexpr int MODE_STATUS_LED_PIN = WIFI_STATUS_LED_PIN;
static constexpr bool MODE_STATUS_LED_ACTIVE_HIGH = WIFI_STATUS_LED_ACTIVE_HIGH;

// 4S battery monitor input. Use ADC1-capable pins while Wi-Fi is active.
// Good choices on classic ESP32 are GPIO34, GPIO35, GPIO36, and GPIO39.
static constexpr bool BATTERY_SENSE_ENABLED = true;
static constexpr int SENSE_PIN = 34;  // Battery divider output to ADC1 input
static constexpr int BATTERY_SENSE_PIN = SENSE_PIN;

}  // namespace board_config
