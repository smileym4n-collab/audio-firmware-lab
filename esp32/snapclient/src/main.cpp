/*
  Project: ESP32 audio client v9.13 (runtime mode toggle with optional MCLK)
  Version: 0.9.13
  Framework: Arduino (PlatformIO)

  Pin map (ESP32-WROVER-IE-N16R8 -> external I2S DAC):
    GPIO26 -> I2S BCLK
    GPIO25 -> I2S LRCLK / WS
    GPIO13 -> I2S DOUT
    GPIO0  -> Optional I2S MCLK when enabled in board_config.h
    GPIO32 -> Runtime mode-toggle button (active low with internal pull-up)
    GPIO33 -> Mode-status LED

  Notes:
  - Cold boot always starts in Snapclient mode.
  - Press the runtime mode button to reboot into the other mode.
  - Snapclient mode drives the mode LED solid on.
  - Bluetooth mode blinks the mode LED.
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
  Serial.printf("[version] %s\n", app_config::PROJECT_VERSION);
  Serial.printf("[target] %s\n", app_config::TARGET_MODULE);
  Serial.printf("[cpu] %u MHz\n", getCpuFrequencyMhz());
  configurePsramAllocator();

  const auto selectedMode = detectOperatingMode();
  gActiveMode = selectedMode == app_config::OperatingMode::Snapclient
                    ? static_cast<RuntimeMode *>(&gSnapclientMode)
                    : static_cast<RuntimeMode *>(&gBluetoothMode);

  Serial.printf("[boot] selected mode=%s\n", gActiveMode->name());
  Serial.printf("[led] pin=%d, Snapclient=solid on, Bluetooth=blink\n",
                board_config::MODE_STATUS_LED_PIN);
  Serial.printf("[button] pin=%d, press while running to toggle mode and reboot\n",
                board_config::BOOT_MODE_BUTTON_PIN);
  Serial.println("[serial] commands: 'b' -> Bluetooth, 's' -> Snapclient, 't' -> toggle");
  gModeLed.setMode(selectedMode);
  gModeSwitch.begin(selectedMode);

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
