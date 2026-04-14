#pragma once

/*
  ESP32 Snapclient bench prototype configuration.
  Edit values below for your local network and hardware.
*/

// ---------- Wi-Fi ----------
#define SNAP_WIFI_SSID "TomEmmaWireless"
#define SNAP_WIFI_PASSWORD "aw3s0m3w1ththr33s"

// ---------- Snapserver ----------
// Use a static IP for first bring-up to avoid mDNS complexity.
#define SNAP_SERVER_IP IPAddress(192, 168, 5, 130)
#define SNAP_SERVER_PORT 1704
#define SNAP_CLIENT_NAME "esp32-snapclient-bench"

// ---------- I2S pin map (generic ESP32 dev board + external DAC) ----------
#define SNAP_I2S_BCLK_PIN 26   // BCLK / SCK
#define SNAP_I2S_LRCLK_PIN 25  // LRCLK / WS
#define SNAP_I2S_DOUT_PIN 22   // DIN on external I2S DAC

// ---------- Audio format ----------
// Snapcast commonly runs 48 kHz on ESP32 clients.
#define SNAP_AUDIO_SAMPLE_RATE 48000
#define SNAP_AUDIO_BITS_PER_SAMPLE 16
#define SNAP_AUDIO_CHANNELS 2

// ---------- Bring-up behavior ----------
#define SNAP_WIFI_CONNECT_TIMEOUT_MS 20000
#define SNAP_WIFI_RETRY_DELAY_MS 500
#define SNAP_MAIN_LOOP_DELAY_MS 1
