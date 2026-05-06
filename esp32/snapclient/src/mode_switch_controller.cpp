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
  printSerialHelp();
}

void ModeSwitchController::setRuntimeMode(RuntimeMode *runtimeMode) {
  runtimeMode_ = runtimeMode;
}

void ModeSwitchController::update() {
  processSerialInput();

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
  requestModeChange(nextMode, "button");
}

void ModeSwitchController::processSerialInput() {
  while (Serial.available() > 0) {
    const int value = Serial.read();
    if (value < 0) {
      return;
    }

    switch (static_cast<char>(value)) {
      case 'b':
      case 'B':
        requestModeChange(app_config::OperatingMode::Bluetooth, "serial");
        return;
      case 's':
      case 'S':
        requestModeChange(app_config::OperatingMode::Snapclient, "serial");
        return;
      case 't':
      case 'T':
        requestModeToggle();
        return;
      case 'h':
      case 'H':
      case '?':
        printSerialHelp();
        break;
      case '\r':
      case '\n':
      case ' ':
      case '\t':
        break;
      default:
        Serial.printf("[serial] unrecognized mode command '%c'\n",
                      static_cast<char>(value));
        printSerialHelp();
        break;
    }
  }
}

void ModeSwitchController::printSerialHelp() const {
  Serial.println("[serial] mode commands: 'b' -> Bluetooth, 's' -> Snapclient, 't' -> toggle, '?' -> help");
}

void ModeSwitchController::requestModeChange(app_config::OperatingMode nextMode,
                                             const char *source) {
  if (nextMode == currentMode_) {
    Serial.printf("[%s] already in %s mode\n",
                  source,
                  app_config::operatingModeName(nextMode));
    return;
  }

  requestOperatingModeOnNextRestart(nextMode);

  if (source != nullptr && strcmp(source, "button") == 0) {
    Serial.printf("[button] pressed on GPIO%d -> rebooting into %s mode\n",
                  board_config::BOOT_MODE_BUTTON_PIN,
                  app_config::operatingModeName(nextMode));
  } else {
    Serial.printf("[%s] rebooting into %s mode\n",
                  source != nullptr ? source : "mode",
                  app_config::operatingModeName(nextMode));
  }

  if (runtimeMode_ != nullptr) {
    Serial.println("[audio] fading to mute before restart");
    runtimeMode_->prepareForRestart();
  }

  delay(app_config::MODE_SWITCH_RESTART_DELAY_MS);
  ESP.restart();
}
