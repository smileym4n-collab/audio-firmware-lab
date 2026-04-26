#include "snapclient_mode.h"

#include "project_snap_processor_rtos.h"
#include "snapclient_config.h"

SnapclientMode::SnapclientMode()
    : pcmProbe_(audioOutput_.stream()),
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

  snapTaskRunning_ = true;
  xTaskCreatePinnedToCore(snapClientTaskEntry,
                          "snap-loop",
                          app_config::SNAPCLIENT_TASK_STACK_WORDS,
                          this,
                          app_config::SNAPCLIENT_TASK_PRIORITY,
                          &snapTaskHandle_,
                          app_config::SNAPCLIENT_TASK_CORE);
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
  const uint32_t nowMs = millis();

  if (nowMs - lastWifiCheckMs_ >= app_config::SNAP_WIFI_MONITOR_INTERVAL_MS) {
    lastWifiCheckMs_ = nowMs;
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("[wifi] link lost, restarting...");
      delay(app_config::RESTART_DELAY_MS);
      ESP.restart();
    }
  }

  snapProcessor_->logRuntime();
  if (snapProcessor_->isOutputTimedOut(
          app_config::SNAP_OUTPUT_IDLE_TIMEOUT_MS)) {
    if (!playbackIdleLogged_) {
      Serial.println(
          "[snapclient] playback idle timeout: decoded PCM is no longer reaching the output task");
      playbackIdleLogged_ = true;
    }
  } else {
    playbackIdleLogged_ = false;
  }

  delay(app_config::MAIN_LOOP_DELAY_MS);
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
