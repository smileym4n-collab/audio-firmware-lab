/*
  Project: BTI2S
  Target: ESP32 (Arduino framework)

  Summary:
  - Receives Bluetooth A2DP audio from a phone/computer.
  - Sends audio out over I2S (no MCLK) on fixed pins.
  - Rotary encoder controls volume (rotate) and mute (push).
  - Bluetooth device name can be changed over Serial and stored in NVS.

  Serial usage (115200 baud):
  - name=YourNewName
*/

#include <Arduino.h>
#include <BluetoothA2DPSink.h>
#include <Preferences.h>
#include <driver/i2s.h>  // Provides i2s_pin_config_t on ESP32 Arduino

// ------------------------------
// Pin map (fixed by project requirements)
// ------------------------------
static constexpr int I2S_LRCK_PIN = 25;   // IO25 -> I2S LRCK / WS
static constexpr int I2S_BCK_PIN  = 26;   // IO26 -> I2S BCK / SCK
static constexpr int I2S_DATA_PIN = 13;   // IO13 -> I2S DATA OUT

static constexpr int ENC_SW_PIN = 35;     // IO35 -> Encoder switch (input-only pin)
static constexpr int ENC_A_PIN  = 32;     // IO32 -> Encoder A
static constexpr int ENC_B_PIN  = 33;     // IO33 -> Encoder B

// ------------------------------
// Bluetooth name storage
// ------------------------------
static constexpr char PREF_NAMESPACE[] = "bti2s";      // NVS namespace
static constexpr char PREF_KEY_BT_NAME[] = "bt_name"; // NVS key
static constexpr char DEFAULT_BT_NAME[] = "BTI2S";    // Fallback device name
static constexpr size_t MAX_BT_NAME_LEN = 24;          // Does not include terminating NUL

// ------------------------------
// Audio control configuration
// ------------------------------
static constexpr uint8_t DEFAULT_VOLUME_PERCENT = 70;  // Startup volume
static constexpr uint8_t VOLUME_MIN = 0;
static constexpr uint8_t VOLUME_MAX = 100;
static constexpr uint8_t VOLUME_STEP = 2;              // Per encoder detent

static constexpr uint32_t ENCODER_POLL_MS = 1;
static constexpr uint32_t SWITCH_DEBOUNCE_MS = 30;

static constexpr bool ENABLE_SERIAL_DEBUG = true;

BluetoothA2DPSink a2dpSink;
Preferences preferences;

char btDeviceName[MAX_BT_NAME_LEN + 1] = {0};
char serialLine[64] = {0};
size_t serialLinePos = 0;

uint8_t currentVolume = DEFAULT_VOLUME_PERCENT;
uint8_t volumeBeforeMute = DEFAULT_VOLUME_PERCENT;
bool muted = false;

uint8_t prevEncoderState = 0;
int8_t encoderAccum = 0;
int prevSwitchLevel = HIGH;
uint32_t lastSwitchTransitionMs = 0;
uint32_t lastEncoderPollMs = 0;

static void copySafeName(char *dst, size_t dstLen, const char *src) {
  if (dstLen == 0) {
    return;
  }
  strncpy(dst, src, dstLen - 1);
  dst[dstLen - 1] = '\0';
}

static void loadBluetoothName() {
  preferences.begin(PREF_NAMESPACE, true);
  size_t readLen = preferences.getString(PREF_KEY_BT_NAME, btDeviceName, sizeof(btDeviceName));
  preferences.end();

  if (readLen == 0 || btDeviceName[0] == '\0') {
    copySafeName(btDeviceName, sizeof(btDeviceName), DEFAULT_BT_NAME);
    return;
  }

  btDeviceName[MAX_BT_NAME_LEN] = '\0';
}

static bool saveBluetoothName(const char *name) {
  const size_t len = strnlen(name, MAX_BT_NAME_LEN + 1);
  if (len == 0 || len > MAX_BT_NAME_LEN) {
    return false;
  }

  preferences.begin(PREF_NAMESPACE, false);
  size_t written = preferences.putString(PREF_KEY_BT_NAME, name);
  preferences.end();
  return written > 0;
}

static void applyVolume() {
  const uint8_t applied = muted ? 0 : currentVolume;
  a2dpSink.set_volume(applied);

  if (ENABLE_SERIAL_DEBUG) {
    Serial.printf("Volume %u%% (muted: %s)\n", applied, muted ? "yes" : "no");
  }
}

static void adjustVolume(int8_t direction) {
  if (direction == 0) {
    return;
  }

  int v = static_cast<int>(currentVolume) + direction * static_cast<int>(VOLUME_STEP);
  if (v < VOLUME_MIN) {
    v = VOLUME_MIN;
  }
  if (v > VOLUME_MAX) {
    v = VOLUME_MAX;
  }

  currentVolume = static_cast<uint8_t>(v);
  if (!muted) {
    volumeBeforeMute = currentVolume;
  }

  applyVolume();
}

