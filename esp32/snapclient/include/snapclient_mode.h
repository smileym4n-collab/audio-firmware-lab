#pragma once

#include <memory>

#include <WiFi.h>

#include "AudioTools.h"
#include "SnapClient.h"

#include "audio_probe_stream.h"
#include "audio_output_controller.h"
#include "project_snap_output.h"
#include "snapcast_pcm_decoder.h"
#include "snapclient_time_sync.h"
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
  static void snapClientTaskEntry(void *context);
  void snapClientTaskLoop();

  WiFiClient wifiClient_;
  AudioOutputController audioOutput_;
  AudioProbeStream pcmProbe_;
  SnapcastPcmDecoder codec_;
  ProjectSnapOutput snapOutput_;
  std::unique_ptr<ProjectSnapProcessorRTOS> snapProcessor_;
  snap_arduino::SnapClient snapClient_;
  SnapTimeSyncClampedDynamicSinceStart dynamicTimeSync_;
  TaskHandle_t snapTaskHandle_ = nullptr;
  volatile bool snapTaskRunning_ = false;
  uint32_t lastWifiCheckMs_ = 0;
  bool playbackIdleLogged_ = false;
};
