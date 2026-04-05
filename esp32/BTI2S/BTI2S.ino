/*

// BTI2S
// Version: 0.7.2

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
  - Firmware volume cap limits max applied output gain (see MAX_OUTPUT_VOLUME_PERCENT).
*/

#include <Arduino.h>
#include <BluetoothA2DPSink.h>
#include <Preferences.h>
#include <driver/i2s.h>

// ------------------------------
// Audio source mode selection
// ------------------------------
// Keep Bluetooth as the default because it is known-good in this project.
// AirPlay is staged as a conservative integration point in this sketch:
// - shared I2S output path is already in place
// - Bluetooth remains the default and known-good source
// - AirPlay mode currently falls back to Bluetooth unless a real AirPlay
//   backend is wired in at startAirPlaySource()
//
// Modes:
//   AUDIO_SOURCE_MODE_BLUETOOTH -> existing stable A2DP sink path (default)
//   AUDIO_SOURCE_MODE_AIRPLAY    -> initialization hook + safe Bluetooth fallback
#define AUDIO_SOURCE_MODE_BLUETOOTH 1
#define AUDIO_SOURCE_MODE_AIRPLAY 2
// Set true only after wiring a real AirPlay backend into startAirPlaySource().
static constexpr bool AIRPLAY_BACKEND_ENABLED = false;

#ifndef AUDIO_SOURCE_MODE
#define AUDIO_SOURCE_MODE AUDIO_SOURCE_MODE_BLUETOOTH
#endif


// ------------------------------
// Pin map (fixed by project requirements)
// ------------------------------
static constexpr int I2S_LRCK_PIN = 25;   // IO25 -> I2S LRCK / WS
static constexpr int I2S_BCK_PIN = 26;    // IO26 -> I2S BCK / SCK
static constexpr int I2S_DATA_PIN = 13;   // IO13 -> I2S DATA OUT
static constexpr i2s_port_t I2S_PORT = I2S_NUM_0;

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
static constexpr uint8_t MAX_VOLUME_PERCENT = 100;            // Maximum allowed user-set volume
static constexpr uint8_t MAX_OUTPUT_VOLUME_PERCENT = 85;      // Firmware output cap to reduce downstream DAC clipping
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
// Prints periodic I2S write activity to verify that PCM is flowing.
static constexpr bool ENABLE_AUDIO_PATH_DEBUG = true;
// Set false when no encoder is connected; avoids floating-input noise and unnecessary volume updates.
static constexpr bool ENABLE_ENCODER_CONTROLS = false;
static bool i2sInitialized = false;
static bool activeSourceIsAirPlay = false;
static uint32_t currentI2SSampleRate = 44100;
static uint32_t i2sCallbackCount = 0;
static uint32_t i2sBytesWrittenTotal = 0;
static unsigned long lastI2SDebugAtMs = 0;

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

static bool initI2SOutput() {
  i2s_config_t i2sConfig;
  memset(&i2sConfig, 0, sizeof(i2sConfig));
  i2sConfig.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX);
  i2sConfig.sample_rate = 44100;
  i2sConfig.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  i2sConfig.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  i2sConfig.communication_format = I2S_COMM_FORMAT_STAND_MSB;
  i2sConfig.intr_alloc_flags = 0;
  i2sConfig.dma_buf_count = 8;
  i2sConfig.dma_buf_len = 64;
  i2sConfig.use_apll = false;
  i2sConfig.tx_desc_auto_clear = true;
  i2sConfig.fixed_mclk = 0;

  i2s_pin_config_t i2sPins;
  memset(&i2sPins, 0, sizeof(i2sPins));
  i2sPins.bck_io_num = I2S_BCK_PIN;
  i2sPins.ws_io_num = I2S_LRCK_PIN;
  i2sPins.data_out_num = I2S_DATA_PIN;
  i2sPins.data_in_num = I2S_PIN_NO_CHANGE;

  if (i2s_driver_install(I2S_PORT, &i2sConfig, 0, nullptr) != ESP_OK) {
    return false;
  }
  if (i2s_set_pin(I2S_PORT, &i2sPins) != ESP_OK) {
    i2s_driver_uninstall(I2S_PORT);
    return false;
  }
  i2s_zero_dma_buffer(I2S_PORT);
  i2sInitialized = true;
  return true;
}

// Shared I2S output path: Bluetooth PCM callback writes directly to the same I2S driver
// used by this project's DAC output pins.
static void i2sAudioDataCallback(const uint8_t *data, uint32_t len) {
  if (!i2sInitialized || data == nullptr || len == 0) {
    return;
  }
  size_t bytesWritten = 0;
  i2s_write(I2S_PORT, data, len, &bytesWritten, portMAX_DELAY);

  if (ENABLE_AUDIO_PATH_DEBUG) {
    i2sCallbackCount++;
    i2sBytesWrittenTotal += static_cast<uint32_t>(bytesWritten);
    unsigned long nowMs = millis();
    if ((nowMs - lastI2SDebugAtMs) >= 2000UL) {
      if (ENABLE_SERIAL_DEBUG) {
        Serial.printf("Audio flow: source=%s callbacks=%lu bytes=%lu sample_rate=%lu Hz\n",
                      activeSourceIsAirPlay ? "AirPlay" : "Bluetooth",
                      static_cast<unsigned long>(i2sCallbackCount),
                      static_cast<unsigned long>(i2sBytesWrittenTotal),
                      static_cast<unsigned long>(currentI2SSampleRate));
      }
      i2sCallbackCount = 0;
      i2sBytesWrittenTotal = 0;
      lastI2SDebugAtMs = nowMs;
    }
  }
}

