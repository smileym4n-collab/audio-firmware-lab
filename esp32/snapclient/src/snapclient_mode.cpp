#include "snapclient_mode.h"

#include <esp_heap_caps.h>

#include "bluetooth_name_store.h"
#include "channel_mode_store.h"
#include "project_snap_processor_rtos.h"
#include "snapclient_config.h"

namespace {

bool extractChannelMode(const String &body, app_config::ChannelMode &mode) {
  app_config::ChannelMode rawMode = app_config::ChannelMode::Stereo;
  if (app_config::parseChannelMode(body, rawMode)) {
    mode = rawMode;
    return true;
  }

  const int keyIndex = body.indexOf("\"channel_mode\"");
  if (keyIndex < 0) {
    return false;
  }

  const int colonIndex = body.indexOf(':', keyIndex);
  if (colonIndex < 0) {
    return false;
  }

  const int valueStart = body.indexOf('"', colonIndex + 1);
  if (valueStart < 0) {
    return false;
  }

  const int valueEnd = body.indexOf('"', valueStart + 1);
  if (valueEnd < 0) {
    return false;
  }

  const String value = body.substring(valueStart + 1, valueEnd);
  return app_config::parseChannelMode(value, mode);
}

}  // namespace

SnapclientMode::SnapclientMode()
    : controlServer_(app_config::CONTROL_API_PORT),
      pcmProbe_(audioOutput_.stream()),
      snapOutput_(audio_tools::AudioInfo(app_config::AUDIO_SAMPLE_RATE,
                                         app_config::AUDIO_CHANNELS,
                                         app_config::AUDIO_BITS_PER_SAMPLE),
                  app_config::SNAPCLIENT_USE_RESAMPLER,
                  false),
      snapProcessor_(new ProjectSnapProcessorRTOS(
          snapOutput_,
          app_config::SNAP_OUTPUT_QUEUE_BYTES,
          app_config::SNAP_OUTPUT_ACTIVATION_PERCENT)),
      snapClient_(wifiClient_, pcmProbe_, codec_),
      dynamicTimeSync_(app_config::SNAPCLIENT_PROCESSING_LAG_MS,
                       app_config::SNAPCLIENT_SYNC_INTERVAL,
                       app_config::SNAPCLIENT_MIN_PLAYBACK_FACTOR,
                       app_config::SNAPCLIENT_MAX_PLAYBACK_FACTOR,
                       app_config::SNAPCLIENT_UNITY_DEADBAND) {
  pcmProbe_.setPcmGain(app_config::SNAPCLIENT_FINAL_PCM_GAIN);
  pcmProbe_.setPeriodicStatsEnabled(app_config::SNAPCLIENT_PERIODIC_STATS_ENABLED);
  snapProcessor_->setPeriodicStatsEnabled(
      app_config::SNAPCLIENT_PERIODIC_STATS_ENABLED);
  snapProcessor_->setRebufferEnabled(app_config::SNAPCLIENT_REBUFFER_ENABLED);
  snapProcessor_->setRebufferThresholds(
      app_config::SNAP_OUTPUT_REBUFFER_START_PERCENT,
      app_config::SNAP_OUTPUT_REBUFFER_RESUME_PERCENT);
  codec_.setFormatTarget(pcmProbe_);
}
SnapclientMode::~SnapclientMode() = default;

