/*
  Project: ESP32 Snapcast client prototype (generic dev board + external I2S DAC)
  Framework: Arduino (PlatformIO)

  Pin map (default bench wiring):
    GPIO26 -> I2S BCLK (DAC BCK/SCK)
    GPIO25 -> I2S LRCLK/WS
    GPIO22 -> I2S DOUT (DAC DIN)
    GND    -> DAC GND
    3V3    -> DAC VCC (if 3.3V-compatible module)

  Notes:
  - MCLK is intentionally unused for first bring-up.
  - Configure Wi-Fi and Snapserver IP in include/snapclient_config.h.
*/

#include <WiFi.h>
#include "AudioTools.h"
#include "AudioTools/AudioCodecs/CodecOpus.h"
#include "SnapClient.h"
#include "snapclient_config.h"

OpusAudioDecoder codec;
WiFiClient wifiClient;
I2SStream i2sOut;
SnapClient snapClient(wifiClient, i2sOut, codec);

bool connectWifiWithTimeout() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(SNAP_WIFI_SSID, SNAP_WIFI_PASSWORD);

  const uint32_t startMs = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - startMs) < SNAP_WIFI_CONNECT_TIMEOUT_MS) {
    delay(SNAP_WIFI_RETRY_DELAY_MS);
    Serial.print('.');
  }

  return WiFi.status() == WL_CONNECTED;
}

void configureI2SOutput() {
  auto cfg = i2sOut.defaultConfig(TX_MODE);
  cfg.sample_rate = SNAP_AUDIO_SAMPLE_RATE;
  cfg.bits_per_sample = SNAP_AUDIO_BITS_PER_SAMPLE;
  cfg.channels = SNAP_AUDIO_CHANNELS;
  cfg.pin_bck = SNAP_I2S_BCLK_PIN;
  cfg.pin_ws = SNAP_I2S_LRCLK_PIN;
  cfg.pin_data = SNAP_I2S_DOUT_PIN;
  i2sOut.begin(cfg);
}

void setup() {
  Serial.begin(115200);
  delay(200);

  Serial.println("\n[boot] ESP32 Snapclient prototype");
  Serial.println("[wifi] connecting...");

  if (!connectWifiWithTimeout()) {
    Serial.println("\n[wifi] failed to connect, restarting...");
    delay(1500);
    ESP.restart();
  }

  Serial.print("\n[wifi] connected, ip=");
  Serial.println(WiFi.localIP());

  configureI2SOutput();

  snapClient.setWiFi(true);
  snapClient.setServerIP(SNAP_SERVER_IP);
  snapClient.snapProcessor().setServerPort(SNAP_SERVER_PORT);
  snapClient.snapProcessor().setClientName(SNAP_CLIENT_NAME);

  Serial.print("[snapclient] server=");
  Serial.print(SNAP_SERVER_IP);
  Serial.print(":");
  Serial.println(SNAP_SERVER_PORT);

  if (!snapClient.begin()) {
    Serial.println("[snapclient] begin failed, restarting...");
    delay(1500);
    ESP.restart();
  }

  Serial.println("[snapclient] running");
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[wifi] link lost, restarting...");
    delay(1000);
    ESP.restart();
  }

  snapClient.doLoop();
  delay(SNAP_MAIN_LOOP_DELAY_MS);
}
