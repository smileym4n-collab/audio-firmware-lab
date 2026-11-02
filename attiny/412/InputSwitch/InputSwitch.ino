/* InputSwitch, ATtiny412 SOIC-8, megaTinyCore, permanent +5 V supply.
   PA1 (pin 4): SOURCE_AV -> K1/K2/K4 + source LEDs. HIGH = AV.
   PA2 (pin 5): MUTE -> K3. HIGH = disconnect source and short amp inputs.
   PA3 (pin 7): AUDIO_PRESENT_N, external 47k/2.2u envelope. LOW = audio.
   PA6 (pin 2): P3A_ON -> external G7L coil driver. HIGH = power ON.
   PA7 (pin 3): TOGGLE_AV, external pull-down. HIGH = AV, LOW = Reference.
   PA0 (pin 6): UPDI ONLY. VDD pin 1, control GND pin 8.

   Select ATtiny412, 10 MHz internal, millis enabled, BOD 4.2 V enabled,
   reset pin UPDI, no bootloader. Burn Bootloader applies the fuse settings.
   Watchdog is enabled below (~1 s); leave watchdog fuse/window disabled.
   Timings and bench assumptions: input_switch_config.h and README.md.
*/
#include <Arduino.h>
#include <avr/io.h>
#include <avr/xmega.h>
#include "input_switch_fsm.h"

#if !defined(__AVR_ATtiny412__)
#error "This pin mapping and build are for ATtiny412 only."
#endif
#if F_CPU > 10000000UL
#error "Use <=10 MHz to retain CPU voltage margin below the 4.2 V BOD level."
#endif
#if defined(MILLIS_USE_TIMERNONE)
#error "Enable millis() in megaTinyCore."
#endif

static InputSwitch controller;
static uint32_t last_poll;

/* Called before Arduino core initialization. No timers or globals required.
   Reset itself still releases K3: hardware does NOT default to mute. */
void onBeforeInit() {
    PORTA.OUTSET = PIN2_bm;
    PORTA.OUTCLR = PIN1_bm | PIN6_bm;
    PORTA.DIRSET = PIN1_bm | PIN2_bm | PIN6_bm;
}

static void applyOutputs() {
    /* Mute first; source/power writes follow. Never reconfigure PA0/UPDI. */
    if (controller.out_mute) PORTA.OUTSET = PIN2_bm;
    if (controller.out_source_av) PORTA.OUTSET = PIN1_bm;
    else PORTA.OUTCLR = PIN1_bm;
    if (controller.out_power) PORTA.OUTSET = PIN6_bm;
    else PORTA.OUTCLR = PIN6_bm;
    if (!controller.out_mute) PORTA.OUTCLR = PIN2_bm;
}

void setup() {
    onBeforeInit();
    pinMode(PIN_PA7, INPUT);  // R2 pulls open/disconnected switch to Reference.
    pinMode(PIN_PA3, INPUT);  // R10 external pull-up; do not change the RC.
    last_poll = millis();
    input_switch_init(&controller, last_poll, digitalRead(PIN_PA7) == HIGH);
    applyOutputs();
    __asm__ __volatile__("wdr");
    while (WDT.STATUS & WDT_SYNCBUSY_bm) { }
    _PROTECTED_WRITE(WDT.CTRLA, WDT_PERIOD_1KCLK_gc);
    while (WDT.STATUS & WDT_SYNCBUSY_bm) { }
}

void loop() {
    const uint32_t now = millis();
    if (now == last_poll) return;
    last_poll = now;
    const uint8_t inputs = PORTA.IN;
    input_switch_update(&controller, now, !!(inputs & PIN7_bm),
                         !(inputs & PIN3_bm));
    applyOutputs();
    /* Service only after time advanced and control/output work completed.
       A stopped millis interrupt must also cause watchdog recovery. */
    __asm__ __volatile__("wdr");
}