bool SnapclientMode::begin() {
  Serial.println("[mode] starting Snapclient over Wi-Fi");
  Serial.println("[wifi] connecting...");

  if (!connectWifiWithTimeout()) {
    Serial.println("[wifi] failed to connect");
    return false;
  }

  Serial.print("[wifi] connected, ip=");
  Serial.println(WiFi.localIP());

  if (!audioOutput_.begin(app_config::AUDIO_SAMPLE_RATE)) {
    Serial.println("[i2s] begin failed");
    return false;
  }
  audioOutput_.setChannelMode(loadChannelModePreference());
  Serial.printf("[channel] snapclient channel mode=%s\n",
                app_config::channelModeName(audioOutput_.channelMode()));
  batteryMonitor_.begin();
  batteryMonitor_.update(true);
  beginControlApi();

  snapClient_.setSnapProcessor(*snapProcessor_);
  snapClient_.setSnapTimeSync(dynamicTimeSync_);
  snapClient_.setWiFi(true);
  snapClient_.setServerIP(app_config::snapServerIp());
  snapClient_.snapProcessor().setServerPort(app_config::SNAP_SERVER_PORT);
  snapClient_.snapProcessor().setHostName(app_config::SNAP_HOST_NAME);
  snapClient_.snapProcessor().setClientName(app_config::SNAP_CLIENT_NAME);
  snapClient_.snapProcessor().setFastLoop(app_config::SNAP_USE_FAST_LOOP);
  snapClient_.setVolumeFactor(app_config::SNAPCLIENT_OUTPUT_GAIN);

  Serial.print("[snapclient] server=");
  Serial.print(app_config::snapServerIp());
  Serial.print(":");
  Serial.println(app_config::SNAP_SERVER_PORT);
  Serial.printf(
      "[audio] configured fallback=%lu Hz, %u-bit, %u ch, codec=pcm (Snapcast WAV wrapper)\n",
      static_cast<unsigned long>(app_config::AUDIO_SAMPLE_RATE),
      app_config::AUDIO_BITS_PER_SAMPLE,
      app_config::AUDIO_CHANNELS);
  Serial.println(
      "[audio] path=Snapserver PCM -> SnapcastPcmDecoder -> shared I2S DAC");
  Serial.printf("[i2s] initial format=%lu Hz, %u-bit, %u ch\n",
                static_cast<unsigned long>(app_config::AUDIO_SAMPLE_RATE),
                app_config::AUDIO_BITS_PER_SAMPLE,
                app_config::AUDIO_CHANNELS);
  Serial.printf("[snapclient] queue=%lu bytes, free_psram=%lu\n",
                static_cast<unsigned long>(app_config::SNAP_OUTPUT_QUEUE_BYTES),
                static_cast<unsigned long>(ESP.getFreePsram()));
  Serial.printf("[snapclient] queue activation=%u%%\n",
                app_config::SNAP_OUTPUT_ACTIVATION_PERCENT);
  Serial.printf("[snapclient] rebuffer=%u%% -> %u%%\n",
                app_config::SNAP_OUTPUT_REBUFFER_START_PERCENT,
                app_config::SNAP_OUTPUT_REBUFFER_RESUME_PERCENT);
  Serial.printf("[snapclient] queue entry slots=%d\n",
                RTOS_MAX_QUEUE_ENTRY_COUNT);
  Serial.println("[snapclient] decoder=SnapcastPcmDecoder");
  Serial.printf("[snapclient] output gain=%.2f\n",
                app_config::SNAPCLIENT_OUTPUT_GAIN);
  Serial.printf("[snapclient] final pcm gain=%.2f\n",
                app_config::SNAPCLIENT_FINAL_PCM_GAIN);
  Serial.printf("[snapclient] sync=dynamic-clamped range=%.4f..%.4f deadband=%.4f lag=%dms interval=%d\n",
                app_config::SNAPCLIENT_MIN_PLAYBACK_FACTOR,
                app_config::SNAPCLIENT_MAX_PLAYBACK_FACTOR,
                app_config::SNAPCLIENT_UNITY_DEADBAND,
                app_config::SNAPCLIENT_PROCESSING_LAG_MS,
                app_config::SNAPCLIENT_SYNC_INTERVAL);
  Serial.printf("[snapclient] resampler=%s\n",
                app_config::SNAPCLIENT_USE_RESAMPLER ? "on" : "off");

  if (!snapClient_.begin()) {
    Serial.println("[snapclient] begin failed");
    return false;
  }

  if (!startSnapClientTask()) {
    return false;
  }

  Serial.printf("[snapclient] task core=%ld prio=%lu stack=%lu delay=%lu\n",
                static_cast<long>(app_config::SNAPCLIENT_TASK_CORE),
                static_cast<unsigned long>(app_config::SNAPCLIENT_TASK_PRIORITY),
                static_cast<unsigned long>(app_config::SNAPCLIENT_TASK_STACK_WORDS),
                static_cast<unsigned long>(app_config::SNAPCLIENT_TASK_DELAY_MS));
  Serial.printf("[snapclient] periodic stats=%s\n",
                app_config::SNAPCLIENT_PERIODIC_STATS_ENABLED ? "on" : "off");
  Serial.println("[snapclient] running");
  return true;
}

