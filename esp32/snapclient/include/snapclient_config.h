#pragma once

/*
  ESP32 audio client v5 configuration.
  Version: 0.5.0
  Edit values below for your local network, stream, and Bluetooth naming.
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

static constexpr char PROJECT_TITLE[] = "ESP32 Audio Client v5";
static constexpr char PROJECT_VERSION[] = "0.5.0";
static constexpr char TARGET_MODULE[] = "ESP32-WROVER-IE-N16R8";

// ---------- Wi-Fi ----------
static constexpr char SNAP_WIFI_SSID[] = "TomEmmaWireless";
static constexpr char SNAP_WIFI_PASSWORD[] = "aw3s0m3w1ththr33s";
static constexpr uint32_t SNAP_WIFI_CONNECT_TIMEOUT_MS = 20000;
static constexpr uint32_t SNAP_WIFI_RETRY_DELAY_MS = 500;
static constexpr uint32_t SNAP_WIFI_MONITOR_INTERVAL_MS = 1000;

// ---------- Snapserver ----------
inline IPAddress snapServerIp() { return IPAddress(192, 168, 5, 252); }
static constexpr uint16_t SNAP_SERVER_PORT = 1704;
static constexpr char SNAP_HOST_NAME[] = "esp32-wrover-snapclient-v5";
static constexpr char SNAP_CLIENT_NAME[] = "esp32-wrover-snapclient-v5";

// ---------- Bluetooth ----------
static constexpr char BLUETOOTH_DEVICE_NAME[] = "ESP32 Audio Receiver v5";
static constexpr bool BLUETOOTH_AUTO_RECONNECT = false;
static constexpr uint32_t BLUETOOTH_IDLE_DELAY_MS = 25;
static constexpr uint32_t BLUETOOTH_DEFAULT_SAMPLE_RATE = 44100;

// ---------- Boot-time mode selection ----------
// Default boot path is Snapclient mode.
// If the momentary button is detected active during startup, boot Bluetooth mode.
static constexpr OperatingMode BOOT_MODE_WHEN_BUTTON_ACTIVE =
    OperatingMode::Bluetooth;
static constexpr OperatingMode BOOT_MODE_WHEN_BUTTON_INACTIVE =
    OperatingMode::Snapclient;
static constexpr uint8_t BOOT_MODE_STABLE_SAMPLES = 8;
static constexpr uint32_t BOOT_MODE_SAMPLE_DELAY_MS = 5;

// ---------- Mode LED behavior ----------
// Snapclient mode uses a steady LED.
// Bluetooth mode uses a simple repeating blink to show the alternate mode clearly.
static constexpr uint32_t MODE_LED_BLUETOOTH_BLINK_INTERVAL_MS = 250;

// ---------- Audio format ----------
// Keep this aligned with the Snapserver PCM stream profile.
static constexpr uint32_t AUDIO_SAMPLE_RATE = 48000;
static constexpr uint8_t AUDIO_BITS_PER_SAMPLE = 16;
static constexpr uint8_t AUDIO_CHANNELS = 2;

// ---------- I2S / DMA tuning ----------
static constexpr uint8_t I2S_DMA_BUFFER_COUNT = 16;
static constexpr uint16_t I2S_DMA_BUFFER_SIZE = 1024;
static constexpr bool I2S_USE_AUDIO_PLL = true;

// ---------- Buffering / stability ----------
static constexpr uint32_t SNAP_OUTPUT_QUEUE_BYTES = 32768;
static constexpr uint8_t SNAP_OUTPUT_ACTIVATION_PERCENT = 60;
static constexpr uint32_t PSRAM_ALLOC_THRESHOLD_BYTES = 4096;

// ---------- Runtime ----------
static constexpr uint32_t CPU_FREQ_MHZ = 240;
static constexpr uint32_t SERIAL_BAUD = 115200;
static constexpr uint32_t MAIN_LOOP_DELAY_MS = 1;
static constexpr bool SNAP_USE_FAST_LOOP = true;
static constexpr uint32_t RESTART_DELAY_MS = 1500;

}  // namespace app_config
