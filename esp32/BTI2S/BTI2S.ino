/*
  Project: BTI2S
  Target: ESP32 (Arduino framework)

  Summary:
  - Receives Bluetooth A2DP audio from a phone/computer.
  - Sends audio out over I2S (no MCLK) on fixed pins.
  - Bluetooth device name can be changed and stored in NVS via Serial command.
  - Rotary encoder controls volume and mute.

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

static constexpr int ENC_SW_PIN = 35;     // IO35 -> Encoder push switch (input-only pin)
static constexpr int ENC_A_PIN = 32;      // IO32 -> Encoder phase A
static constexpr int ENC_B_PIN = 33;      // IO33 -> Encoder phase B

// ------------------------------
// Bluetooth naming configuration
// ------------------------------
static constexpr char PREF_NAMESPACE[] = "bti2s";      // NVS namespace
static constexpr char PREF_KEY_BT_NAME[] = "bt_name"; // NVS key for device name
static constexpr char DEFAULT_BT_NAME[] = "BTI2S";    // Used when no saved name exists
static constexpr size_t MAX_BT_NAME_LEN = 24;          // Conservative human-readable name limit

// ------------------------------
// Audio volume configuration
// ------------------------------
static constexpr uint8_t DEFAULT_VOLUME_PERCENT = 70;   // Startup volume when unmuted
static constexpr uint8_t MIN_VOLUME_PERCENT = 0;        // Library volume lower bound
static constexpr uint8_t MAX_VOLUME_PERCENT = 100;      // Library volume upper bound
static constexpr uint8_t VOLUME_STEP_PERCENT = 2;       // Volume change per encoder detent

// Switch debounce and polling
static constexpr uint32_t ENCODER_POLL_MS = 1;
static constexpr uint32_t SWITCH_DEBOUNCE_MS = 30;

// Enable serial logging and serial command interface.
// Set to false to reduce serial activity.
static constexpr bool ENABLE_SERIAL_DEBUG = true;

BluetoothA2DPSink a2dpSink;
Preferences preferences;
String btDeviceName;

uint8_t currentVolumePercent = DEFAULT_VOLUME_PERCENT;
uint8_t preMuteVolumePercent = DEFAULT_VOLUME_PERCENT;
bool isMuted = false;

uint8_t previousEncoderState = 0;
int8_t encoderAccumulator = 0;
int previousSwitchLevel = HIGH;
uint32_t lastSwitchTransitionMs = 0;
uint32_t lastEncoderPollMs = 0;

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

static void applyVolume() {
  const uint8_t applied = isMuted ? 0 : currentVolumePercent;
  a2dpSink.set_volume(applied);

  if (ENABLE_SERIAL_DEBUG) {
    Serial.printf("Audio volume -> %u%% (muted: %s)\n", applied, isMuted ? "yes" : "no");
  }
}

static void setVolumeFromEncoderStep(int8_t stepDirection) {
  if (stepDirection == 0) {
    return;
  }

  int newVolume = static_cast<int>(currentVolumePercent) + (stepDirection * static_cast<int>(VOLUME_STEP_PERCENT));
  if (newVolume < MIN_VOLUME_PERCENT) {
    newVolume = MIN_VOLUME_PERCENT;
  }
  if (newVolume > MAX_VOLUME_PERCENT) {
    newVolume = MAX_VOLUME_PERCENT;
  }

  currentVolumePercent = static_cast<uint8_t>(newVolume);
  if (!isMuted) {
    preMuteVolumePercent = currentVolumePercent;
  }

  applyVolume();
}

static void toggleMute() {
  if (!isMuted) {
    preMuteVolumePercent = currentVolumePercent;
    isMuted = true;
  } else {
    isMuted = false;
    currentVolumePercent = preMuteVolumePercent;
  }

  applyVolume();
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

static void initEncoderPins() {
  // GPIO35 is input-only and does not support internal pull-up on ESP32.
  // Assumption: encoder switch line has external pull-up (or pull-down) in hardware.
  pinMode(ENC_SW_PIN, INPUT);

  pinMode(ENC_A_PIN, INPUT_PULLUP);
  pinMode(ENC_B_PIN, INPUT_PULLUP);

  previousEncoderState = (static_cast<uint8_t>(digitalRead(ENC_A_PIN)) << 1) | static_cast<uint8_t>(digitalRead(ENC_B_PIN));
  previousSwitchLevel = digitalRead(ENC_SW_PIN);
}

static void pollEncoder() {
  const uint32_t now = millis();
  if (now - lastEncoderPollMs < ENCODER_POLL_MS) {
    return;
  }
  lastEncoderPollMs = now;

  const uint8_t currentState = (static_cast<uint8_t>(digitalRead(ENC_A_PIN)) << 1) | static_cast<uint8_t>(digitalRead(ENC_B_PIN));
  if (currentState != previousEncoderState) {
    static const int8_t transitionTable[16] = {
        0, -1, +1, 0,
        +1, 0, 0, -1,
        -1, 0, 0, +1,
        0, +1, -1, 0
    };

    const uint8_t index = (previousEncoderState << 2) | currentState;
    encoderAccumulator += transitionTable[index];
    previousEncoderState = currentState;

    if (encoderAccumulator >= 4) {
      encoderAccumulator = 0;
      setVolumeFromEncoderStep(+1);
    } else if (encoderAccumulator <= -4) {
      encoderAccumulator = 0;
      setVolumeFromEncoderStep(-1);
    }
  }

  const int switchLevel = digitalRead(ENC_SW_PIN);
  if (switchLevel != previousSwitchLevel) {
    if (now - lastSwitchTransitionMs >= SWITCH_DEBOUNCE_MS) {
      lastSwitchTransitionMs = now;
      previousSwitchLevel = switchLevel;

      // Active-low press assumed (common encoder wiring with pull-up).
      if (switchLevel == LOW) {
        toggleMute();
      }
    }
  }
}

void setup() {
  if (ENABLE_SERIAL_DEBUG) {
    Serial.begin(115200);
    delay(150);
    Serial.println();
    Serial.println("BTI2S startup");
  }

  initEncoderPins();
  btDeviceName = getStoredBluetoothName();

  i2s_pin_config_t i2sPins = {
      .bck_io_num = I2S_BCK_PIN,
      .ws_io_num = I2S_LRCK_PIN,
      .data_out_num = I2S_DATA_PIN,
      .data_in_num = I2S_PIN_NO_CHANGE,
  };

  a2dpSink.set_pin_config(i2sPins);
  a2dpSink.start(btDeviceName.c_str());
  applyVolume();

  if (ENABLE_SERIAL_DEBUG) {
    Serial.printf("Bluetooth device name: %s\n", btDeviceName.c_str());
    Serial.printf("I2S pins -> LRCK:%d BCK:%d DATA:%d\n", I2S_LRCK_PIN, I2S_BCK_PIN, I2S_DATA_PIN);
    Serial.printf("Encoder pins -> SW:%d A:%d B:%d\n", ENC_SW_PIN, ENC_A_PIN, ENC_B_PIN);
    Serial.println("Ready. Serial rename command: name=YourNewName");
    Serial.println("Encoder: rotate to change volume, push to toggle mute.");
  }
}

void loop() {
  pollEncoder();
  handleSerialCommands();
  delay(1);
}
