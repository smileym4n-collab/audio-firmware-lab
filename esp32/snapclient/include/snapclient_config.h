#pragma once

/*
  ESP32 Snapclient v2 configuration (stability-first).
  Version: 0.2.0
  Edit values below for your local network and hardware.
*/

// ---------- Wi-Fi ----------
#define SNAP_WIFI_SSID "TomEmmaWireless"
#define SNAP_WIFI_PASSWORD "aw3s0m3w1ththr33s"
#define SNAP_WIFI_CONNECT_TIMEOUT_MS 20000
#define SNAP_WIFI_RETRY_DELAY_MS 500
#define SNAP_WIFI_MONITOR_INTERVAL_MS 1000

// ---------- Snapserver ----------
// Use static IP for bench bring-up.
#define SNAP_SERVER_IP IPAddress(192, 168, 5, 130)
#define SNAP_SERVER_PORT 1704
#define SNAP_CLIENT_NAME "esp32-snapclient-v2"

// ---------- I2S pin map (ESP32 dev board -> PCM5102) ----------
#define SNAP_I2S_BCLK_PIN 26   // BCLK / SCK
#define SNAP_I2S_LRCLK_PIN 25  // LRCLK / WS
#define SNAP_I2S_DOUT_PIN 22   // DIN on PCM5102

// ---------- Audio format ----------
// Keep the stream conservative for non-PSRAM ESP32 + Opus decode.
#define SNAP_AUDIO_SAMPLE_RATE 48000
#define SNAP_AUDIO_BITS_PER_SAMPLE 16
#define SNAP_AUDIO_CHANNELS 2

// ---------- Runtime stability tuning ----------
#define SNAP_CPU_FREQ_MHZ 240
#define SNAP_TASK_CORE 1
#define SNAP_TASK_PRIORITY 3
#define SNAP_TASK_STACK_WORDS 8192
#define SNAP_TASK_DELAY_MS 1
#define SNAP_MAIN_LOOP_DELAY_MS 20
