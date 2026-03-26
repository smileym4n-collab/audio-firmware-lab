/*
  Project: BTI2S UART smoke test
  Target: ESP32-WROOM-32UE (Arduino framework)

  Purpose:
  - Verify basic UART wiring and Serial Monitor communication using only FT232.
  - Print periodic heartbeat messages.
  - Echo received bytes so TX/RX direction can be confirmed.

  Wiring for FT232 (3.3V logic):
  - FT232 TX -> ESP32 U0RXD (GPIO3)
  - FT232 RX -> ESP32 U0TXD (GPIO1)
  - FT232 GND -> ESP32 GND
  - FT232 3.3V -> ESP32 3.3V
*/

#include <Arduino.h>

// ------------------------------
// Pin map (UART0 defaults)
// ------------------------------
static constexpr int UART0_TX_PIN = 1;  // GPIO1 -> U0TXD
static constexpr int UART0_RX_PIN = 3;  // GPIO3 -> U0RXD

// Optional heartbeat LED (set to -1 if your board has no LED wired)
static constexpr int HEARTBEAT_LED_PIN = 2;  // Common ESP32 dev LED pin

static constexpr uint32_t UART_BAUD = 115200;
static constexpr uint32_t HEARTBEAT_MS = 1000;

uint32_t lastHeartbeatMs = 0;
uint32_t heartbeatCount = 0;

void setup() {
  pinMode(HEARTBEAT_LED_PIN, OUTPUT);
  digitalWrite(HEARTBEAT_LED_PIN, LOW);

  Serial.begin(UART_BAUD);
  delay(150);

  Serial.println();
  Serial.println("=== ESP32 UART smoke test ===");
  Serial.printf("UART0 pins TX=%d RX=%d baud=%lu\n", UART0_TX_PIN, UART0_RX_PIN, static_cast<unsigned long>(UART_BAUD));
  Serial.println("Type any characters and press Enter; bytes will echo.");
  Serial.println("If you see this text, serial output path is working.");
}

void loop() {
  const uint32_t now = millis();

  if (now - lastHeartbeatMs >= HEARTBEAT_MS) {
    lastHeartbeatMs = now;
    heartbeatCount++;

    digitalWrite(HEARTBEAT_LED_PIN, !digitalRead(HEARTBEAT_LED_PIN));
    Serial.printf("heartbeat=%lu uptime_ms=%lu\n",
                  static_cast<unsigned long>(heartbeatCount),
                  static_cast<unsigned long>(now));
  }

  while (Serial.available() > 0) {
    const int inByte = Serial.read();
    Serial.printf("rx_dec=%d rx_hex=0x%02X char='%c'\n",
                  inByte,
                  static_cast<unsigned>(inByte & 0xFF),
                  (inByte >= 32 && inByte <= 126) ? static_cast<char>(inByte) : '.');
  }
}