static void i2sSampleRateCallback(uint16_t rate) {
  if (!i2sInitialized || rate == 0) {
    return;
  }
  currentI2SSampleRate = rate;
  i2s_set_clk(I2S_PORT, rate, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_STEREO);
  if (ENABLE_SERIAL_DEBUG) {
    Serial.printf("I2S sample rate set: %u Hz\n", static_cast<unsigned>(rate));
  }
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
  uint8_t requestedVolumePercent = isMuted ? 0 : volumePercent;
  uint8_t appliedVolumePercent = requestedVolumePercent;
  if (appliedVolumePercent > MAX_OUTPUT_VOLUME_PERCENT) {
    appliedVolumePercent = MAX_OUTPUT_VOLUME_PERCENT;
  }

  getA2DPSink().set_volume(appliedVolumePercent);

  if (ENABLE_SERIAL_DEBUG) {
    Serial.printf(
        "Volume requested: %u%%  applied: %u%%  cap: %u%%  Mute: %s\n",
        static_cast<unsigned>(volumePercent),
        static_cast<unsigned>(appliedVolumePercent),
        static_cast<unsigned>(MAX_OUTPUT_VOLUME_PERCENT),
        isMuted ? "ON" : "OFF");
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

  if (line.equalsIgnoreCase("status")) {
    Serial.printf("Requested source mode: %s\n",
                  (AUDIO_SOURCE_MODE == AUDIO_SOURCE_MODE_AIRPLAY) ? "AirPlay" : "Bluetooth");
    Serial.printf("Active source: %s\n", activeSourceIsAirPlay ? "AirPlay" : "Bluetooth");
    Serial.printf("AirPlay backend enabled: %s\n", AIRPLAY_BACKEND_ENABLED ? "true" : "false");
    Serial.printf("I2S initialized: %s, sample rate: %lu Hz\n",
                  i2sInitialized ? "true" : "false",
                  static_cast<unsigned long>(currentI2SSampleRate));
    return;
  }

  Serial.println("Unknown command. Use: name=YourNewName | vol=0..100 | status");
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

// Bluetooth source path:
// A2DP source -> ESP32 A2DP decoder -> i2sAudioDataCallback() -> shared I2S output
static void startBluetoothSource() {
  if (ENABLE_SERIAL_DEBUG) Serial.println("setup: create deferred A2DP sink object");
  BluetoothA2DPSink &a2dpSink = getA2DPSink();
  // Bluetooth audio path: A2DP decoder -> PCM callback -> shared I2S output
  a2dpSink.set_stream_reader(i2sAudioDataCallback, false);
  a2dpSink.set_sample_rate_callback(i2sSampleRateCallback);
  a2dpSink.set_on_connection_state_changed(onConnectionStateChanged);
  if (ENABLE_SERIAL_DEBUG) Serial.println("setup: start A2DP sink");
  a2dpSink.start(btDeviceName.c_str());
  activeSourceIsAirPlay = false;
  applyOutputVolume();
}

// AirPlay source hook:
// Intended future path: AirPlay/RAOP receiver -> PCM callback -> shared I2S output.
// Conservative behavior for now: if not integrated, return false and let caller
// fall back to Bluetooth automatically.
static bool startAirPlaySource() {
  if (ENABLE_SERIAL_DEBUG) {
    Serial.println("setup: AirPlay mode selected");
    Serial.printf("setup: AIRPLAY_BACKEND_ENABLED=%s\n", AIRPLAY_BACKEND_ENABLED ? "true" : "false");
    Serial.println("setup: no AirPlay backend linked in this sketch yet; falling back to Bluetooth");
    Serial.println("setup: to test AirPlay, keep AUDIO_SOURCE_MODE=AUDIO_SOURCE_MODE_AIRPLAY and wire backend code into startAirPlaySource()");
  }
  activeSourceIsAirPlay = false;
  if (AIRPLAY_BACKEND_ENABLED) {
    // Placeholder path for a future real AirPlay backend startup.
    // Return true when backend has started and is feeding i2sAudioDataCallback().
    activeSourceIsAirPlay = true;
    return true;
  }
  return false;
}

void setup() {
  // Mode initialization: select source mode, then bind selected source to shared I2S path.
  if (ENABLE_SERIAL_DEBUG) {
    Serial.begin(115200);
    delay(150);
    Serial.println();
    Serial.println("BTI2S startup");
    Serial.printf("Audio source mode requested: %s\n",
                  (AUDIO_SOURCE_MODE == AUDIO_SOURCE_MODE_AIRPLAY) ? "AirPlay" : "Bluetooth");
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

  if (ENABLE_SERIAL_DEBUG) Serial.println("setup: init explicit I2S output");
  if (!initI2SOutput()) {
    if (ENABLE_SERIAL_DEBUG) {
      Serial.println("ERROR: I2S init failed. Rebooting...");
    }
    delay(300);
    ESP.restart();
  }

  bool sourceStarted = false;
#if AUDIO_SOURCE_MODE == AUDIO_SOURCE_MODE_AIRPLAY
  sourceStarted = startAirPlaySource();
#endif
  if (!sourceStarted) {
    startBluetoothSource();
  }

  if (ENABLE_SERIAL_DEBUG) {
    Serial.printf("Active source after setup: %s\n", activeSourceIsAirPlay ? "AirPlay" : "Bluetooth");
    Serial.printf("Bluetooth device name: %s\n", btDeviceName.c_str());
    Serial.printf("I2S pins -> LRCK:%d BCK:%d DATA:%d\n", I2S_LRCK_PIN, I2S_BCK_PIN, I2S_DATA_PIN);
    Serial.printf("Encoder pins -> SW:%d A:%d B:%d\n", ENC_SW_PIN, ENC_A_PIN, ENC_B_PIN);
    Serial.println("Ready. Commands: name=YourNewName | vol=0..100 | status");
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
