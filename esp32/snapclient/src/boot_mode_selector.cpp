#include "boot_mode_selector.h"

#include "board_config.h"

app_config::OperatingMode detectOperatingMode() {
  const int pinModeValue = board_config::BOOT_MODE_BUTTON_USE_PULLUP
                               ? INPUT_PULLUP
                               : INPUT_PULLDOWN;

  pinMode(board_config::BOOT_MODE_BUTTON_PIN, pinModeValue);
  delay(app_config::BOOT_MODE_SAMPLE_DELAY_MS);

  uint8_t activeSamples = 0;
  for (uint8_t sample = 0; sample < app_config::BOOT_MODE_STABLE_SAMPLES; ++sample) {
    if (digitalRead(board_config::BOOT_MODE_BUTTON_PIN) ==
        board_config::BOOT_MODE_BUTTON_ACTIVE_LEVEL) {
      ++activeSamples;
    }
    delay(app_config::BOOT_MODE_SAMPLE_DELAY_MS);
  }

  const bool buttonActive =
      activeSamples >= ((app_config::BOOT_MODE_STABLE_SAMPLES / 2) + 1);

  Serial.printf("[boot] mode button pin=%d active_samples=%u/%u\n",
                board_config::BOOT_MODE_BUTTON_PIN,
                activeSamples,
                app_config::BOOT_MODE_STABLE_SAMPLES);

  const auto selectedMode = buttonActive
                                ? app_config::BOOT_MODE_WHEN_BUTTON_ACTIVE
                                : app_config::BOOT_MODE_WHEN_BUTTON_INACTIVE;

  Serial.printf("[boot] mode button %s -> %s mode\n",
                buttonActive ? "active" : "inactive",
                app_config::operatingModeName(selectedMode));

  return selectedMode;
}
