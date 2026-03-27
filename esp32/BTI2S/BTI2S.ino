/*

// BTI2S
// Version: 0.5.0

  Project: BTI2S
  Target: ESP32 (Arduino framework)

  Summary:
  - Receives Bluetooth A2DP audio from a phone/computer.
  - Sends audio out over I2S (no MCLK) on fixed pins.
  - Bluetooth device name can be changed and stored in NVS via Serial command.
  - Rotary encoder controls output volume and mute.

  Serial usage (115200 baud):
  - name=YourNewName  -> store BT name and reboot
  - vol=0..100        -> set volume percent immediately (and unmute)
*/

#include <Arduino.h>
#include <BluetoothA2DPSink.h>
#include <Preferences.h>

// ------------------------------
// Pin map (project target routing; startup mute drives these pins low)
// ------------------------------
static constexpr int I2S_LRCK_PIN = 25;   // IO25 -> I2S LRCK / WS
static constexpr int I2S_BCK_PIN = 26;    // IO26 -> I2S BCK / SCK
static constexpr int I2S_DATA_PIN = 13;   // IO13 -> I2S DATA OUT

static constexpr int ENC_SW_PIN = 35;     // IO35 -> encoder switch (input-only pin, external pull-up expected)
static constexpr int ENC_A_PIN = 32;      // IO32 -> encoder channel A
static constexpr int ENC_B_PIN = 33;      // IO33 -> encoder channel B

// ------------------------------
// Startup/output behaviour
// ------------------------------
static constexpr unsigned long STARTUP_MUTE_HOLD_MS = 40;    // Hold I2S lines low at boot to reduce startup pops
static constexpr uint8_t DEFAULT_VOLUME_PERCENT = 70;         // Initial playback level after boot
static constexpr uint8_t VOLUME_STEP_PERCENT = 2;             // Encoder step size
static constexpr uint8_t MIN_VOLUME_PERCENT = 0;              // Minimum allowed volume
static constexpr uint8_t MAX_VOLUME_PERCENT = 100;            // Maximum allowed volume
static constexpr unsigned long SWITCH_DEBOUNCE_MS = 30;       // Debounce for encoder button

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
// Set false when no encoder is connected; avoids floating-input noise and unnecessary volume updates.
static constexpr bool ENABLE_ENCODER_CONTROLS = false;

static BluetoothA2DPSink &getA2DPSink() {
  static BluetoothA2DPSink sink;
  return sink;
}

Preferences preferences;
String btDeviceName;

uint8_t volumePercent = DEFAULT_VOLUME_PERCENT;
bool isMuted = false;
uint8_t lastEncoderAB = 0;
bool lastSwitchReading = true;
bool stableSwitchState = true;
unsigned long lastSwitchChangeAtMs = 0;


static void applyStartupMuteState() {
  // Keep all I2S output lines in a known inactive state while BT/I2S stack initializes.
  pinMode(I2S_BCK_PIN, OUTPUT);
  pinMode(I2S_LRCK_PIN, OUTPUT);
  pinMode(I2S_DATA_PIN, OUTPUT);

  digitalWrite(I2S_BCK_PIN, LOW);
  digitalWrite(I2S_LRCK_PIN, LOW);
  digitalWrite(I2S_DATA_PIN, LOW);
  delay(STARTUP_MUTE_HOLD_MS);
}

static void configureEncoderPins() {
  pinMode(ENC_A_PIN, INPUT_PULLUP);
  pinMode(ENC_B_PIN, INPUT_PULLUP);
  pinMode(ENC_SW_PIN, INPUT);

  uint8_t a = static_cast<uint8_t>(digitalRead(ENC_A_PIN));
  uint8_t b = static_cast<uint8_t>(digitalRead(ENC_B_PIN));
  lastEncoderAB = static_cast<uint8_t>((a << 1U) | b);

  lastSwitchReading = (digitalRead(ENC_SW_PIN) == LOW);
  stableSwitchState = lastSwitchReading;
}

static void applyOutputVolume() {
  uint8_t appliedVolumePercent = isMuted ? 0 : volumePercent;
  getA2DPSink().set_volume(appliedVolumePercent);

  if (ENABLE_SERIAL_DEBUG) {
    Serial.printf("Volume: %u%%  Mute: %s\n", static_cast<unsigned>(volumePercent), isMuted ? "ON" : "OFF");
  }
}

static int8_t readEncoderDelta() {
  static constexpr int8_t TRANSITIONS[16] = {
      0, -1, 1, 0,
      1, 0, 0, -1,
      -1, 0, 0, 1,
      0, 1, -1, 0,
  };

  uint8_t a = static_cast<uint8_t>(digitalRead(ENC_A_PIN));
  uint8_t b = static_cast<uint8_t>(digitalRead(ENC_B_PIN));
  uint8_t currentAB = static_cast<uint8_t>((a << 1U) | b);
  uint8_t index = static_cast<uint8_t>((lastEncoderAB << 2U) | currentAB);
  lastEncoderAB = currentAB;
  return TRANSITIONS[index];
}