void SnapclientMode::loop() {
  batteryMonitor_.update();
  handleControlApi();

  const uint32_t nowMs = millis();

  if (nowMs - lastWifiCheckMs_ >= app_config::SNAP_WIFI_MONITOR_INTERVAL_MS) {
    lastWifiCheckMs_ = nowMs;
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("[wifi] link lost, restarting...");
      logDiagnosticSnapshot("wifi-link-lost");
      prepareForRestart();
      delay(app_config::RESTART_DELAY_MS);
      ESP.restart();
    }
  }

  snapProcessor_->logRuntime();
  if (snapProcessor_->isOutputTimedOut(
          app_config::SNAP_OUTPUT_IDLE_TIMEOUT_MS)) {
    if (playbackIdleSinceMs_ == 0) {
      playbackIdleSinceMs_ = nowMs;
    }

    if (!playbackIdleLogged_) {
      Serial.println(
          "[snapclient] playback idle timeout: decoded PCM is no longer reaching the output task");
      logDiagnosticSnapshot("idle-timeout");
      playbackIdleLogged_ = true;
    }

    if ((nowMs - playbackIdleSinceMs_) >= app_config::SNAP_OUTPUT_IDLE_RESTART_MS) {
      Serial.println(
          "[snapclient] playback idle persisted, restarting Snapclient mode");
      logDiagnosticSnapshot("idle-restart");
      prepareForRestart();
      delay(app_config::RESTART_DELAY_MS);
      ESP.restart();
    }
  } else {
    playbackIdleSinceMs_ = 0;
    playbackIdleLogged_ = false;
  }

  delay(app_config::MAIN_LOOP_DELAY_MS);
}

bool extractJsonStringValue(const String &body, const char *key, String &value) {
  String quotedKey = "\"";
  quotedKey += key;
  quotedKey += "\"";

  const int keyIndex = body.indexOf(quotedKey);
  if (keyIndex < 0) {
    return false;
  }

  const int colonIndex = body.indexOf(':', keyIndex);
  if (colonIndex < 0) {
    return false;
  }

  const int valueStart = body.indexOf('"', colonIndex + 1);
  if (valueStart < 0) {
    return false;
  }

  const int valueEnd = body.indexOf('"', valueStart + 1);
  if (valueEnd < 0) {
    return false;
  }

  value = body.substring(valueStart + 1, valueEnd);
  value.trim();
  return true;
}

void SnapclientMode::prepareForRestart() {
  if (restartPrepared_) {
    return;
  }

  restartPrepared_ = true;
  logDiagnosticSnapshot("prepare-restart");
  stopSnapClientTask(app_config::SNAPCLIENT_TASK_STOP_TIMEOUT_MS);
  snapProcessor_->end();
  audioOutput_.muteForRestart(app_config::AUDIO_MODE_CHANGE_MUTE_RAMP_MS);
}

void SnapclientMode::logDiagnosticSnapshot(const char *reason) {
  const uint32_t largestDmaBlock =
      heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  const int wifiStatus = static_cast<int>(WiFi.status());
  const long rssi = WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0;
  const char *taskState =
      snapTaskHandle_ != nullptr ? (snapTaskRunning_ ? "running" : "stopping")
                                 : "stopped";

  Serial.printf(
      "[diag] reason=%s uptime=%lu wifi=%d rssi=%ld heap=%lu minheap=%lu psram=%lu dma_largest=%lu snap_task=%s\n",
      reason != nullptr ? reason : "-",
      static_cast<unsigned long>(millis()),
      wifiStatus,
      rssi,
      static_cast<unsigned long>(ESP.getFreeHeap()),
      static_cast<unsigned long>(ESP.getMinFreeHeap()),
      static_cast<unsigned long>(ESP.getFreePsram()),
      static_cast<unsigned long>(largestDmaBlock),
      taskState);

  snapProcessor_->logRuntime(reason, true);
}

bool SnapclientMode::startSnapClientTask() {
  snapTaskRunning_ = true;
  snapTaskHandle_ = nullptr;

  const BaseType_t result =
      xTaskCreatePinnedToCore(snapClientTaskEntry,
                              "snap-loop",
                              app_config::SNAPCLIENT_TASK_STACK_WORDS,
                              this,
                              app_config::SNAPCLIENT_TASK_PRIORITY,
                              &snapTaskHandle_,
                              app_config::SNAPCLIENT_TASK_CORE);

  if (result == pdPASS) {
    return true;
  }

  snapTaskRunning_ = false;
  snapTaskHandle_ = nullptr;
  Serial.printf("[snapclient] task creation failed result=%ld\n",
                static_cast<long>(result));
  logDiagnosticSnapshot("snap-task-create-failed");
  snapProcessor_->end();
  return false;
}

void SnapclientMode::stopSnapClientTask(uint32_t timeoutMs) {
  if (!snapTaskRunning_ && snapTaskHandle_ == nullptr) {
    return;
  }

  Serial.println("[snapclient] stopping snap loop task");
  snapTaskRunning_ = false;

  const uint32_t startMs = millis();
  while (snapTaskHandle_ != nullptr && (millis() - startMs) < timeoutMs) {
    delay(10);
  }

  if (snapTaskHandle_ != nullptr) {
    Serial.printf("[snapclient] snap loop task stop timeout after %lums\n",
                  static_cast<unsigned long>(millis() - startMs));
    return;
  }

  Serial.printf("[snapclient] snap loop task stopped in %lums\n",
                static_cast<unsigned long>(millis() - startMs));
}

