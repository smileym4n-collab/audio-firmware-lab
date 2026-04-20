#pragma once

#include "snapclient_config.h"

class ModeLedController {
 public:
  void begin();
  void setMode(app_config::OperatingMode mode);
  void update();

 private:
  void write(bool on);

  app_config::OperatingMode activeMode_ = app_config::OperatingMode::Snapclient;
  uint32_t lastToggleMs_ = 0;
  bool ledOn_ = false;
};
