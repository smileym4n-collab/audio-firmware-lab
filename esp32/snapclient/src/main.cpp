/*
  Project: ESP32 audio client v4 (WROVER-IE Snapclient + Bluetooth prototype)
  Version: 0.4.0
  Framework: Arduino (PlatformIO)

  Pin map (ESP32-WROVER-IE-N16R8 -> external I2S DAC):
    GPIO26 -> I2S BCLK
    GPIO25 -> I2S LRCLK / WS
    GPIO22 -> I2S DOUT
    GPIO0  -> I2S MCLK
    GPIO32 -> Boot mode select button (active low with internal pull-up)

  Notes:
  - Boot with the button released for Snapclient mode.
  - Boot with the button held low for Bluetooth receiver mode.
  - Classic ESP32 MCLK routing is limited to GPIO0/GPIO1/GPIO3.
  - Wi-Fi and Snapserver settings are in include/snapclient_config.h.
  - Hardware pin assignments are in include/board_config.h.
*/

#include "AudioTools/AudioLibs/MemoryManager.h"

#include "bluetooth_mode.h"
#include "boot_mode_selector.h"
#include "board_config.h"
#include "runtime_mode.h"
#include "snapclient_config.h"
#include "snapclient_mode.h"

namespace {

audio_tools::MemoryManager gMemoryManager;
SnapclientMode gSnapclientMode;
BluetoothMode gBluetoothMode;
RuntimeMode *gActiveMode = nullptr;

void configureStatusLed() {
  if (board_config::STATUS_LED_PIN < 0) {
    return;
  }

  pinMode(board_config::STATUS_LED_PIN, OUTPUT);
  digitalWrite(board_config::STATUS_LED_PIN,
               board_config::STATUS_LED_ACTIVE_HIGH ? LOW : HIGH);
}

void setStatusLed(bool on) {
  if (board_config::STATUS_LED_PIN < 0) {
    return;
  }

  const int level = on ? (board_config::STATUS_LED_ACTIVE_HIGH ? HIGH : LOW)
                       : (board_config::STATUS_LED_ACTIVE_HIGH ? LOW : HIGH);
  digitalWrite(board_config::STATUS_LED_PIN, level);
}

void configurePsramAllocator() {
  if (!psramFound()) {
    Serial.println("[psram] not detected");
    return;
  }

  gMemoryManager.begin(app_config::PSRAM_ALLOC_THRESHOLD_BYTES);
  Serial.printf("[psram] size=%lu bytes, free=%lu bytes\n",
                static_cast<unsigned long>(ESP.getPsramSize()),
                static_cast<unsigned long>(ESP.getFreePsram()));
}

}  // namespace

void setup() {
  Serial.begin(app_config::SERIAL_BAUD);
  delay(200);

  configureStatusLed();
  setCpuFrequencyMhz(app_config::CPU_FREQ_MHZ);

  Serial.printf("\n[boot] %s\n", app_config::PROJECT_TITLE);
  Serial.printf("[version] %s\n", app_config::PROJECT_VERSION);
  Serial.printf("[target] %s\n", app_config::TARGET_MODULE);
  Serial.printf("[cpu] %u MHz\n", getCpuFrequencyMhz());
  configurePsramAllocator();

  const auto selectedMode = detectOperatingMode();
  gActiveMode = selectedMode == app_config::OperatingMode::Snapclient
                    ? static_cast<RuntimeMode *>(&gSnapclientMode)
                    : static_cast<RuntimeMode *>(&gBluetoothMode);

  Serial.printf("[boot] selected mode=%s\n", gActiveMode->name());
  setStatusLed(true);

  if (!gActiveMode->begin()) {
    Serial.println("[boot] mode start failed, restarting...");
    delay(app_config::RESTART_DELAY_MS);
    ESP.restart();
  }
}

void loop() {
  if (gActiveMode != nullptr) {
    gActiveMode->loop();
  } else {
    delay(100);
  }
}
