/*

// BTI2S
// Version: 0.9.0

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
#include <esp_adc_cal.h>
#include <driver/adc.h>
#include <driver/i2s.h>

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
#define BATTERY_ADC_PIN         34         // IO34 -> battery divider ADC input (input-only pin)

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

// ------------------------------
// Battery monitor configuration (easy-to-edit section)
// ------------------------------
#define BATTERY_R_TOP_OHMS      270000.0f
#define BATTERY_R_BOTTOM_OHMS    47000.0f
static constexpr uint8_t BATTERY_ADC_SAMPLES = 16;
static constexpr unsigned long BATTERY_POLL_INTERVAL_MS = 5000;
static constexpr float BATTERY_ADC_REF_VOLTAGE = 3.3f;       // Fallback scaling if ADC calibration is unavailable.
static constexpr float BATTERY_ADC_FULL_SCALE_COUNTS = 4095.0f;
static constexpr float BATTERY_PERCENT_SMOOTH_ALPHA = 0.20f; // Lower = steadier, slower updates.
static constexpr uint32_t BATTERY_ADC_DEFAULT_VREF_MV = 1100; // Used if eFuse calibration is unavailable.
static constexpr adc1_channel_t BATTERY_ADC1_CHANNEL = ADC1_CHANNEL_6; // GPIO34 -> ADC1_CH6

// BLE Battery Service support.
// Runtime reporting can be toggled from Serial using: blebat=on / blebat=off
static constexpr bool ENABLE_BLE_BATTERY_SERVICE = true;
static constexpr bool BLE_BATTERY_REPORT_DEFAULT_ENABLED = true;
static constexpr char BLE_BATTERY_NAME_SUFFIX[] = "-BAT";

// Enable serial logging and serial command interface.
// Set to false to reduce serial activity.
static constexpr bool ENABLE_SERIAL_DEBUG = true;
// Set to false to disable battery debug prints while leaving monitor active.
static constexpr bool ENABLE_BATTERY_DEBUG = true;
// Set false when no encoder is connected; avoids floating-input noise and unnecessary volume updates.
static constexpr bool ENABLE_ENCODER_CONTROLS = false;

#if ENABLE_BLE_BATTERY_SERVICE
#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#endif

typedef struct {
  float pack_voltage;
  int percent;
} battery_point_t;

// Estimated 4S Li-ion state-of-charge curve.
// Loaded battery voltage can sag under heavy bass / high output power.
// Table is intentionally easy to tune after real-world testing.
static const battery_point_t battery_curve_4s[] = {
    {16.80f, 100},
    {16.60f, 98},
    {16.40f, 95},
    {16.20f, 90},
    {16.00f, 85},
    {15.80f, 78},
    {15.60f, 70},
    {15.40f, 62},
    {15.20f, 54},
    {15.00f, 46},
    {14.80f, 38},
    {14.60f, 30},
    {14.40f, 22},
    {14.20f, 15},
    {14.00f, 10},
    {13.80f, 6},
    {13.60f, 3},
    {13.20f, 1},
    {12.00f, 0}
};

static constexpr size_t BATTERY_CURVE_POINTS = sizeof(battery_curve_4s) / sizeof(battery_curve_4s[0]);
static bool i2sInitialized = false;

static float gBatteryPinVoltage = 0.0f;
static float gBatteryPackVoltage = 0.0f;
static float gBatteryFilteredPercent = 0.0f;
static int gBatteryPercent = 0;
static uint32_t gBatteryRawAdcAverage = 0;
static bool gBatteryInitialized = false;
static unsigned long gLastBatteryPollMs = 0;
static bool gBleBatteryReportingEnabled = BLE_BATTERY_REPORT_DEFAULT_ENABLED;
static esp_adc_cal_characteristics_t gBatteryAdcCharacteristics;
static bool gBatteryAdcCalibrated = false;

#if ENABLE_BLE_BATTERY_SERVICE
static BLECharacteristic *gBatteryLevelCharacteristic = nullptr;
static BLECharacteristic *gBatteryPackVoltageTextCharacteristic = nullptr;
static BLECharacteristic *gBatteryPackPercentTextCharacteristic = nullptr;
static String gBleBatteryDeviceName;
static BLEServer *gBleBatteryServer = nullptr;
static BLEAdvertising *gBleBatteryAdvertising = nullptr;
static bool gBleBatteryAdvertisingActive = false;
static bool gBleBatteryClientConnected = false;
static constexpr char BLE_BATTERY_DIAG_SERVICE_UUID[] = "12345678-1234-5678-1234-56789abcdef0";
static constexpr char BLE_BATTERY_DIAG_VOLTAGE_CHAR_UUID[] = "12345678-1234-5678-1234-56789abcdef1";
static constexpr char BLE_BATTERY_DIAG_PERCENT_CHAR_UUID[] = "12345678-1234-5678-1234-56789abcdef2";
#endif

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

static float batteryReadPinVoltage() {
  uint32_t adcSum = 0;
  for (uint8_t i = 0; i < BATTERY_ADC_SAMPLES; ++i) {
    adcSum += static_cast<uint32_t>(adc1_get_raw(BATTERY_ADC1_CHANNEL));
    delay(2);
  }

  gBatteryRawAdcAverage = adcSum / BATTERY_ADC_SAMPLES;
  if (gBatteryAdcCalibrated) {
    const uint32_t pinMilliVolts = esp_adc_cal_raw_to_voltage(gBatteryRawAdcAverage, &gBatteryAdcCharacteristics);
    return static_cast<float>(pinMilliVolts) / 1000.0f;
  }

  return (static_cast<float>(gBatteryRawAdcAverage) / BATTERY_ADC_FULL_SCALE_COUNTS) * BATTERY_ADC_REF_VOLTAGE;
}

static float batteryPinToPackVoltage(float pinVoltage) {
  // Divider reconstruction:
  // Vpack = Vpin * (Rtop + Rbottom) / Rbottom
  // where Vpin is measured at BATTERY_ADC_PIN across Rbottom.
  const float dividerRatio = (BATTERY_R_TOP_OHMS + BATTERY_R_BOTTOM_OHMS) / BATTERY_R_BOTTOM_OHMS;
  return pinVoltage * dividerRatio;
}

// Lookup + interpolation helper.
// Input: measured pack voltage. Output: clamped 0..100% estimate.
static int batteryPercentFromVoltage(float packVoltage) {
  if (packVoltage >= battery_curve_4s[0].pack_voltage) {
    return 100;
  }
  if (packVoltage <= battery_curve_4s[BATTERY_CURVE_POINTS - 1].pack_voltage) {
    return 0;
  }

  for (size_t i = 0; i < (BATTERY_CURVE_POINTS - 1); ++i) {
    const battery_point_t &high = battery_curve_4s[i];
    const battery_point_t &low = battery_curve_4s[i + 1];

    if (packVoltage <= high.pack_voltage && packVoltage >= low.pack_voltage) {
      const float spanV = high.pack_voltage - low.pack_voltage;
      if (spanV <= 0.0f) {
        return constrain(high.percent, 0, 100);
      }
      const float t = (packVoltage - low.pack_voltage) / spanV;
      const float interpolated = static_cast<float>(low.percent) + t * static_cast<float>(high.percent - low.percent);
      return constrain(static_cast<int>(interpolated + 0.5f), 0, 100);
    }
  }

  return 0;
}

#if ENABLE_BLE_BATTERY_SERVICE
class BatteryBleServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer * /*server*/) override {
    gBleBatteryClientConnected = true;
    if (ENABLE_SERIAL_DEBUG) {
      Serial.println("BLE battery: client connected");
    }
  }

  void onDisconnect(BLEServer *server) override {
    gBleBatteryClientConnected = false;
    if (ENABLE_SERIAL_DEBUG) {
      Serial.println("BLE battery: client disconnected");
    }
    if (gBleBatteryReportingEnabled && server != nullptr) {
      server->getAdvertising()->start();
      gBleBatteryAdvertisingActive = true;
      if (ENABLE_SERIAL_DEBUG) {
        Serial.println("BLE battery: advertising resumed");
      }
    }
  }
};

