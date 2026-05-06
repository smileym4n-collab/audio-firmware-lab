#include "bluetooth_name_store.h"

#include <Preferences.h>

#include "snapclient_config.h"

namespace {

constexpr char kPrefsNamespace[] = "bt-config";
constexpr char kPrefsNameKey[] = "name";

}  // namespace

String loadBluetoothNamePreference() {
  Preferences prefs;
  if (!prefs.begin(kPrefsNamespace, true)) {
    Serial.println("[bluetooth] failed to open Bluetooth name preferences");
    return app_config::BLUETOOTH_DEVICE_NAME;
  }

  const String name = prefs.getString(kPrefsNameKey, app_config::BLUETOOTH_DEVICE_NAME);
  prefs.end();

  return isValidBluetoothName(name) ? name : app_config::BLUETOOTH_DEVICE_NAME;
}

void saveBluetoothNamePreference(const String &name) {
  Preferences prefs;
  if (!prefs.begin(kPrefsNamespace, false)) {
    Serial.println("[bluetooth] failed to store Bluetooth name preference");
    return;
  }

  prefs.putString(kPrefsNameKey, name);
  prefs.end();
}

bool isValidBluetoothName(const String &name) {
  const size_t length = name.length();
  if (length == 0 || length > app_config::BLUETOOTH_DEVICE_NAME_MAX_LENGTH) {
    return false;
  }

  for (size_t i = 0; i < length; ++i) {
    const char c = name.charAt(i);
    if (c < 32 || c > 126 || c == '"' || c == '\\') {
      return false;
    }
  }

  return true;
}
