#include "snapclient_mode.h"

#include "api/SnapProcessorRTOS.h"

#include "snapclient_config.h"

SnapclientMode::SnapclientMode()
    : snapProcessor_(new snap_arduino::SnapProcessorRTOS(
          app_config::SNAP_OUTPUT_QUEUE_BYTES,
          app_config::SNAP_OUTPUT_ACTIVATION_PERCENT)),
      snapClient_(wifiClient_, audioOutput_.stream(), codec_) {}

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
  snapClient_.setWiFi(true);
  snapClient_.setServerIP(app_config::snapServerIp());
  snapClient_.snapProcessor().setServerPort(app_config::SNAP_SERVER_PORT);
  snapClient_.snapProcessor().setHostName(app_config::SNAP_HOST_NAME);
  snapClient_.snapProcessor().setClientName(app_config::SNAP_CLIENT_NAME);
  snapClient_.snapProcessor().setFastLoop(app_config::SNAP_USE_FAST_LOOP);

  Serial.print("[snapclient] server=");
  Serial.print(app_config::snapServerIp());
  Serial.print(":");
  Serial.println(app_config::SNAP_SERVER_PORT);
  Serial.printf("[audio] expected stream=%lu Hz, %u-bit, %u ch, codec=pcm\n",
                static_cast<unsigned long>(app_config::AUDIO_SAMPLE_RATE),
                app_config::AUDIO_BITS_PER_SAMPLE,
                app_config::AUDIO_CHANNELS);
  Serial.printf("[snapclient] queue=%lu bytes, free_psram=%lu\n",
                static_cast<unsigned long>(app_config::SNAP_OUTPUT_QUEUE_BYTES),
                static_cast<unsigned long>(ESP.getFreePsram()));

  if (!snapClient_.begin()) {
    Serial.println("[snapclient] begin failed");
    return false;
  }

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

  snapClient_.doLoop();
  delay(app_config::MAIN_LOOP_DELAY_MS);
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