static BatteryBleServerCallbacks gBatteryBleServerCallbacks;

static void batteryBleSetAdvertising(bool enabled) {
  if (gBleBatteryAdvertising == nullptr) {
    return;
  }

  if (enabled) {
    gBleBatteryAdvertising->start();
    gBleBatteryAdvertisingActive = true;
  } else {
    gBleBatteryAdvertising->stop();
    gBleBatteryAdvertisingActive = false;
  }
}

static void batteryBleInit(const String &baseName) {
  // NOTE:
  // A2DP audio and BLE are separate subsystems on ESP32.
  // This optional BLE Battery Service is diagnostic-only and may appear as a
  // separate peripheral identity (<BT_NAME>-BAT) in scanner apps.
  // iPhone system battery UI integration for Bluetooth audio accessories
  // is not guaranteed by a generic BLE 0x180F Battery Service.
  gBleBatteryDeviceName = baseName;
  gBleBatteryDeviceName += BLE_BATTERY_NAME_SUFFIX;
  BLEDevice::init(gBleBatteryDeviceName.c_str());
  gBleBatteryServer = BLEDevice::createServer();
  gBleBatteryServer->setCallbacks(&gBatteryBleServerCallbacks);
  BLEService *batteryService = gBleBatteryServer->createService(BLEUUID((uint16_t)0x180F));
  gBatteryLevelCharacteristic = batteryService->createCharacteristic(
      BLEUUID((uint16_t)0x2A19), BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  gBatteryLevelCharacteristic->addDescriptor(new BLE2902());

  uint8_t initial = 0;
  gBatteryLevelCharacteristic->setValue(&initial, 1);
  batteryService->start();

  BLEService *diagService = gBleBatteryServer->createService(BLEUUID(BLE_BATTERY_DIAG_SERVICE_UUID));
  gBatteryPackVoltageTextCharacteristic =
      diagService->createCharacteristic(BLEUUID(BLE_BATTERY_DIAG_VOLTAGE_CHAR_UUID), BLECharacteristic::PROPERTY_READ);
  gBatteryPackPercentTextCharacteristic =
      diagService->createCharacteristic(BLEUUID(BLE_BATTERY_DIAG_PERCENT_CHAR_UUID), BLECharacteristic::PROPERTY_READ);
  gBatteryPackVoltageTextCharacteristic->setValue("0.00");
  gBatteryPackPercentTextCharacteristic->setValue("0");
  diagService->start();

  gBleBatteryAdvertising = BLEDevice::getAdvertising();
  gBleBatteryAdvertising->addServiceUUID(BLEUUID((uint16_t)0x180F));
  gBleBatteryAdvertising->addServiceUUID(BLEUUID(BLE_BATTERY_DIAG_SERVICE_UUID));
  gBleBatteryAdvertising->setScanResponse(true);
  gBleBatteryAdvertising->setMinPreferred(0x06);  // iOS-friendly connection parameter hint
  gBleBatteryAdvertising->setMinPreferred(0x12);
  batteryBleSetAdvertising(gBleBatteryReportingEnabled);
}

static void batteryBleUpdatePercent(uint8_t percent) {
  if (gBatteryLevelCharacteristic == nullptr) {
    return;
  }
  gBatteryLevelCharacteristic->setValue(&percent, 1);
  gBatteryLevelCharacteristic->notify();
}

static void batteryBleUpdateDiagnostics(float packVoltage, int percent) {
  if (gBatteryPackVoltageTextCharacteristic != nullptr) {
    char voltageText[12];
    snprintf(voltageText, sizeof(voltageText), "%.2f", packVoltage);
    gBatteryPackVoltageTextCharacteristic->setValue(voltageText);
  }
  if (gBatteryPackPercentTextCharacteristic != nullptr) {
    char percentText[6];
    snprintf(percentText, sizeof(percentText), "%d", percent);
    gBatteryPackPercentTextCharacteristic->setValue(percentText);
  }
}
#endif
static void batteryInit() {
  pinMode(BATTERY_ADC_PIN, INPUT);
  adc1_config_width(ADC_WIDTH_BIT_12);
  adc1_config_channel_atten(BATTERY_ADC1_CHANNEL, ADC_ATTEN_DB_11);
  esp_adc_cal_value_t calType = esp_adc_cal_characterize(
      ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, BATTERY_ADC_DEFAULT_VREF_MV, &gBatteryAdcCharacteristics);
  gBatteryAdcCalibrated = (calType != ESP_ADC_CAL_VAL_NOT_SUPPORTED);
  gLastBatteryPollMs = 0;

#if ENABLE_BLE_BATTERY_SERVICE
  batteryBleInit(btDeviceName);
#endif
}

static void batteryPoll() {
  const unsigned long nowMs = millis();
  if (gBatteryInitialized && (nowMs - gLastBatteryPollMs) < BATTERY_POLL_INTERVAL_MS) {
    return;
  }
  gLastBatteryPollMs = nowMs;

  gBatteryPinVoltage = batteryReadPinVoltage();
  gBatteryPackVoltage = batteryPinToPackVoltage(gBatteryPinVoltage);
  const int instantPercent = batteryPercentFromVoltage(gBatteryPackVoltage);

  if (!gBatteryInitialized) {
    gBatteryFilteredPercent = static_cast<float>(instantPercent);
    gBatteryInitialized = true;
  } else {
    gBatteryFilteredPercent +=
        BATTERY_PERCENT_SMOOTH_ALPHA * (static_cast<float>(instantPercent) - gBatteryFilteredPercent);
  }

  gBatteryPercent = constrain(static_cast<int>(gBatteryFilteredPercent + 0.5f), 0, 100);

#if ENABLE_BLE_BATTERY_SERVICE
  if (gBleBatteryReportingEnabled) {
    batteryBleUpdatePercent(static_cast<uint8_t>(gBatteryPercent));
  }
  batteryBleUpdateDiagnostics(gBatteryPackVoltage, gBatteryPercent);
#endif

  if (ENABLE_SERIAL_DEBUG && ENABLE_BATTERY_DEBUG) {
    Serial.printf("BAT pin=%.3fV pack=%.2fV soc=%d%% cal=%s\n",
                  gBatteryPinVoltage,
                  gBatteryPackVoltage,
                  gBatteryPercent,
                  gBatteryAdcCalibrated ? "Y" : "N");
  }
}

static float batteryGetVoltage() {
  return gBatteryPackVoltage;
}

static int batteryGetPercent() {
  return gBatteryPercent;
}

#if ENABLE_BLE_BATTERY_SERVICE
static bool parseOnOffValue(const String &text, bool &outValue) {
  if (text.equalsIgnoreCase("on") || text.equalsIgnoreCase("1") || text.equalsIgnoreCase("true")) {
    outValue = true;
    return true;
  }
  if (text.equalsIgnoreCase("off") || text.equalsIgnoreCase("0") || text.equalsIgnoreCase("false")) {
    outValue = false;
    return true;
  }
  return false;
}
#endif

static void printBatteryStatus() {
  Serial.printf("BAT raw=%lu pin=%.3fV pack=%.2fV soc=%d%%",
                static_cast<unsigned long>(gBatteryRawAdcAverage),
                gBatteryPinVoltage,
                gBatteryPackVoltage,
                gBatteryPercent);
#if ENABLE_BLE_BATTERY_SERVICE
  Serial.printf(" ble_adv=%s ble_client=%s ble_report=%s",
                gBleBatteryAdvertisingActive ? "ON" : "OFF",
                gBleBatteryClientConnected ? "YES" : "NO",
                gBleBatteryReportingEnabled ? "ON" : "OFF");
#endif
  Serial.println();
}

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

static void i2sAudioDataCallback(const uint8_t *data, uint32_t len) {
  if (!i2sInitialized || data == nullptr || len == 0) {
    return;
  }
  size_t bytesWritten = 0;
  i2s_write(I2S_PORT, data, len, &bytesWritten, portMAX_DELAY);
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

#if ENABLE_BLE_BATTERY_SERVICE
  if (line.startsWith("blebat=")) {
    String value = line.substring(7);
    value.trim();
    bool newState = gBleBatteryReportingEnabled;
    if (!parseOnOffValue(value, newState)) {
      Serial.println("Invalid blebat value. Use: blebat=on|off");
      return;
    }

    gBleBatteryReportingEnabled = newState;
    if (gBleBatteryReportingEnabled) {
      batteryBleSetAdvertising(true);
      batteryBleUpdatePercent(static_cast<uint8_t>(batteryGetPercent()));
      batteryBleUpdateDiagnostics(gBatteryPackVoltage, gBatteryPercent);
    } else {
      batteryBleSetAdvertising(false);
    }
    Serial.printf("BLE battery reporting: %s\n", gBleBatteryReportingEnabled ? "ON" : "OFF");
    return;
  }

  if (line.equalsIgnoreCase("blebat?")) {
    Serial.printf("BLE BAT service=ENABLED name=%s adv=%s client=%s report=%s\n",
                  gBleBatteryDeviceName.c_str(),
                  gBleBatteryAdvertisingActive ? "ON" : "OFF",
                  gBleBatteryClientConnected ? "CONNECTED" : "DISCONNECTED",
                  gBleBatteryReportingEnabled ? "ON" : "OFF");
    return;
  }
#endif

  if (line.equalsIgnoreCase("bat?")) {
    printBatteryStatus();
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

  Serial.println("Unknown command. Use: name=YourNewName | vol=0..100 | bat? | blebat? | blebat=on|off");
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

  if (ENABLE_SERIAL_DEBUG) Serial.println("setup: init battery monitor");
  batteryInit();
  batteryPoll();

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
  if (ENABLE_SERIAL_DEBUG) Serial.println("setup: start A2DP sink");
  a2dpSink.start(btDeviceName.c_str());
  applyOutputVolume();

  if (ENABLE_SERIAL_DEBUG) {
    Serial.printf("Bluetooth device name: %s\n", btDeviceName.c_str());
    Serial.printf("I2S pins -> LRCK:%d BCK:%d DATA:%d\n", I2S_LRCK_PIN, I2S_BCK_PIN, I2S_DATA_PIN);
    Serial.printf("Encoder pins -> SW:%d A:%d B:%d\n", ENC_SW_PIN, ENC_A_PIN, ENC_B_PIN);
    Serial.printf("Battery ADC -> PIN:%d Rtop:%.0f Rbottom:%.0f\n", BATTERY_ADC_PIN, BATTERY_R_TOP_OHMS, BATTERY_R_BOTTOM_OHMS);
    Serial.printf("Battery startup -> pack=%.2fV soc=%d%% (first sample)\n", batteryGetVoltage(), batteryGetPercent());
#if ENABLE_BLE_BATTERY_SERVICE
    Serial.printf("Battery BLE service enabled (0x180F/0x2A19), reporting %s. Toggle: blebat=on|off\n",
                  gBleBatteryReportingEnabled ? "ON" : "OFF");
    Serial.printf("Battery BLE name: %s\n", gBleBatteryDeviceName.c_str());
    Serial.println("Note: iPhone may not show this generic BLE battery in system BT battery UI.");
    Serial.println("      A2DP audio + BLE battery can appear as separate functions/devices.");
#else
    Serial.println("Battery BLE service disabled to avoid changing BT audio behavior.");
#endif
    Serial.println("Ready. Commands: name=YourNewName | vol=0..100 | bat? | blebat? | blebat=on|off");
    Serial.println("A2DP status will be logged on connect/disconnect events.");
  }
}

void loop() {
  if (ENABLE_ENCODER_CONTROLS) {
    handleEncoderControls();
  }
  batteryPoll();
  handleSerialCommands();
  delay(2);
}
