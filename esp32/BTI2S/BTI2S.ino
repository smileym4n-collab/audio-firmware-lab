/*

// BTI2S
// Version: 0.1.0

  Project: BTI2S
  Target: ESP32 (Arduino framework)

  Summary:
  - Receives Bluetooth A2DP audio from a phone/computer.
  - Sends audio out over I2S (no MCLK) on fixed pins.
  - Bluetooth device name can be changed and stored in NVS via Serial command.

  Serial usage (115200 baud):
  - Send: name=YourNewName
  - Device stores the name and reboots.
*/

#include <Arduino.h>
#include <BluetoothA2DPSink.h>
#include <Preferences.h>

// ------------------------------
// Pin map (fixed by project requirements)
// ------------------------------
static constexpr int I2S_LRCK_PIN = 25;   // IO25 -> I2S LRCK / WS
static constexpr int I2S_BCK_PIN = 26;    // IO26 -> I2S BCK / SCK
static constexpr int I2S_DATA_PIN = 13;   // IO13 -> I2S DATA OUT

// ------------------------------
// Bluetooth naming configuration
// ------------------------------
static constexpr char PREF_NAMESPACE[] = "bti2s";      // NVS namespace
static constexpr char PREF_KEY_BT_NAME[] = "bt_name"; // NVS key for device name
static constexpr char DEFAULT_BT_NAME[] = "BTI2S";    // Used when no saved name exists
static constexpr size_t MAX_BT_NAME_LEN = 24;          // Conservative human-readable name limit

// Enable serial logging and serial command interface.
// Set to false to reduce serial activity.
static constexpr bool ENABLE_SERIAL_DEBUG = true;

BluetoothA2DPSink a2dpSink;
Preferences preferences;
String btDeviceName;

static String getStoredBluetoothName() {
  preferences.begin(PREF_NAMESPACE, true);
  String name = preferences.getString(PREF_KEY_BT_NAME, DEFAULT_BT_NAME);
  preferences.end();

  name.trim();
  if (name.isEmpty()) {
    return String(DEFAULT_BT_NAME);
  }
  if (name.length() > MAX_BT_NAME_LEN) {
    name.remove(MAX_BT_NAME_LEN);
  }
  return name;
}

static bool saveBluetoothName(const String &newName) {
  String cleanName = newName;
  cleanName.trim();

  if (cleanName.isEmpty() || cleanName.length() > MAX_BT_NAME_LEN) {
    return false;
  }

  preferences.begin(PREF_NAMESPACE, false);
  bool ok = preferences.putString(PREF_KEY_BT_NAME, cleanName) > 0;
  preferences.end();
  return ok;
}

static void handleSerialCommands() {
  if (!ENABLE_SERIAL_DEBUG || !Serial.available()) {
    return;
  }

  String line = Serial.readStringUntil('\n');
  line.trim();

  if (!line.startsWith("name=")) {
    Serial.println("Unknown command. Use: name=YourNewName");
    return;
  }

  String requestedName = line.substring(5);
  requestedName.trim();

  if (!saveBluetoothName(requestedName)) {
    Serial.printf("Invalid name. Use 1..%u characters.\n", static_cast<unsigned>(MAX_BT_NAME_LEN));
    return;
  }

  Serial.printf("Saved new Bluetooth name: %s\n", requestedName.c_str());
  Serial.println("Rebooting to apply the new name...");
  delay(100);
  ESP.restart();
}

void setup() {
  if (ENABLE_SERIAL_DEBUG) {
    Serial.begin(115200);
    delay(150);
    Serial.println();
    Serial.println("BTI2S startup");
  }

  btDeviceName = getStoredBluetoothName();

  i2s_pin_config_t i2sPins = {
      .bck_io_num = I2S_BCK_PIN,
      .ws_io_num = I2S_LRCK_PIN,
      .data_out_num = I2S_DATA_PIN,
      .data_in_num = I2S_PIN_NO_CHANGE,
  };

  a2dpSink.set_pin_config(i2sPins);
  a2dpSink.start(btDeviceName.c_str());

  if (ENABLE_SERIAL_DEBUG) {
    Serial.printf("Bluetooth device name: %s\n", btDeviceName.c_str());
    Serial.printf("I2S pins -> LRCK:%d BCK:%d DATA:%d\n", I2S_LRCK_PIN, I2S_BCK_PIN, I2S_DATA_PIN);
    Serial.println("Ready. Send command 'name=YourNewName' to rename and reboot.");
  }
}

void loop() {
  handleSerialCommands();
  delay(10);
}
