/*

// BTI2S
// Version: 0.7.0

  Project: BTI2S
  Target: ESP32 (Arduino framework)

  Summary:
  - Receives Bluetooth A2DP audio from a phone/computer.
  - Sends audio out over I2S (no MCLK) on fixed pins.
  - Bluetooth device name can be changed and stored in NVS via Serial command.
  - Audio output can be capped in firmware to reduce downstream DAC clipping.

  Serial usage (115200 baud):
  - name=YourNewName  -> store BT name and reboot
  - cap=0..100        -> set output cap percent (stored in NVS)
  - cap?              -> print active output cap percent
*/

#include <Arduino.h>
#include <BluetoothA2DPSink.h>
#include <Preferences.h>
#include <driver/i2s.h>

// ------------------------------
// Pin map (fixed by project requirements)
// ------------------------------
static constexpr int I2S_LRCK_PIN = 25;   // IO25 -> I2S LRCK / WS
static constexpr int I2S_BCK_PIN = 26;    // IO26 -> I2S BCK / SCK
static constexpr int I2S_DATA_PIN = 13;   // IO13 -> I2S DATA OUT
static constexpr i2s_port_t I2S_PORT = I2S_NUM_0;

// ------------------------------
// Startup/output behaviour
// ------------------------------
static constexpr unsigned long STARTUP_MUTE_HOLD_MS = 40;   // Hold I2S lines low at boot to reduce startup pops
static constexpr uint8_t MIN_PERCENT = 0;                   // Minimum valid percentage
static constexpr uint8_t MAX_PERCENT = 100;                 // Maximum valid percentage
static constexpr uint8_t DEFAULT_OUTPUT_CAP_PERCENT = 85;   // Default output cap to reduce downstream DAC clipping

// ------------------------------
// Bluetooth naming/cap configuration
// ------------------------------
static constexpr char PREF_NAMESPACE[] = "bti2s";          // NVS namespace
static constexpr char PREF_KEY_BT_NAME[] = "bt_name";      // NVS key for Bluetooth name
static constexpr char PREF_KEY_OUTPUT_CAP[] = "out_cap";   // NVS key for output cap percent
static constexpr char DEFAULT_BT_NAME[] = "BTI2S";         // Used when no saved name exists
static constexpr size_t MAX_BT_NAME_LEN = 24;               // Conservative human-readable name limit

// Enable serial logging and serial command interface.
// Set to false to reduce serial activity.
static constexpr bool ENABLE_SERIAL_DEBUG = true;
static bool i2sInitialized = false;

static BluetoothA2DPSink &getA2DPSink() {
  static BluetoothA2DPSink sink;
  return sink;
}

Preferences preferences;
String btDeviceName;
uint8_t outputCapPercent = DEFAULT_OUTPUT_CAP_PERCENT;

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

static int16_t applyGainPercentToSample(int16_t sample, uint8_t gainPercent) {
  int32_t scaled = (static_cast<int32_t>(sample) * static_cast<int32_t>(gainPercent)) / MAX_PERCENT;
  if (scaled > 32767) {
    scaled = 32767;
  } else if (scaled < -32768) {
    scaled = -32768;
  }
  return static_cast<int16_t>(scaled);
}

static void writeCappedAudio(const uint8_t *data, uint32_t len) {
  static constexpr size_t PROCESS_CHUNK_BYTES = 256;
  int16_t sampleBuffer[PROCESS_CHUNK_BYTES / sizeof(int16_t)];

  const uint8_t *cursor = data;
  uint32_t remaining = len;

  while (remaining > 0) {
    uint32_t chunkLen = remaining > PROCESS_CHUNK_BYTES ? PROCESS_CHUNK_BYTES : remaining;

    if (outputCapPercent >= MAX_PERCENT) {
      size_t bytesWritten = 0;
      i2s_write(I2S_PORT, cursor, chunkLen, &bytesWritten, portMAX_DELAY);
    } else {
      uint32_t evenBytes = chunkLen & ~1U;
      uint32_t sampleCount = evenBytes / sizeof(int16_t);

      const int16_t *srcSamples = reinterpret_cast<const int16_t *>(cursor);
      for (uint32_t i = 0; i < sampleCount; ++i) {
        sampleBuffer[i] = applyGainPercentToSample(srcSamples[i], outputCapPercent);
      }

      size_t bytesWritten = 0;
      i2s_write(I2S_PORT, sampleBuffer, evenBytes, &bytesWritten, portMAX_DELAY);

      // For safety if odd byte is ever received, pass it through unchanged.
      if ((chunkLen & 1U) != 0U) {
        i2s_write(I2S_PORT, cursor + evenBytes, 1, &bytesWritten, portMAX_DELAY);
      }
    }

    cursor += chunkLen;
    remaining -= chunkLen;
  }
}

