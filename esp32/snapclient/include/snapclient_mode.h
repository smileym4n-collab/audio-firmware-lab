#pragma once

#include <memory>

#include <WiFi.h>
#include <WebServer.h>

#include "AudioTools.h"
#include "SnapClient.h"

#include "audio_probe_stream.h"
#include "audio_output_controller.h"
#include "battery_monitor.h"
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
  void prepareForRestart() override;

 private:
  bool connectWifiWithTimeout();
  void beginControlApi();
  void handleControlApi();
  void sendControlStatus();
  void handleSetChannelMode();
  void handleSetBluetoothName();
  void handleFirmwareUploadRaw();
  void handleFirmwareUploadComplete();
  void failFirmwareUpload(int statusCode,
                          const char *error,
                          const char *message);
  void scheduleFirmwareRestart();
  void logDiagnosticSnapshot(const char *reason);
  bool startSnapClientTask();
  void stopSnapClientTask(uint32_t timeoutMs);
  static void snapClientTaskEntry(void *context);
  void snapClientTaskLoop();

  WiFiClient wifiClient_;
  AudioOutputController audioOutput_;
  BatteryMonitor batteryMonitor_;
  WebServer controlServer_;
  AudioProbeStream pcmProbe_;
  SnapcastPcmDecoder codec_;
  ProjectSnapOutput snapOutput_;
  std::unique_ptr<ProjectSnapProcessorRTOS> snapProcessor_;
  snap_arduino::SnapClient snapClient_;
  SnapTimeSyncClampedDynamicSinceStart dynamicTimeSync_;
  TaskHandle_t snapTaskHandle_ = nullptr;
  volatile bool snapTaskRunning_ = false;
  uint32_t lastWifiCheckMs_ = 0;
  uint32_t playbackIdleSinceMs_ = 0;
  uint32_t otaRestartAtMs_ = 0;
  size_t otaExpectedSize_ = 0;
  size_t otaWritten_ = 0;
  size_t otaPartitionSize_ = 0;
  int otaResponseStatus_ = 500;
  String otaError_;
  String otaMessage_;
  bool playbackIdleLogged_ = false;
  bool restartPrepared_ = false;
  bool otaUpdateInProgress_ = false;
  bool otaUpdateAccepted_ = false;
  bool otaUpdateFailed_ = false;
  bool otaRebootPending_ = false;
};
