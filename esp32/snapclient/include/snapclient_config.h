#pragma once

/*
  ESP32 audio client v9.30 configuration.
  Version: 0.13.0
  Edit values below for your local network, Snapserver, and Bluetooth naming.
*/

#include <Arduino.h>

#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "Copy include/secrets.example.h to include/secrets.h and set Wi-Fi credentials."
#define SNAP_WIFI_SSID_SECRET ""
#define SNAP_WIFI_PASSWORD_SECRET ""
#endif

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

static constexpr char PROJECT_TITLE[] = "ESP32 Audio Client v9.30";
static constexpr char FIRMWARE_VERSION[] = "0.13.0";
static constexpr char TARGET_MODULE[] = "ESP32-WROVER-IE-N16R8";

// ---------- Wi-Fi ----------
static constexpr char SNAP_WIFI_SSID[] = SNAP_WIFI_SSID_SECRET;
static constexpr char SNAP_WIFI_PASSWORD[] = SNAP_WIFI_PASSWORD_SECRET;
static constexpr uint32_t SNAP_WIFI_CONNECT_TIMEOUT_MS = 20000;
static constexpr uint32_t SNAP_WIFI_RETRY_DELAY_MS = 500;
static constexpr uint32_t SNAP_WIFI_MONITOR_INTERVAL_MS = 1000;

// ---------- Local control API ----------
// Used by companion apps for ESP32-specific controls that Snapserver does not
// expose, such as local channel routing.
static constexpr uint16_t CONTROL_API_PORT = 8080;

// ---------- Battery monitor ----------
// Hardware divider: battery positive -> R_TOP -> ADC pin -> R_BOTTOM -> GND.
// A full 4S Li-ion pack at 16.8 V reads about 2.49 V with 270k/47k.
static constexpr float BATTERY_R_TOP_OHMS = 270000.0f;
static constexpr float BATTERY_R_BOTTOM_OHMS = 47000.0f;
static constexpr uint8_t BATTERY_ADC_SAMPLES = 16;
static constexpr uint32_t BATTERY_POLL_INTERVAL_MS = 5000;
static constexpr float BATTERY_ADC_REF_VOLTAGE = 3.3f;
static constexpr float BATTERY_ADC_FULL_SCALE_COUNTS = 4095.0f;
static constexpr float BATTERY_PERCENT_SMOOTH_ALPHA = 0.20f;
static constexpr uint32_t BATTERY_ADC_DEFAULT_VREF_MV = 1100;

// ---------- Snapserver ----------
inline IPAddress snapServerIp() { return IPAddress(192, 168, 5, 106); }
static constexpr uint16_t SNAP_SERVER_PORT = 1704;
static constexpr char SNAP_HOST_NAME[] = "esp32-wrover-snapclient-v6";
static constexpr char SNAP_CLIENT_NAME[] = "esp32-wrover-snapclient-v6";

// ---------- Bluetooth ----------
static constexpr char BLUETOOTH_DEVICE_NAME[] = "CoolCube";
static constexpr size_t BLUETOOTH_DEVICE_NAME_MAX_LENGTH = 31;
static constexpr bool BLUETOOTH_AUTO_RECONNECT = false;
static constexpr uint32_t BLUETOOTH_IDLE_DELAY_MS = 25;
static constexpr uint32_t BLUETOOTH_DEFAULT_SAMPLE_RATE = 44100;

// ---------- Runtime mode switching ----------
// Cold boot defaults to Snapclient.
// A running-system button press toggles to the other mode and restarts.
static constexpr uint32_t MODE_SWITCH_DEBOUNCE_MS = 40;
static constexpr uint32_t MODE_SWITCH_RESTART_DELAY_MS = 100;
static constexpr uint32_t MODE_SWITCH_MAGIC = 0x534D4F44;  // "SMOD"

// ---------- Status LED behavior ----------
// Snapclient mode uses a steady LED.
// Bluetooth mode uses a simple repeating blink to show the alternate mode clearly.
static constexpr uint32_t MODE_LED_BLUETOOTH_BLINK_INTERVAL_MS = 250;

// ---------- Audio format ----------
// Keep this aligned with the Snapserver PCM stream profile and the external DAC.
static constexpr uint32_t AUDIO_SAMPLE_RATE = 48000;
static constexpr uint8_t AUDIO_BITS_PER_SAMPLE = 16;
static constexpr uint8_t AUDIO_CHANNELS = 2;

