#include "mode_led_controller.h"

#include "board_config.h"

void ModeLedController::begin() {
  pinMode(board_config::MODE_STATUS_LED_PIN, OUTPUT);
  write(false);
}

void ModeLedController::setMode(app_config::OperatingMode mode) {
  activeMode_ = mode;
  lastToggleMs_ = millis();

  // Snapclient is the normal appliance mode, so keep the LED steady.
  // Bluetooth is the alternate boot mode, so blink to make it obvious.
  if (activeMode_ == app_config::OperatingMode::Snapclient) {
    write(true);
  } else {
    write(false);
  }
}

void ModeLedController::update() {
  if (activeMode_ != app_config::OperatingMode::Bluetooth) {
    return;
  }

  const uint32_t nowMs = millis();
  if (nowMs - lastToggleMs_ < app_config::MODE_LED_BLUETOOTH_BLINK_INTERVAL_MS) {
    return;
  }

  lastToggleMs_ = nowMs;
  write(!ledOn_);
}

void ModeLedController::write(bool on) {
  ledOn_ = on;
  const int level = on
                        ? (board_config::MODE_STATUS_LED_ACTIVE_HIGH ? HIGH : LOW)
                        : (board_config::MODE_STATUS_LED_ACTIVE_HIGH ? LOW : HIGH);
  digitalWrite(board_config::MODE_STATUS_LED_PIN, level);
}