static void i2sAudioDataCallback(const uint8_t *data, uint32_t len) {
  if (!i2sInitialized || data == nullptr || len == 0) {
    return;
  }
  writeCappedAudio(data, len);
}

static void i2sSampleRateCallback(uint16_t rate) {
  if (!i2sInitialized || rate == 0) {
    return;
  }
  i2s_set_clk(I2S_PORT, rate, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_STEREO);
  if (ENABLE_SERIAL_DEBUG) {
    Serial.printf("I2S sample rate set: %u Hz\n", static_cast<unsigned>(rate));
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

static uint8_t clampPercent(uint8_t value) {
  if (value > MAX_PERCENT) {
    return MAX_PERCENT;
  }
  return value;
}

static uint8_t getStoredOutputCapPercent() {
  preferences.begin(PREF_NAMESPACE, true);
  uint32_t rawCap = preferences.getUInt(PREF_KEY_OUTPUT_CAP, DEFAULT_OUTPUT_CAP_PERCENT);
  preferences.end();
  if (rawCap > MAX_PERCENT) {
    return DEFAULT_OUTPUT_CAP_PERCENT;
  }
  return static_cast<uint8_t>(rawCap);
}

static bool saveOutputCapPercent(uint8_t newCapPercent) {
  uint8_t cleanCap = clampPercent(newCapPercent);

  preferences.begin(PREF_NAMESPACE, false);
  bool ok = preferences.putUInt(PREF_KEY_OUTPUT_CAP, static_cast<uint32_t>(cleanCap)) > 0;
  preferences.end();

  if (ok) {
    outputCapPercent = cleanCap;
  }

  return ok;
}

static bool parsePercentValue(const String &line, const char *prefix, uint8_t &outPercent) {
  if (!line.startsWith(prefix)) {
    return false;
  }

  String valueText = line.substring(strlen(prefix));
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
  if (parsed < MIN_PERCENT || parsed > MAX_PERCENT) {
    return false;
  }

  outPercent = static_cast<uint8_t>(parsed);
  return true;
}

static void handleSerialCommands() {
  if (!ENABLE_SERIAL_DEBUG || !Serial.available()) {
    return;
  }

  String line = Serial.readStringUntil('\n');
  line.trim();

  if (line.equalsIgnoreCase("cap?")) {
    Serial.printf("Output cap: %u%%\n", static_cast<unsigned>(outputCapPercent));
    return;
  }

  uint8_t requestedCap = 0;
  if (parsePercentValue(line, "cap=", requestedCap)) {
    if (saveOutputCapPercent(requestedCap)) {
      Serial.printf("Output cap saved/applied: %u%%\n", static_cast<unsigned>(outputCapPercent));
    } else {
      Serial.println("Failed to save output cap to NVS.");
    }
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

  Serial.println("Unknown command. Use: name=YourNewName | cap=0..100 | cap?");
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
  outputCapPercent = getStoredOutputCapPercent();

  if (ENABLE_SERIAL_DEBUG) Serial.println("setup: apply startup mute state");
  applyStartupMuteState();

  if (ENABLE_SERIAL_DEBUG) Serial.println("setup: create deferred A2DP sink object");
  BluetoothA2DPSink &a2dpSink = getA2DPSink();

  if (ENABLE_SERIAL_DEBUG) Serial.println("setup: init explicit I2S output");
  if (!initI2SOutput()) {
    if (ENABLE_SERIAL_DEBUG) {
      Serial.println("ERROR: I2S init failed. Rebooting...");
    }
    delay(300);
    ESP.restart();
  }

  a2dpSink.set_stream_reader(i2sAudioDataCallback, false);
  a2dpSink.set_sample_rate_callback(i2sSampleRateCallback);
  a2dpSink.set_on_connection_state_changed(onConnectionStateChanged);

  // Leave A2DP sink volume at 100% so Bluetooth source controls still reach full range.
  a2dpSink.set_volume(MAX_PERCENT);

  if (ENABLE_SERIAL_DEBUG) Serial.println("setup: start A2DP sink");
  a2dpSink.start(btDeviceName.c_str());

  if (ENABLE_SERIAL_DEBUG) {
    Serial.printf("Bluetooth device name: %s\n", btDeviceName.c_str());
    Serial.printf("I2S pins -> LRCK:%d BCK:%d DATA:%d\n", I2S_LRCK_PIN, I2S_BCK_PIN, I2S_DATA_PIN);
    Serial.printf("Output cap: %u%%\n", static_cast<unsigned>(outputCapPercent));
    Serial.println("Ready. Commands: name=YourNewName | cap=0..100 | cap?");
    Serial.println("A2DP status will be logged on connect/disconnect events.");
  }
}

void loop() {
  handleSerialCommands();
  delay(2);
}
