#pragma once

#include "snapclient_config.h"

class ModeLedController {
 public:
  void begin();
  void setMode(app_config::OperatingMode mode);
  void update();

 private:
  void writeWifiLed(bool on);
  void writeBtLed(bool on);

  app_config::OperatingMode activeMode_ = app_config::OperatingMode::Snapclient;
  uint32_t lastToggleMs_ = 0;
  bool btLedOn_ = false;
};