void SnapclientMode::snapClientTaskEntry(void *context) {
  auto *self = static_cast<SnapclientMode *>(context);
  if (self != nullptr) {
    self->snapClientTaskLoop();
  }
  vTaskDelete(nullptr);
}

void SnapclientMode::snapClientTaskLoop() {
  while (snapTaskRunning_) {
    if (WiFi.status() == WL_CONNECTED) {
      snapClient_.doLoop();
    }
    vTaskDelay(pdMS_TO_TICKS(app_config::SNAPCLIENT_TASK_DELAY_MS));
  }
  snapTaskHandle_ = nullptr;
}

bool SnapclientMode::connectWifiWithTimeout() {
  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);
  WiFi.setHostname(app_config::SNAP_HOST_NAME);
  WiFi.begin(app_config::SNAP_WIFI_SSID, app_config::SNAP_WIFI_PASSWORD);

  const uint32_t startMs = millis();
  while (WiFi.status() != WL_CONNECTED &&
         (millis() - startMs) < app_config::SNAP_WIFI_CONNECT_TIMEOUT_MS) {
    delay(app_config::SNAP_WIFI_RETRY_DELAY_MS);
    Serial.print('.');
  }

  Serial.println();
  return WiFi.status() == WL_CONNECTED;
}

void SnapclientMode::beginControlApi() {
  controlServer_.on("/api/status", HTTP_GET, [this]() { sendControlStatus(); });
  controlServer_.on("/api/channel-mode", HTTP_POST, [this]() { handleSetChannelMode(); });
  controlServer_.on("/api/bluetooth-name", HTTP_POST, [this]() { handleSetBluetoothName(); });
  controlServer_.onNotFound([this]() {
    controlServer_.send(404, "application/json", "{\"error\":\"not_found\"}");
  });
  controlServer_.begin();

  Serial.printf("[control] api=http://%s:%u/api/status\n",
                WiFi.localIP().toString().c_str(),
                app_config::CONTROL_API_PORT);
}

void SnapclientMode::handleControlApi() {
  controlServer_.handleClient();
}

void SnapclientMode::sendControlStatus() {
  batteryMonitor_.update();
  const BatteryStatus battery = batteryMonitor_.status();

  String response = "{";
  response += "\"project\":\"";
  response += app_config::PROJECT_TITLE;
  response += "\",\"version\":\"";
  response += app_config::FIRMWARE_VERSION;
  response += "\",\"firmwareVersion\":\"";
  response += app_config::FIRMWARE_VERSION;
  response += "\",\"runtime_mode\":\"snapclient\"";
  response += ",\"channel_mode\":\"";
  response += app_config::channelModeName(audioOutput_.channelMode());
  response += "\",\"bluetooth_name\":\"";
  response += loadBluetoothNamePreference();
  response += "\",\"battery\":{";
  response += "\"available\":";
  response += battery.available ? "true" : "false";
  if (battery.available) {
    response += ",\"voltage\":";
    response += String(battery.voltage, 2);
    response += ",\"percent\":";
    response += battery.percent;
  }
  response += "}";
  response += ",\"capabilities\":{\"channel_modes\":[\"stereo\",\"left\",\"right\"],\"bluetooth_name\":true}";
  response += "}";

  controlServer_.send(200, "application/json", response);
}

void SnapclientMode::handleSetChannelMode() {
  const String body = controlServer_.arg("plain");
  app_config::ChannelMode requestedMode = app_config::ChannelMode::Stereo;

  if (!extractChannelMode(body, requestedMode)) {
    controlServer_.send(
        400,
        "application/json",
        "{\"error\":\"invalid_channel_mode\",\"allowed\":[\"stereo\",\"left\",\"right\"]}");
    return;
  }

  audioOutput_.setChannelMode(requestedMode);
  saveChannelModePreference(requestedMode);
  sendControlStatus();
}

void SnapclientMode::handleSetBluetoothName() {
  const String body = controlServer_.arg("plain");
  String requestedName;

  if (!extractJsonStringValue(body, "bluetooth_name", requestedName) ||
      !isValidBluetoothName(requestedName)) {
    controlServer_.send(
        400,
        "application/json",
        "{\"error\":\"invalid_bluetooth_name\",\"max_length\":31}");
    return;
  }

  saveBluetoothNamePreference(requestedName);
  Serial.printf("[bluetooth] saved device name=%s\n", requestedName.c_str());
  sendControlStatus();
}
