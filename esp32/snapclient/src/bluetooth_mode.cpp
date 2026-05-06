#include "bluetooth_mode.h"

#include <WiFi.h>

#include "snapclient_config.h"

BluetoothMode *BluetoothMode::instance_ = nullptr;

BluetoothMode::BluetoothMode() = default;

bool BluetoothMode::begin() {
  Serial.println("[mode] starting Bluetooth audio receiver");

  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_OFF);
  delay(100);

  instance_ = this;

  a2dpSink_.set_stream_reader(handleAudioData, false);
  a2dpSink_.set_sample_rate_callback(handleSampleRate);
  a2dpSink_.set_on_connection_state_changed(handleConnectionState, this);
  a2dpSink_.set_auto_reconnect(app_config::BLUETOOTH_AUTO_RECONNECT);

  Serial.printf("[bluetooth] device name=%s\n",
                app_config::BLUETOOTH_DEVICE_NAME);
  Serial.println("[bluetooth] waiting for a source device...");
  a2dpSink_.start(app_config::BLUETOOTH_DEVICE_NAME);

  activeSampleRate_ = app_config::BLUETOOTH_DEFAULT_SAMPLE_RATE;
  if (!audioOutput_.begin(activeSampleRate_)) {
    Serial.println("[i2s] begin failed");
    return false;
  }

  return true;
}

void BluetoothMode::loop() {
  delay(app_config::BLUETOOTH_IDLE_DELAY_MS);
}

void BluetoothMode::handleAudioData(const uint8_t *data, uint32_t length) {
  if (instance_ != nullptr) {
    instance_->onAudioData(data, length);
  }
}

void BluetoothMode::handleSampleRate(uint16_t rate) {
  if (instance_ != nullptr) {
    instance_->onSampleRate(rate);
  }
}

void BluetoothMode::handleConnectionState(esp_a2d_connection_state_t state,
                                          void *context) {
  (void)context;
  Serial.printf("[bluetooth] connection state=%d\n", static_cast<int>(state));
}

void BluetoothMode::onAudioData(const uint8_t *data, uint32_t length) {
  audioOutput_.write(data, length);
}

void BluetoothMode::onSampleRate(uint16_t rate) {
  if (rate == 0 || rate == activeSampleRate_) {
    return;
  }

  activeSampleRate_ = rate;
  audioOutput_.updateAudioFormat(rate,
                                 app_config::AUDIO_CHANNELS,
                                 app_config::AUDIO_BITS_PER_SAMPLE);
}
