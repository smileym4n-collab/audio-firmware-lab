// NJW1195A bench test firmware - ESP32-S3
//
// Input selection and volume control of an NJW1195A over the serial console.
// Test firmware only: the final control firmware is intended for an ATtiny.
//
// NJW1195A control (datasheet Ver 0.4E):
//   16-bit word, MSB first: D15..D8 data, D7..D4 select address, D3..D0 chip address
//   Data sampled on CLOCK FALLING edge (timing diagram, p.11), word latched
//   on LATCH rising edge. DATA is changed while CLOCK is high.
//   CLOCK held high when idle (datasheet note).
//   Logic VIH min 2.5 V / VIL max 1.5 V, so 3.3 V GPIO drives it directly.
//   Chip powers up with all volumes and selectors muted.
//
// Assumptions:
//   - DIFF (pin 22) tied low: 4-input single-ended mode.
//   - ADR0/ADR1 (pins 19/20) tied low: chip address 0.
//   - ESP32 GND connected to the NJW1195A logic/audio ground (0 V).

#include <Arduino.h>

// ---- GPIO (ESP32-S3 DevKitC-1) ----
// GPIO4-6 are plain I/O: not strapping, USB, UART0, flash or PSRAM pins.
const uint8_t PIN_DATA  = 4;  // -> NJW1195A pin 14 DATA
const uint8_t PIN_CLOCK = 5;  // -> NJW1195A pin 15 CLOCK
const uint8_t PIN_LATCH = 6;  // -> NJW1195A pin 16 LATCH

const uint8_t CHIP_ADDR = 0;  // ADR1:ADR0 = Low:Low

// Bit-bang delay. Datasheet minimums are 1.6-4 us; 20 us tolerates long jumpers.
const uint8_t BIT_DELAY_US = 20;

// Select addresses (D7..D4), single-ended mode
const uint8_t REG_VOL_1A = 0x0;  // Out1A volume
const uint8_t REG_VOL_1B = 0x1;  // Out1B volume
const uint8_t REG_VOL_2A = 0x2;  // Out2A volume
const uint8_t REG_VOL_2B = 0x3;  // Out2B volume
const uint8_t REG_SEL_A  = 0x4;  // D15..D13 = SEL1A, D12..D10 = SEL2A
const uint8_t REG_SEL_B  = 0x5;  // D15..D13 = SEL1B, D12..D10 = SEL2B

// Volume data: 0x01 = +31.5 dB ... 0x40 = 0 dB ... 0xFE = -95 dB, 0x00 = mute
const uint8_t VOL_MUTE = 0x00;
const int16_t VOL_MAX_HALF_DB = 63;    // +31.5 dB
const int16_t VOL_MIN_HALF_DB = -190;  // -95.0 dB

// ---- State ----
int16_t volHalfDb = -60;  // -30 dB, applied when unmuted
bool    muted     = true; // start muted (safe)
uint8_t input1    = 1;    // selector 1 -> Out1 (0 = mute, 1..4)
uint8_t input2    = 0;    // selector 2 -> Out2 (0 = mute, 1..4)

// Input scan: steps through all 8 input pins (InA1-4, then InB1-4)
bool     scanning    = false;
uint8_t  scanStep    = 0;     // 0..3 = InA1..4, 4..7 = InB1..4
uint32_t scanDwellMs = 3000;
uint32_t scanLastMs  = 0;
bool     txLog       = true;  // print each word sent (off during scan)

char    lineBuf[32];
uint8_t lineLen = 0;

// ---- NJW1195A low level ----

void njwWriteWord(uint16_t word) {
  digitalWrite(PIN_LATCH, LOW);
  delayMicroseconds(BIT_DELAY_US);
  for (int8_t bit = 15; bit >= 0; bit--) {
    digitalWrite(PIN_DATA, (word >> bit) & 1);  // change while CLOCK high
    delayMicroseconds(BIT_DELAY_US);
    digitalWrite(PIN_CLOCK, LOW);   // data sampled on falling edge
    delayMicroseconds(BIT_DELAY_US);
    digitalWrite(PIN_CLOCK, HIGH);
    delayMicroseconds(BIT_DELAY_US);
  }
  digitalWrite(PIN_LATCH, HIGH);    // latch on rising edge
  delayMicroseconds(BIT_DELAY_US);
  digitalWrite(PIN_DATA, LOW);

  if (txLog) Serial.printf("  tx 0x%04X\n", word);
}

void njwWriteAddr(uint8_t data, uint8_t reg, uint8_t addr) {
  njwWriteWord(((uint16_t)data << 8) | ((reg & 0x0F) << 4) | (addr & 0x0F));
}

