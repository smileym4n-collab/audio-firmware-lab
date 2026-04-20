#pragma once

#include <memory>

#include <WiFi.h>

#include "AudioTools.h"
#include "AudioTools/AudioCodecs/CodecWAV.h"
#include "SnapClient.h"

#include "audio_output_controller.h"
#include "runtime_mode.h"

namespace snap_arduino {
class SnapProcessorRTOS;
}

class SnapclientMode : public RuntimeMode {
 public:
  SnapclientMode();
  ~SnapclientMode();

  bool begin() override;
  void loop() override;
  const char *name() const override { return "Snapclient"; }

 private:
  bool connectWifiWithTimeout();

  WAVDecoder codec_;
  WiFiClient wifiClient_;
  AudioOutputController audioOutput_;
  std::unique_ptr<snap_arduino::SnapProcessorRTOS> snapProcessor_;
  snap_arduino::SnapClient snapClient_;
  uint32_t lastWifiCheckMs_ = 0;
};
