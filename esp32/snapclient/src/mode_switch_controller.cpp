#include "mode_switch_controller.h"

#include <esp_system.h>

#include "board_config.h"
#include "boot_mode_selector.h"

namespace {

app_config::OperatingMode oppositeMode(app_config::OperatingMode mode) {
  return mode == app_config::OperatingMode::Snapclient
             ? app_config::OperatingMode::Bluetooth
             : app_config::OperatingMode::Snapclient;
}

}  // namespace

void ModeSwitchController::begin(app_config::OperatingMode currentMode) {
  currentMode_ = currentMode;
  pinMode(board_config::BOOT_MODE_BUTTON_PIN,
          board_config::BOOT_MODE_BUTTON_USE_PULLUP ? INPUT_PULLUP : INPUT_PULLDOWN);
}

void ModeSwitchController::update() {
  const bool buttonActive = isButtonActive();

  if (!buttonWasReleasedAfterBoot_) {
    if (!buttonActive) {
      buttonWasReleasedAfterBoot_ = true;
      Serial.println("[button] mode switch armed after initial release");
    }
    return;
  }

  if (!buttonActive) {
    pressedSinceMs_ = 0;
    pressHandled_ = false;
    return;
  }

  if (pressHandled_) {
    return;
  }

  if (pressedSinceMs_ == 0) {
    pressedSinceMs_ = millis();
    return;
  }

  if (millis() - pressedSinceMs_ < app_config::MODE_SWITCH_DEBOUNCE_MS) {
    return;
  }

  pressHandled_ = true;
  requestModeToggle();
}

bool ModeSwitchController::isButtonActive() const {
  return digitalRead(board_config::BOOT_MODE_BUTTON_PIN) ==
         board_config::BOOT_MODE_BUTTON_ACTIVE_LEVEL;
}

void ModeSwitchController::requestModeToggle() {
  const auto nextMode = oppositeMode(currentMode_);

  requestOperatingModeOnNextRestart(nextMode);

  Serial.printf("[button] pressed on GPIO%d -> rebooting into %s mode\n",
                board_config::BOOT_MODE_BUTTON_PIN,
                app_config::operatingModeName(nextMode));
  delay(app_config::MODE_SWITCH_RESTART_DELAY_MS);
  ESP.restart();
}
