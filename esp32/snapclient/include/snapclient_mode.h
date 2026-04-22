#pragma once

#include <memory>

#include <WiFi.h>

#include "AudioTools.h"
#include "SnapClient.h"

#include "audio_probe_stream.h"
#include "audio_output_controller.h"
#include "snapcast_pcm_decoder.h"
#include "runtime_mode.h"

class ProjectSnapProcessorRTOS;

class SnapclientMode : public RuntimeMode {
 public:
  SnapclientMode();
  ~SnapclientMode();

  bool begin() override;
  void loop() override;
  const char *name() const override { return "Snapclient"; }

 private:
  bool connectWifiWithTimeout();

  WiFiClient wifiClient_;
  AudioOutputController audioOutput_;
  AudioProbeStream pcmProbe_;
  SnapcastPcmDecoder codec_;
  std::unique_ptr<ProjectSnapProcessorRTOS> snapProcessor_;
  snap_arduino::SnapClient snapClient_;
  uint32_t lastWifiCheckMs_ = 0;
  bool playbackIdleLogged_ = false;
};
