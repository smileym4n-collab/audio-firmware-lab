/*
  Project: ESP32 audio client v9.30 (SnapApp channel control API)
  Version: 0.13.0
  Framework: Arduino (PlatformIO)

  Pin map (ESP32-WROVER-IE-N16R8 -> external I2S DAC):
    GPIO26 -> I2S BCLK
    GPIO25 -> I2S LRCLK / WS
    GPIO13 -> I2S DOUT
    GPIO0  -> Optional I2S MCLK when enabled in board_config.h
    GPIO34 -> SENSE / battery voltage divider input
    GPIO23 -> Runtime mode-toggle button (active low with internal pull-up)
    GPIO32 -> Wi-Fi/Snapclient status LED (active high)
    GPIO33 -> Bluetooth status LED (active high)

  Notes:
  - Cold boot always starts in Snapclient mode.
  - Press the runtime mode button to reboot into the other mode.
  - Snapclient mode exposes a local HTTP control API on port 8080.
  - Snapclient mode drives the Wi-Fi LED solid on.
  - Bluetooth mode blinks the BT LED.
  - MCLK is optional and disabled by default for PCM5102-style builds.
  - Classic ESP32 MCLK routing is limited to GPIO0/GPIO1/GPIO3 when enabled.
  - Wi-Fi and Snapserver settings are in include/snapclient_config.h.
  - Hardware pin assignments are in include/board_config.h.
*/

#include "AudioTools/AudioLibs/MemoryManager.h"

#include "bluetooth_mode.h"
#include "boot_mode_selector.h"
#include "board_config.h"
#include "mode_led_controller.h"
#include "mode_switch_controller.h"
#include "runtime_mode.h"
#include "snapclient_config.h"
#include "snapclient_mode.h"

namespace {

audio_tools::MemoryManager gMemoryManager;
SnapclientMode gSnapclientMode;
BluetoothMode gBluetoothMode;
ModeLedController gModeLed;
ModeSwitchController gModeSwitch;
RuntimeMode *gActiveMode = nullptr;

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

  gModeLed.begin();
  setCpuFrequencyMhz(app_config::CPU_FREQ_MHZ);

  Serial.printf("\n[boot] %s\n", app_config::PROJECT_TITLE);
  Serial.printf("[version] %s\n", app_config::FIRMWARE_VERSION);
  Serial.printf("[target] %s\n", app_config::TARGET_MODULE);
  Serial.printf("[cpu] %u MHz\n", getCpuFrequencyMhz());
  configurePsramAllocator();

  const auto selectedMode = detectOperatingMode();
  gActiveMode = selectedMode == app_config::OperatingMode::Snapclient
                    ? static_cast<RuntimeMode *>(&gSnapclientMode)
                    : static_cast<RuntimeMode *>(&gBluetoothMode);

  Serial.printf("[boot] selected mode=%s\n", gActiveMode->name());
  Serial.printf("[led] wifi=GPIO%d solid in Snapclient, bt=GPIO%d blink in Bluetooth\n",
                board_config::WIFI_STATUS_LED_PIN,
                board_config::BT_STATUS_LED_PIN);
  Serial.printf("[button] pin=%d, press while running to toggle mode and reboot\n",
                board_config::BOOT_MODE_BUTTON_PIN);
  Serial.printf("[battery] sense=%s pin=%d\n",
                board_config::BATTERY_SENSE_ENABLED ? "enabled" : "disabled",
                board_config::BATTERY_SENSE_PIN);
  Serial.println("[serial] commands: 'b' -> Bluetooth, 's' -> Snapclient, 't' -> toggle");
  gModeLed.setMode(selectedMode);
  gModeSwitch.begin(selectedMode);
  gModeSwitch.setRuntimeMode(gActiveMode);

  if (!gActiveMode->begin()) {
    Serial.println("[boot] mode start failed, restarting...");
    delay(app_config::RESTART_DELAY_MS);
    ESP.restart();
  }
}

void loop() {
  gModeLed.update();
  gModeSwitch.update();

  if (gActiveMode != nullptr) {
    gActiveMode->loop();
  } else {
    delay(100);
  }
}
