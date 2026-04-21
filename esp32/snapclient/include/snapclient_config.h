#pragma once

/*
  ESP32 audio client v9.11 configuration.
  Version: 0.9.11
  Edit values below for your local network, Snapserver, and Bluetooth naming.
*/

#include <Arduino.h>

namespace app_config {

enum class OperatingMode : uint8_t { Snapclient, Bluetooth };

inline const char *operatingModeName(OperatingMode mode) {
  switch (mode) {
    case OperatingMode::Snapclient:
      return "Snapclient";
    case OperatingMode::Bluetooth:
      return "Bluetooth";
    default:
      return "Unknown";
  }
}

static constexpr char PROJECT_TITLE[] = "ESP32 Audio Client v9.11";
static constexpr char PROJECT_VERSION[] = "0.9.11";
static constexpr char TARGET_MODULE[] = "ESP32-WROVER-IE-N16R8";

// ---------- Wi-Fi ----------
static constexpr char SNAP_WIFI_SSID[] = "TomEmmaWireless";
static constexpr char SNAP_WIFI_PASSWORD[] = "aw3s0m3w1ththr33s";
static constexpr uint32_t SNAP_WIFI_CONNECT_TIMEOUT_MS = 20000;
static constexpr uint32_t SNAP_WIFI_RETRY_DELAY_MS = 500;
static constexpr uint32_t SNAP_WIFI_MONITOR_INTERVAL_MS = 1000;

// ---------- Snapserver ----------
inline IPAddress snapServerIp() { return IPAddress(192, 168, 5, 106); }
static constexpr uint16_t SNAP_SERVER_PORT = 1704;
static constexpr char SNAP_HOST_NAME[] = "esp32-wrover-snapclient-v6";
static constexpr char SNAP_CLIENT_NAME[] = "esp32-wrover-snapclient-v6";

// ---------- Bluetooth ----------
static constexpr char BLUETOOTH_DEVICE_NAME[] = "CoolCube";
static constexpr bool BLUETOOTH_AUTO_RECONNECT = false;
static constexpr uint32_t BLUETOOTH_IDLE_DELAY_MS = 25;
static constexpr uint32_t BLUETOOTH_DEFAULT_SAMPLE_RATE = 44100;

// ---------- Runtime mode switching ----------
// Cold boot defaults to Snapclient.
// A running-system button press toggles to the other mode and restarts.
static constexpr uint32_t MODE_SWITCH_DEBOUNCE_MS = 40;
static constexpr uint32_t MODE_SWITCH_RESTART_DELAY_MS = 100;
static constexpr uint32_t MODE_SWITCH_MAGIC = 0x534D4F44;  // "SMOD"

// ---------- Mode LED behavior ----------
// Snapclient mode uses a steady LED.
// Bluetooth mode uses a simple repeating blink to show the alternate mode clearly.
static constexpr uint32_t MODE_LED_BLUETOOTH_BLINK_INTERVAL_MS = 250;

// ---------- Audio format ----------
// Keep this aligned with the decoded Snapserver playback format.
static constexpr uint32_t AUDIO_SAMPLE_RATE = 48000;
static constexpr uint8_t AUDIO_BITS_PER_SAMPLE = 16;
static constexpr uint8_t AUDIO_CHANNELS = 2;

// ---------- I2S / DMA tuning ----------
// These are intentionally generous for the WROVER hardware and external DAC use.
// The goal here is stable playback rather than minimum latency.
static constexpr uint8_t I2S_DMA_BUFFER_COUNT = 24;
// Classic ESP32 I2S driver requires the DMA buffer size to stay within 8..1024.
static constexpr uint16_t I2S_DMA_BUFFER_SIZE = 1024;
static constexpr bool I2S_USE_AUDIO_PLL = true;

// ---------- Buffering / stability ----------
// Keep enough Opus buffering for Wi-Fi jitter without waiting so long that
// playback starts on stale data.
static constexpr uint32_t SNAP_OUTPUT_QUEUE_BYTES = 32768;
// Start the RTOS output task early once the queue has a modest amount of data.
static constexpr uint8_t SNAP_OUTPUT_ACTIVATION_PERCENT = 20;
static constexpr uint32_t PSRAM_ALLOC_THRESHOLD_BYTES = 4096;
static constexpr int SNAP_PROCESSING_LAG_MS = -172;
static constexpr int SNAP_SYNC_UPDATE_INTERVAL = 10;
static constexpr float SNAP_FIXED_PLAYBACK_FACTOR = 1.0f;

// ---------- Runtime ----------
static constexpr uint32_t CPU_FREQ_MHZ = 240;
static constexpr uint32_t SERIAL_BAUD = 115200;
static constexpr uint32_t MAIN_LOOP_DELAY_MS = 1;
static constexpr bool SNAP_USE_FAST_LOOP = true;
static constexpr uint32_t RESTART_DELAY_MS = 1500;
static constexpr uint32_t AUDIO_DEBUG_LOG_INTERVAL_MS = 1000;

}  // namespace app_config
