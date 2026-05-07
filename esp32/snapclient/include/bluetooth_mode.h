#pragma once

#include "BluetoothA2DPSink.h"

#include <Arduino.h>

#include "audio_output_controller.h"
#include "runtime_mode.h"

class BluetoothMode : public RuntimeMode {
 public:
  BluetoothMode();

  bool begin() override;
  void loop() override;
  const char *name() const override { return "Bluetooth"; }
  void prepareForRestart() override;
  bool bluetoothClientConnected() const override { return connected_; }

 private:
  static void handleAudioData(const uint8_t *data, uint32_t length);
  static void handleSampleRate(uint16_t rate);
  static void handleConnectionState(esp_a2d_connection_state_t state, void *context);

  void onAudioData(const uint8_t *data, uint32_t length);
  void onSampleRate(uint16_t rate);

  static BluetoothMode *instance_;

  AudioOutputController audioOutput_;
  BluetoothA2DPSink a2dpSink_;
  String bluetoothDeviceName_;
  uint16_t activeSampleRate_ = 44100;
  volatile bool connected_ = false;
};
