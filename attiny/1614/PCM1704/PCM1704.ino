/*
  Project: PCM1704 DAC (Reference Series DAC-3) DF1704 configuration
  Version: v1.0.0
  Target:  ATtiny1614 (megaTinyCore), U7 on the PCM1704 board

  Pin map (PCM1704.kicad_sch, U7)
  -------------------------------
  PA4 (pin 2)  -> DF_RST, DF1704 pin 14 (R75 10k pull-down: DF1704 held in reset until released)
  PA3 (pin 13) -> ML, DF1704 pin 13, control latch (R76 10k pull-up: idles HIGH)
  PA2 (pin 12) -> MC, DF1704 pin 12, control clock (R77 47k pull-down: idles LOW)
  PA1 (pin 11) -> MD, DF1704 pin 11, control data  (R78 47k pull-down: idles LOW)
  PA0 (pin 10) -> UPDI, J3 pin 2 (programming only)
  PA5-PA7, PB0-PB3 -> not connected (internal pull-ups enabled)

  Behaviour
  ---------
  The DF1704 MODE pin is tied HIGH (software mode). Its reset defaults are
  16-bit input and 16-bit output, so it must be configured for the 24-bit I2S
  source and the 24-bit PCM1704:

  1. Hold DF_RST low while the rails and MCLK settle.
  2. Release DF_RST and wait for the DF1704 internal initialisation.
  3. Write MODE0..MODE3.
  4. Wait, then write MODE0..MODE3 again (insurance; the MCU cannot see MCLK).
  5. Enter power-down sleep with all pins held, so no MCU clock runs during
     playback (DESIGN_REVIEW M6). Nothing wakes the MCU.

  Notes
  -----
  - DF1704 timing and registers from TI SBAS099A, Figures 8-10 and Table V.
  - Assumes the XMOS MCLK is running whenever this board is powered.
  - Brown-out detection should be enabled in the Tools menu so a supply dip
    re-runs the whole sequence (see README.md).
*/

#include <avr/io.h>
#include <avr/sleep.h>
#include <util/delay.h>

// DF1704 control pins, all on PORTA
const uint8_t DF_RST_bm = PIN4_bm; // DF1704 RST, LOW = reset
const uint8_t DF_ML_bm  = PIN3_bm; // Latch, idles HIGH, LOW pulse after B0
const uint8_t DF_MC_bm  = PIN2_bm; // Clock, MD sampled on rising edge
const uint8_t DF_MD_bm  = PIN1_bm; // Data, MSB first

// DF1704 register words: B10..B9 = address, B8..B0 = data (reserved bits 0)
const uint16_t DF_MODE0 = 0x01FF; // LDL = 1, AL = 0xFF: left attenuation 0 dB
const uint16_t DF_MODE1 = 0x03FF; // LDR = 1, AR = 0xFF: right attenuation 0 dB
const uint16_t DF_MODE2 = 0x0469; // OW = 11 24-bit out, IW = 01 (24-bit I2S with I2S = 1), DEM off, MUT = 1 mute off
const uint16_t DF_MODE3 = 0x0601; // SF = 44.1k (unused), CKO = XTI, SRO = sharp, ATC = independent, LRP = 0, I2S = 1
// If left and right come out swapped on the bench, set LRP: DF_MODE3 = 0x0603.

// Startup sequencing
const uint16_t RAIL_SETTLE_MS  = 500;  // Rails, DF1704 power-on reset and MCLK settle with DF_RST held low
const uint16_t DF_INIT_MS      = 10;   // DF1704 needs 1024 XTI clocks after RST rises (about 42 us)
const uint16_t REWRITE_DELAY_MS = 1000; // Second register write as insurance

// Bit-bang half period. The DF1704 needs >= 40 ns setup/hold and a 100 ns MC cycle;
// 2 us is ample at any clock and still only takes ~70 us per word.
#define DF_BIT_DELAY_US 2

static void dfWriteWord(uint16_t word) {
  for (uint8_t i = 0; i < 16; i++) {
    PORTA.OUTCLR = DF_MC_bm;
    if (word & 0x8000) {
      PORTA.OUTSET = DF_MD_bm;
    } else {
      PORTA.OUTCLR = DF_MD_bm;
    }
    _delay_us(DF_BIT_DELAY_US);
    PORTA.OUTSET = DF_MC_bm; // DF1704 samples MD here
    _delay_us(DF_BIT_DELAY_US);
    word <<= 1;
  }

  // ML low pulse after B0; the word is latched on the ML rising edge
  PORTA.OUTCLR = DF_ML_bm;
  _delay_us(DF_BIT_DELAY_US);
  PORTA.OUTSET = DF_ML_bm;
  _delay_us(DF_BIT_DELAY_US);

  // Return MC and MD to their idle (pull-down) state
  PORTA.OUTCLR = DF_MC_bm | DF_MD_bm;
  _delay_us(DF_BIT_DELAY_US);
}

static void dfWriteAll() {
  dfWriteWord(DF_MODE0);
  dfWriteWord(DF_MODE1);
  dfWriteWord(DF_MODE2);
  dfWriteWord(DF_MODE3);
}

void setup() {
  // Safe states first, matching the external resistors, then enable outputs:
  // DF_RST LOW (DF1704 in reset), ML HIGH, MC/MD LOW.
  PORTA.OUTCLR = DF_RST_bm | DF_MC_bm | DF_MD_bm;
  PORTA.OUTSET = DF_ML_bm;
  PORTA.DIRSET = DF_RST_bm | DF_ML_bm | DF_MC_bm | DF_MD_bm;

  // Unused pins: pull-ups so they do not float
  PORTA.PIN5CTRL = PORT_PULLUPEN_bm;
  PORTA.PIN6CTRL = PORT_PULLUPEN_bm;
  PORTA.PIN7CTRL = PORT_PULLUPEN_bm;
  PORTB.PIN0CTRL = PORT_PULLUPEN_bm;
  PORTB.PIN1CTRL = PORT_PULLUPEN_bm;
  PORTB.PIN2CTRL = PORT_PULLUPEN_bm;
  PORTB.PIN3CTRL = PORT_PULLUPEN_bm;

  _delay_ms(RAIL_SETTLE_MS);

  PORTA.OUTSET = DF_RST_bm; // Release DF1704 reset
  _delay_ms(DF_INIT_MS);

  dfWriteAll();
  _delay_ms(REWRITE_DELAY_MS);
  dfWriteAll();

  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  sleep_enable();
}

void loop() {
  // Power-down keeps all pin states. No wake source is enabled; if anything
  // does wake the CPU, go straight back to sleep.
  sleep_cpu();
}