void njwWrite(uint8_t data, uint8_t reg) {
  njwWriteAddr(data, reg, CHIP_ADDR);
}

uint8_t volumeCode() {
  if (muted) return VOL_MUTE;
  return (uint8_t)(64 - volHalfDb);  // 0 dB = 0x40, 0.5 dB per step
}

void sendVolume() {
  uint8_t code = volumeCode();
  njwWrite(code, REG_VOL_1A);
  njwWrite(code, REG_VOL_1B);
  njwWrite(code, REG_VOL_2A);
  njwWrite(code, REG_VOL_2B);
}

void sendSelectors() {
  uint8_t data = (input1 << 5) | (input2 << 2);  // SEL1 in D15..D13, SEL2 in D12..D10
  njwWrite(data, REG_SEL_A);
  njwWrite(data, REG_SEL_B);
}

// ---- Input scan ----

// NJW1195A package pins for InA1..4 / InB1..4
const uint8_t PIN_IN_A[4] = {1, 2, 3, 4};
const uint8_t PIN_IN_B[4] = {32, 31, 30, 29};

// Select one input pin on one channel (other channel muted), max volume.
// Sent to all 4 chip addresses in case ADR0/ADR1 strapping is unknown.
void scanApply() {
  uint8_t n    = (scanStep % 4) + 1;
  bool    chB  = scanStep >= 4;
  uint8_t sel  = (n << 5) | (n << 2);  // SEL1 and SEL2 both on input n
  uint8_t vmax = (uint8_t)(64 - VOL_MAX_HALF_DB);

  for (uint8_t addr = 0; addr < 4; addr++) {
    njwWriteAddr(vmax, REG_VOL_1A, addr);
    njwWriteAddr(vmax, REG_VOL_1B, addr);
    njwWriteAddr(vmax, REG_VOL_2A, addr);
    njwWriteAddr(vmax, REG_VOL_2B, addr);
    njwWriteAddr(chB ? 0 : sel, REG_SEL_A, addr);
    njwWriteAddr(chB ? sel : 0, REG_SEL_B, addr);
  }

  if (chB) {
    Serial.printf("[scan %u/8] InB%u (pin %u) -> Out1B pin 25, Out2B pin 21\n",
                  scanStep + 1, n, PIN_IN_B[n - 1]);
  } else {
    Serial.printf("[scan %u/8] InA%u (pin %u) -> Out1A pin 8, Out2A pin 12\n",
                  scanStep + 1, n, PIN_IN_A[n - 1]);
  }
}

void scanStart(uint32_t dwellSec) {
  if (dwellSec > 0) scanDwellMs = dwellSec * 1000;
  scanning   = true;
  scanStep   = 0;
  txLog      = false;
  Serial.printf("Scanning 8 inputs at +31.5 dB, %lu s each, all chip addresses.\n",
                (unsigned long)(scanDwellMs / 1000));
  Serial.println("Press Enter to stop and hold the current input.");
  scanApply();
  scanLastMs = millis();
}

void scanStop() {
  scanning  = false;
  txLog     = true;
  volHalfDb = VOL_MAX_HALF_DB;
  muted     = false;
  input1    = input2 = (scanStep % 4) + 1;
  Serial.printf("Scan stopped, holding step %u/8: In%c%u (pin %u)\n",
                scanStep + 1, scanStep >= 4 ? 'B' : 'A', input1,
                scanStep >= 4 ? PIN_IN_B[input1 - 1] : PIN_IN_A[input1 - 1]);
  Serial.println("Other channel still muted; any input/volume command resumes normal control.");
}

// ---- Console ----

void printStatus() {
  Serial.printf("Input Out1: %u  Out2: %u  (0 = mute)\n", input1, input2);
  Serial.printf("Volume: %.1f dB%s (code 0x%02X)\n",
                volHalfDb / 2.0f, muted ? " [MUTED]" : "", volumeCode());
}

void printHelp() {
  Serial.println();
  Serial.println("NJW1195A test - commands:");
  Serial.println("  1..4      select input for Out1 (same as i <n>)");
  Serial.println("  i <0-4>   input for Out1 (0 = mute)");
  Serial.println("  i2 <0-4>  input for Out2 (0 = mute)");
  Serial.println("  v <dB>    volume, -95 to +31.5 in 0.5 dB steps (e.g. v -20.5)");
  Serial.println("  + / -     volume up / down 1 dB");
  Serial.println("  m         mute toggle");
  Serial.println("  w         re-send all registers");
  Serial.println("  r <hex>   send raw 16-bit word, e.g. r 4040");
  Serial.println("  scan [s]  cycle all 8 input pins at max volume, s seconds each (default 3)");
  Serial.println("  p <0|1>   hold DATA/CLOCK/LATCH all low or all high (wiring check)");
  Serial.println("  s         status");
  Serial.println("  h         help");
  Serial.println();
}