static void toggleMute() {
  if (!muted) {
    volumeBeforeMute = currentVolume;
    muted = true;
  } else {
    muted = false;
    currentVolume = volumeBeforeMute;
  }

  applyVolume();
}

static void initEncoderPins() {
  // GPIO35 is input-only and has no internal pull-up on ESP32.
  // Assumption: encoder switch has external bias resistor in hardware.
  pinMode(ENC_SW_PIN, INPUT);

  pinMode(ENC_A_PIN, INPUT_PULLUP);
  pinMode(ENC_B_PIN, INPUT_PULLUP);

  prevEncoderState = (static_cast<uint8_t>(digitalRead(ENC_A_PIN)) << 1) |
                     static_cast<uint8_t>(digitalRead(ENC_B_PIN));
  prevSwitchLevel = digitalRead(ENC_SW_PIN);
}

static void pollEncoder() {
  const uint32_t now = millis();
  if ((now - lastEncoderPollMs) < ENCODER_POLL_MS) {
    return;
  }
  lastEncoderPollMs = now;

  const uint8_t encoderState = (static_cast<uint8_t>(digitalRead(ENC_A_PIN)) << 1) |
                               static_cast<uint8_t>(digitalRead(ENC_B_PIN));

  if (encoderState != prevEncoderState) {
    static const int8_t trans[16] = {
      0, -1, +1, 0,
      +1, 0, 0, -1,
      -1, 0, 0, +1,
      0, +1, -1, 0
    };

    const uint8_t idx = (prevEncoderState << 2) | encoderState;
    encoderAccum += trans[idx];
    prevEncoderState = encoderState;

    if (encoderAccum >= 4) {
      encoderAccum = 0;
      adjustVolume(+1);
    } else if (encoderAccum <= -4) {
      encoderAccum = 0;
      adjustVolume(-1);
    }
  }

  const int sw = digitalRead(ENC_SW_PIN);
  if (sw != prevSwitchLevel) {
    if ((now - lastSwitchTransitionMs) >= SWITCH_DEBOUNCE_MS) {
      lastSwitchTransitionMs = now;
      prevSwitchLevel = sw;

      // Active-low press expected with pull-up wiring.
      if (sw == LOW) {
        toggleMute();
      }
    }
  }
}

static void handleSerialRename() {
  if (!ENABLE_SERIAL_DEBUG) {
    return;
  }

  while (Serial.available() > 0) {
    const char c = static_cast<char>(Serial.read());

    if (c == '\r') {
      continue;
    }

    if (c == '\n') {
      serialLine[serialLinePos] = '\0';
      serialLinePos = 0;

      if (strncmp(serialLine, "name=", 5) != 0) {
        Serial.println("Unknown command. Use: name=YourNewName");
        continue;
      }

      const char *requestedName = &serialLine[5];
      const size_t len = strnlen(requestedName, MAX_BT_NAME_LEN + 1);
      if (len == 0 || len > MAX_BT_NAME_LEN) {
        Serial.printf("Invalid name. Use 1..%u characters.\n", static_cast<unsigned>(MAX_BT_NAME_LEN));
        continue;
      }

      if (!saveBluetoothName(requestedName)) {
        Serial.println("Failed to save name.");
        continue;
      }

      Serial.printf("Saved Bluetooth name: %s\n", requestedName);
      Serial.println("Rebooting to apply...\n");
      delay(100);
      ESP.restart();
      return;
    }

    if (serialLinePos < (sizeof(serialLine) - 1)) {
      serialLine[serialLinePos++] = c;
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
  loadBluetoothName();

  i2s_pin_config_t i2sPins = {
    .bck_io_num = I2S_BCK_PIN,
    .ws_io_num = I2S_LRCK_PIN,
    .data_out_num = I2S_DATA_PIN,
    .data_in_num = I2S_PIN_NO_CHANGE,
  };

  a2dpSink.start(btDeviceName);
  // Some BluetoothA2DPSink versions do not provide set_pin_config().
  // Apply pin routing directly via ESP32 I2S driver for compatibility.
  i2s_set_pin(I2S_NUM_0, &i2sPins);
  applyVolume();

  if (ENABLE_SERIAL_DEBUG) {
    Serial.printf("Bluetooth name: %s\n", btDeviceName);
    Serial.printf("I2S pins -> LRCK:%d BCK:%d DATA:%d\n", I2S_LRCK_PIN, I2S_BCK_PIN, I2S_DATA_PIN);
    Serial.printf("Encoder pins -> SW:%d A:%d B:%d\n", ENC_SW_PIN, ENC_A_PIN, ENC_B_PIN);
    Serial.println("Rotate encoder for volume, press encoder to mute/unmute.");
    Serial.println("Rename command: name=YourNewName");
  }
}

void loop() {
  pollEncoder();
  handleSerialRename();
  delay(1);
}
