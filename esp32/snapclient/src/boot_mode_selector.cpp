#include "boot_mode_selector.h"

#include <esp_system.h>

#include <Preferences.h>

#include "board_config.h"

namespace {

constexpr char kPrefsNamespace[] = "boot-mode";
constexpr char kPrefsPendingKey[] = "pending";
constexpr char kPrefsModeKey[] = "mode";

bool loadPendingMode(app_config::OperatingMode &mode) {
  Preferences prefs;
  if (!prefs.begin(kPrefsNamespace, false)) {
    Serial.println("[boot] failed to open mode preferences");
    return false;
  }

  const bool hasPendingMode = prefs.getBool(kPrefsPendingKey, false);
  const uint8_t storedMode = prefs.getUChar(
      kPrefsModeKey, static_cast<uint8_t>(app_config::OperatingMode::Snapclient));

  if (hasPendingMode &&
      storedMode <= static_cast<uint8_t>(app_config::OperatingMode::Bluetooth)) {
    mode = static_cast<app_config::OperatingMode>(storedMode);
    prefs.putBool(kPrefsPendingKey, false);
    prefs.putUChar(kPrefsModeKey,
                   static_cast<uint8_t>(app_config::OperatingMode::Snapclient));
    prefs.end();
    return true;
  }

  prefs.end();
  return false;
}

}  // namespace

app_config::OperatingMode detectOperatingMode() {
  const auto resetReason = esp_reset_reason();
  Serial.printf("[boot] mode button pin=%d active_level=%d\n",
                board_config::BOOT_MODE_BUTTON_PIN,
                board_config::BOOT_MODE_BUTTON_ACTIVE_LEVEL);
  Serial.println("[boot] cold boot default=Snapclient");

  app_config::OperatingMode requestedMode = app_config::OperatingMode::Snapclient;
  if (loadPendingMode(requestedMode)) {
    Serial.printf("[boot] pending restart request -> %s mode (reset_reason=%d)\n",
                  app_config::operatingModeName(requestedMode),
                  static_cast<int>(resetReason));
    return requestedMode;
  }

  if (resetReason == ESP_RST_SW) {
    Serial.println("[boot] software restart with no stored mode change");
  } else {
    Serial.printf("[boot] reset_reason=%d\n", static_cast<int>(resetReason));
  }

  Serial.println("[boot] no pending mode change -> Snapclient mode");
  return app_config::OperatingMode::Snapclient;
}

void requestOperatingModeOnNextRestart(app_config::OperatingMode mode) {
  Preferences prefs;
  if (!prefs.begin(kPrefsNamespace, false)) {
    Serial.println("[boot] failed to store pending mode change");
    return;
  }

  prefs.putBool(kPrefsPendingKey, true);
  prefs.putUChar(kPrefsModeKey, static_cast<uint8_t>(mode));
  prefs.end();

  Serial.printf("[boot] stored next mode=%s\n",
                app_config::operatingModeName(mode));
}
