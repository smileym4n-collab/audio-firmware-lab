#pragma once

#include "snapclient_config.h"

class ModeSwitchController {
 public:
  void begin(app_config::OperatingMode currentMode);
  void update();

 private:
  bool isButtonActive() const;
  void processSerialInput();
  void printSerialHelp() const;
  void requestModeChange(app_config::OperatingMode nextMode, const char *source);
  void requestModeToggle();

  app_config::OperatingMode currentMode_ = app_config::OperatingMode::Snapclient;
  bool buttonWasReleasedAfterBoot_ = false;
  bool pressHandled_ = false;
  uint32_t pressedSinceMs_ = 0;
};
