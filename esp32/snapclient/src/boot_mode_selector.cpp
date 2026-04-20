#include "boot_mode_selector.h"

#include <esp_system.h>

#include "board_config.h"

namespace {

RTC_DATA_ATTR uint32_t gPendingModeMagic = 0;
RTC_DATA_ATTR uint8_t gPendingModeValue = 0;

}  // namespace

app_config::OperatingMode detectOperatingMode() {
  const auto resetReason = esp_reset_reason();
  Serial.printf("[boot] mode button pin=%d active_level=%d\n",
                board_config::BOOT_MODE_BUTTON_PIN,
                board_config::BOOT_MODE_BUTTON_ACTIVE_LEVEL);
  Serial.println("[boot] cold boot default=Snapclient");

  if (resetReason == ESP_RST_SW &&
      gPendingModeMagic == app_config::MODE_SWITCH_MAGIC &&
      gPendingModeValue <= static_cast<uint8_t>(app_config::OperatingMode::Bluetooth)) {
    const auto selectedMode =
        static_cast<app_config::OperatingMode>(gPendingModeValue);
    gPendingModeMagic = 0;
    gPendingModeValue = 0;

    Serial.printf("[boot] software restart request -> %s mode\n",
                  app_config::operatingModeName(selectedMode));
    return selectedMode;
  }

  gPendingModeMagic = 0;
  gPendingModeValue = 0;
  Serial.println("[boot] no pending mode change -> Snapclient mode");
  return app_config::OperatingMode::Snapclient;
}

void requestOperatingModeOnNextRestart(app_config::OperatingMode mode) {
  gPendingModeMagic = app_config::MODE_SWITCH_MAGIC;
  gPendingModeValue = static_cast<uint8_t>(mode);
}