void setVolume(int16_t halfDb) {
  volHalfDb = constrain(halfDb, VOL_MIN_HALF_DB, VOL_MAX_HALF_DB);
  sendVolume();
  printStatus();
}

void setInput(uint8_t *sel, long n) {
  if (n < 0 || n > 4) {
    Serial.println("Input must be 0-4");
    return;
  }
  *sel = (uint8_t)n;
  sendSelectors();
  printStatus();
}

void handleLine(char *line) {
  while (*line == ' ') line++;
  if (*line == '\0') return;

  char *arg = line + 1;
  if (line[0] == 'i' && line[1] == '2') arg = line + 2;

  if (line[0] >= '1' && line[0] <= '4' && line[1] == '\0') {
    setInput(&input1, line[0] - '0');
  } else if (line[0] == 'i' && line[1] == '2') {
    setInput(&input2, strtol(arg, nullptr, 10));
  } else if (line[0] == 'i') {
    setInput(&input1, strtol(arg, nullptr, 10));
  } else if (line[0] == 'v') {
    float db = strtof(arg, nullptr);
    setVolume((int16_t)lroundf(db * 2.0f));
  } else if (line[0] == '+') {
    setVolume(volHalfDb + 2);
  } else if (line[0] == '-') {
    setVolume(volHalfDb - 2);
  } else if (line[0] == 'm') {
    muted = !muted;
    sendVolume();
    printStatus();
  } else if (line[0] == 'w') {
    sendSelectors();
    sendVolume();
    printStatus();
  } else if (line[0] == 'r') {
    njwWriteWord((uint16_t)strtoul(arg, nullptr, 16));
  } else if (line[0] == 'p') {
    // Static level for metering at the chip pins; next transfer restores idle
    uint8_t level = strtol(arg, nullptr, 10) ? HIGH : LOW;
    digitalWrite(PIN_DATA, level);
    digitalWrite(PIN_CLOCK, level);
    digitalWrite(PIN_LATCH, level);
    Serial.printf("DATA/CLOCK/LATCH held %s - measure chip pins 14/15/16 to GND\n",
                  level ? "HIGH (~3.3 V)" : "LOW (~0 V)");
  } else if (strncmp(line, "scan", 4) == 0) {
    scanStart(strtoul(line + 4, nullptr, 10));
  } else if (line[0] == 's') {
    printStatus();
  } else if (line[0] == 'h' || line[0] == '?') {
    printHelp();
  } else {
    Serial.println("Unknown command, 'h' for help");
  }
}

void setup() {
  // Safe idle levels: CLOCK and LATCH high, DATA low
  pinMode(PIN_CLOCK, OUTPUT);
  digitalWrite(PIN_CLOCK, HIGH);
  pinMode(PIN_LATCH, OUTPUT);
  digitalWrite(PIN_LATCH, HIGH);
  pinMode(PIN_DATA, OUTPUT);
  digitalWrite(PIN_DATA, LOW);

  Serial.begin(115200);
  delay(1500);  // allow USB-CDC to enumerate so the banner is seen

  Serial.println();
  Serial.println("NJW1195A test firmware (ESP32-S3)");
  Serial.printf("DATA=GPIO%u CLOCK=GPIO%u LATCH=GPIO%u chip addr=%u\n",
                PIN_DATA, PIN_CLOCK, PIN_LATCH, CHIP_ADDR);

  // Volume muted first, then selectors
  sendVolume();
  sendSelectors();
  printHelp();
  printStatus();
  Serial.println("Volume is muted - send 'm' to unmute");
}

void loop() {
  if (scanning && millis() - scanLastMs >= scanDwellMs) {
    scanStep = (scanStep + 1) % 8;
    scanApply();
    scanLastMs = millis();
  }

  static char prev = 0;
  while (Serial.available()) {
    char c = Serial.read();
    bool crlf = (c == '\n' && prev == '\r');  // CR+LF counts as one Enter
    prev = c;
    if (crlf) continue;
    if (scanning) {
      // Any key stops the scan; the rest of the line is discarded
      scanStop();
      delay(50);
      while (Serial.available()) Serial.read();
      lineLen = 0;
      break;
    }
    if (c == '\r' || c == '\n') {
      lineBuf[lineLen] = '\0';
      handleLine(lineBuf);
      lineLen = 0;
    } else if (lineLen < sizeof(lineBuf) - 1) {
      lineBuf[lineLen++] = c;
    }
  }
}
