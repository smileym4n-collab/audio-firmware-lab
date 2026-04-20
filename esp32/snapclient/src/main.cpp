/*
  Project: ESP32 Snapcast client v3 (PCM-first prototype for stable playback)
  Version: 0.3.0
  Framework: Arduino (PlatformIO)

  Pin map (standard ESP32 dev board -> PCM5102):
    GPIO26 -> I2S BCLK (PCM5102 BCK/SCK)
    GPIO25 -> I2S LRCLK/WS
    GPIO22 -> I2S DOUT (PCM5102 DIN)
    GND    -> PCM5102 GND
    3V3    -> PCM5102 VCC (for 3.3V-compatible PCM5102 modules)

  Notes:
  - No MCLK is used.
  - This v3 build is intended for Snapserver streams configured with codec=pcm.
  - Wi-Fi and Snapserver settings are in include/snapclient_config.h.
*/

#include <WiFi.h>

#include "AudioTools.h"
#include "AudioTools/AudioCodecs/CodecWAV.h"
#include "SnapClient.h"
#include "snapclient_config.h"

using namespace snap_arduino;

WAVDecoder codec;
WiFiClient wifiClient;
I2SStream i2sOut;
SnapClient snapClient(wifiClient, i2sOut, codec);

TaskHandle_t snapTaskHandle = nullptr;
volatile bool snapClientStarted = false;
uint32_t lastWifiCheckMs = 0;

bool connectWifiWithTimeout() {
  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);  // reduce bursty latency on continuous audio playback
  WiFi.setHostname(SNAP_HOST_NAME);
  WiFi.begin(SNAP_WIFI_SSID, SNAP_WIFI_PASSWORD);

  const uint32_t startMs = millis();
  while (WiFi.status() != WL_CONNECTED &&
         (millis() - startMs) < SNAP_WIFI_CONNECT_TIMEOUT_MS) {
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
  cfg.pin_mck = -1;
  cfg.buffer_count = SNAP_I2S_DMA_BUFFER_COUNT;
  cfg.buffer_size = SNAP_I2S_DMA_BUFFER_SIZE;
  cfg.use_apll = SNAP_I2S_USE_APLL;
  cfg.auto_clear = true;
  i2sOut.begin(cfg);
}

void startSnapClient() {
  snapClient.setWiFi(true);
  snapClient.setServerIP(SNAP_SERVER_IP);
  snapClient.snapProcessor().setServerPort(SNAP_SERVER_PORT);
  snapClient.snapProcessor().setHostName(SNAP_HOST_NAME);
  snapClient.snapProcessor().setClientName(SNAP_CLIENT_NAME);
  snapClient.snapProcessor().setFastLoop(SNAP_USE_FAST_LOOP);

  Serial.print("[snapclient] server=");
  Serial.print(SNAP_SERVER_IP);
  Serial.print(":");
  Serial.println(SNAP_SERVER_PORT);
  Serial.printf("[audio] expected stream=%u Hz, %u-bit, %u ch, codec=pcm\n",
                SNAP_AUDIO_SAMPLE_RATE,
                SNAP_AUDIO_BITS_PER_SAMPLE,
                SNAP_AUDIO_CHANNELS);
  Serial.printf("[i2s] dma buffers=%u x %u bytes\n",
                SNAP_I2S_DMA_BUFFER_COUNT,
                SNAP_I2S_DMA_BUFFER_SIZE);

  if (!snapClient.begin()) {
    Serial.println("[snapclient] begin failed, restarting...");
    delay(1500);
    ESP.restart();
  }

  snapClientStarted = true;
  Serial.println("[snapclient] running");
}

void snapClientTask(void *parameter) {
  (void)parameter;

  for (;;) {
    if (WiFi.status() == WL_CONNECTED && snapClientStarted) {
      snapClient.doLoop();
    }

    vTaskDelay(pdMS_TO_TICKS(SNAP_TASK_DELAY_MS));
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);

  setCpuFrequencyMhz(SNAP_CPU_FREQ_MHZ);
  Serial.println("\n[boot] ESP32 Snapclient v3 (PCM-first)");
  Serial.printf("[cpu] %u MHz\n", getCpuFrequencyMhz());
  Serial.println("[wifi] connecting...");

  if (!connectWifiWithTimeout()) {
    Serial.println("\n[wifi] failed to connect, restarting...");
    delay(1500);
    ESP.restart();
  }

  Serial.print("\n[wifi] connected, ip=");
  Serial.println(WiFi.localIP());

  configureI2SOutput();
  startSnapClient();

  xTaskCreatePinnedToCore(
      snapClientTask,
      "snap-loop",
      SNAP_TASK_STACK_WORDS,
      nullptr,
      SNAP_TASK_PRIORITY,
      &snapTaskHandle,
      SNAP_TASK_CORE);
}

void loop() {
  const uint32_t nowMs = millis();

  if (nowMs - lastWifiCheckMs >= SNAP_WIFI_MONITOR_INTERVAL_MS) {
    lastWifiCheckMs = nowMs;

    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("[wifi] link lost, restarting...");
      delay(1000);
      ESP.restart();
    }
  }

  delay(SNAP_MAIN_LOOP_DELAY_MS);
}
