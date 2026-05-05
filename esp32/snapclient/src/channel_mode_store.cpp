#include "channel_mode_store.h"

#include <Preferences.h>

namespace {

constexpr char kPrefsNamespace[] = "audio-route";
constexpr char kPrefsModeKey[] = "channel";

}  // namespace

app_config::ChannelMode loadChannelModePreference() {
  Preferences prefs;
  if (!prefs.begin(kPrefsNamespace, true)) {
    Serial.println("[channel] failed to open channel mode preferences");
    return app_config::ChannelMode::Stereo;
  }

  const uint8_t storedMode = prefs.getUChar(
      kPrefsModeKey, static_cast<uint8_t>(app_config::ChannelMode::Stereo));
  prefs.end();

  if (storedMode <= static_cast<uint8_t>(app_config::ChannelMode::Right)) {
    return static_cast<app_config::ChannelMode>(storedMode);
  }

  return app_config::ChannelMode::Stereo;
}

void saveChannelModePreference(app_config::ChannelMode mode) {
  Preferences prefs;
  if (!prefs.begin(kPrefsNamespace, false)) {
    Serial.println("[channel] failed to store channel mode preference");
    return;
  }

  prefs.putUChar(kPrefsModeKey, static_cast<uint8_t>(mode));
  prefs.end();
}