// ---------- I2S / DMA tuning ----------
// These are intentionally generous for the WROVER hardware and external DAC use.
// The goal here is stable playback rather than minimum latency.
static constexpr uint8_t I2S_DMA_BUFFER_COUNT = 24;
// Classic ESP32 I2S driver requires the DMA buffer size to stay within 8..1024.
static constexpr uint16_t I2S_DMA_BUFFER_SIZE = 1024;
// Bluetooth starts after the BT stack has allocated its task/heap, so keep its
// I2S DMA footprint smaller than the Snapclient Wi-Fi path.
static constexpr uint8_t BLUETOOTH_I2S_DMA_BUFFER_COUNT = 8;
static constexpr uint16_t BLUETOOTH_I2S_DMA_BUFFER_SIZE = 512;
static constexpr bool I2S_USE_AUDIO_PLL = true;
static constexpr uint32_t AUDIO_UNMUTE_RAMP_MS = 35;
static constexpr uint32_t AUDIO_MODE_CHANGE_MUTE_RAMP_MS = 35;

// ---------- Buffering / stability ----------
// Keep enough PCM buffering for Wi-Fi jitter without waiting so long that
// playback starts on stale data.
static constexpr uint32_t SNAP_OUTPUT_QUEUE_BYTES = 65536;
// Let the output task wait for a deeper PCM cushion before it starts draining.
static constexpr uint8_t SNAP_OUTPUT_ACTIVATION_PERCENT = 75;
// If the live queue falls under this threshold, pause output briefly so the
// FIFO/Wi-Fi path can rebuild a healthier cushion instead of juddering through.
static constexpr uint8_t SNAP_OUTPUT_REBUFFER_START_PERCENT = 55;
// Resume output only once the queue has climbed back to this safer level.
static constexpr uint8_t SNAP_OUTPUT_REBUFFER_RESUME_PERCENT = 75;
// Hard stop-and-refill rebuffering was useful for diagnosis, but on this PCM
// path it can create audible step changes. Leave it off when dynamic sync is
// active so the resampler can absorb small drift more gracefully.
static constexpr bool SNAPCLIENT_REBUFFER_ENABLED = false;
// Keep a little headroom for hot Spotify/librespot PCM and Snapclient's
// resampler so full-scale content does not crunch in the DAC path.
static constexpr float SNAPCLIENT_OUTPUT_GAIN = 0.70f;
// Final safety trim applied to the actual Snapclient PCM samples immediately
// before they are handed to I2S. This does not affect Bluetooth mode.
static constexpr float SNAPCLIENT_FINAL_PCM_GAIN = 0.50f;
// Re-enable the Snapclient resampler, but only allow very small drift
// corrections so the queue can stay centered without audible pitch wobble.
static constexpr bool SNAPCLIENT_USE_RESAMPLER = true;
static constexpr float SNAPCLIENT_MIN_PLAYBACK_FACTOR = 0.9990f;
static constexpr float SNAPCLIENT_MAX_PLAYBACK_FACTOR = 1.0010f;
static constexpr float SNAPCLIENT_UNITY_DEADBAND = 0.0002f;
// Keep the upstream processing-lag baseline so Snapcast startup timing still
// lands close to the server buffer target without changing Bluetooth logic.
static constexpr int SNAPCLIENT_PROCESSING_LAG_MS = -172;
// Adjust gently rather than chasing every update.
static constexpr int SNAPCLIENT_SYNC_INTERVAL = 25;
static constexpr uint32_t PSRAM_ALLOC_THRESHOLD_BYTES = 4096;
static constexpr uint32_t SNAP_OUTPUT_IDLE_TIMEOUT_MS = 3000;

// ---------- Runtime ----------
static constexpr uint32_t CPU_FREQ_MHZ = 240;
static constexpr uint32_t SERIAL_BAUD = 115200;
static constexpr uint32_t MAIN_LOOP_DELAY_MS = 1;
static constexpr bool SNAP_USE_FAST_LOOP = true;
static constexpr BaseType_t SNAPCLIENT_TASK_CORE = 1;
static constexpr UBaseType_t SNAPCLIENT_TASK_PRIORITY = 4;
static constexpr uint32_t SNAPCLIENT_TASK_STACK_WORDS = 8192;
static constexpr uint32_t SNAPCLIENT_TASK_DELAY_MS = 1;
// Leave periodic Snapclient stats off during live audio testing so the UART
// does not add avoidable scheduling pressure. Warnings/errors still log.
static constexpr bool SNAPCLIENT_PERIODIC_STATS_ENABLED = false;
static constexpr uint32_t RESTART_DELAY_MS = 1500;
static constexpr uint32_t AUDIO_DEBUG_LOG_INTERVAL_MS = 1000;

}  // namespace app_config