static void handleEncoderControls() {
  static int8_t encoderAccumulator = 0;

  int8_t delta = readEncoderDelta();
  if (delta != 0) {
    encoderAccumulator = static_cast<int8_t>(encoderAccumulator + delta);

    if (encoderAccumulator >= 4) {
      encoderAccumulator = 0;
      if (volumePercent <= (MAX_VOLUME_PERCENT - VOLUME_STEP_PERCENT)) {
        volumePercent = static_cast<uint8_t>(volumePercent + VOLUME_STEP_PERCENT);
      } else {
        volumePercent = MAX_VOLUME_PERCENT;
      }
      if (isMuted) {
        isMuted = false;
      }
      applyOutputVolume();
    } else if (encoderAccumulator <= -4) {
      encoderAccumulator = 0;
      if (volumePercent >= (MIN_VOLUME_PERCENT + VOLUME_STEP_PERCENT)) {
        volumePercent = static_cast<uint8_t>(volumePercent - VOLUME_STEP_PERCENT);
      } else {
        volumePercent = MIN_VOLUME_PERCENT;
      }
      if (isMuted) {
        isMuted = false;
      }
      applyOutputVolume();
    }
  }

  bool switchPressed = (digitalRead(ENC_SW_PIN) == LOW);
  if (switchPressed != lastSwitchReading) {
    lastSwitchReading = switchPressed;
    lastSwitchChangeAtMs = millis();
  }

  if ((millis() - lastSwitchChangeAtMs) >= SWITCH_DEBOUNCE_MS && stableSwitchState != switchPressed) {
    stableSwitchState = switchPressed;
    if (stableSwitchState) {
      isMuted = !isMuted;
      applyOutputVolume();
    }
  }
}

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


static bool parseVolumePercent(const String &line, uint8_t &outVolume) {
  String valueText;

  if (line.startsWith("vol=")) {
    valueText = line.substring(4);
  } else if (line.startsWith("volume=")) {
    valueText = line.substring(7);
  } else {
    return false;
  }

  valueText.trim();
  if (valueText.isEmpty()) {
    return false;
  }

  for (size_t i = 0; i < static_cast<size_t>(valueText.length()); ++i) {
    if (!isDigit(valueText.charAt(static_cast<unsigned int>(i)))) {
      return false;
    }
  }

  long parsed = valueText.toInt();
  if (parsed < MIN_VOLUME_PERCENT || parsed > MAX_VOLUME_PERCENT) {
    return false;
  }

  outVolume = static_cast<uint8_t>(parsed);
  return true;
}

static void handleSerialCommands() {
  if (!ENABLE_SERIAL_DEBUG || !Serial.available()) {
    return;
  }

  String line = Serial.readStringUntil('\n');
  line.trim();

  uint8_t requestedVolume = 0;
  if (parseVolumePercent(line, requestedVolume)) {
    volumePercent = requestedVolume;
    isMuted = false;
    applyOutputVolume();
    Serial.printf("Volume set to %u%%\n", static_cast<unsigned>(volumePercent));
    return;
  }

  if (line.startsWith("name=")) {
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
    return;
  }

  Serial.println("Unknown command. Use: name=YourNewName or vol=0..100");
}


static void onConnectionStateChanged(esp_a2d_connection_state_t state, void * /*ptr*/) {
  if (!ENABLE_SERIAL_DEBUG) {
    return;
  }

  switch (state) {
    case ESP_A2D_CONNECTION_STATE_DISCONNECTED:
      Serial.println("A2DP: source disconnected");
      break;
    case ESP_A2D_CONNECTION_STATE_CONNECTING:
      Serial.println("A2DP: source connecting...");
      break;
    case ESP_A2D_CONNECTION_STATE_CONNECTED:
      Serial.println("A2DP: source connected");
      break;
    case ESP_A2D_CONNECTION_STATE_DISCONNECTING:
      Serial.println("A2DP: source disconnecting...");
      break;
    default:
      Serial.printf("A2DP: connection state=%d\n", static_cast<int>(state));
      break;
  }
}

void setup() {
  if (ENABLE_SERIAL_DEBUG) {
    Serial.begin(115200);
    delay(150);
    Serial.println();
    Serial.println("BTI2S startup");
  }

  if (ENABLE_SERIAL_DEBUG) Serial.println("setup: load BT name from NVS");
  btDeviceName = getStoredBluetoothName();
  if (ENABLE_SERIAL_DEBUG) Serial.println("setup: apply startup mute state");
  applyStartupMuteState();
  if (ENABLE_ENCODER_CONTROLS) {
    if (ENABLE_SERIAL_DEBUG) Serial.println("setup: init encoder pins");
    configureEncoderPins();
  } else if (ENABLE_SERIAL_DEBUG) {
    Serial.println("setup: encoder controls disabled (serial volume mode)");
  }

  if (ENABLE_SERIAL_DEBUG) Serial.println("setup: create deferred A2DP sink object");
  BluetoothA2DPSink &a2dpSink = getA2DPSink();

  a2dpSink.set_on_connection_state_changed(onConnectionStateChanged);
  if (ENABLE_SERIAL_DEBUG) Serial.println("setup: start A2DP sink");
  a2dpSink.start(btDeviceName.c_str());
  applyOutputVolume();

  if (ENABLE_SERIAL_DEBUG) {
    Serial.printf("Bluetooth device name: %s\n", btDeviceName.c_str());
    Serial.printf("I2S pins -> LRCK:%d BCK:%d DATA:%d\n", I2S_LRCK_PIN, I2S_BCK_PIN, I2S_DATA_PIN);
    Serial.printf("Encoder pins -> SW:%d A:%d B:%d\n", ENC_SW_PIN, ENC_A_PIN, ENC_B_PIN);
    Serial.println("Ready. Commands: name=YourNewName | vol=0..100");
    Serial.println("A2DP status will be logged on connect/disconnect events.");
  }
}

void loop() {
  if (ENABLE_ENCODER_CONTROLS) {
    handleEncoderControls();
  }
  handleSerialCommands();
  delay(2);
}
