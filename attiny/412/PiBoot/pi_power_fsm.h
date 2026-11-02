/*
  pi_power_fsm.h - hardware-independent power controller state machine.

  The sketch reads the pins, converts them to logical levels, calls
  pi_fsm_update() and writes the three logical outputs back to the pins.
  No Arduino calls here, so the same code runs in the host tests.
*/
#ifndef PI_POWER_FSM_H
#define PI_POWER_FSM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  ST_STANDBY = 0,       /* Pi unpowered, waiting for button             */
  ST_BOOTING,           /* Pi powered, shutdown detection not yet armed  */
  ST_RUNNING,           /* Pi powered, detection armed                   */
  ST_SHUTTING_DOWN,     /* Shutdown requested, waiting for SAFE_TO_OFF   */
  ST_SHUTDOWN_TIMEOUT,  /* No completion seen, power kept ON             */
  ST_POWER_OFF_SETTLE   /* Completion accepted, short wait before off    */
} PiState;

/* Simple debouncer: output changes only after raw input is stable. */
typedef struct {
  uint8_t  stable;      /* Debounced level (0/1)          */
  uint8_t  raw;         /* Last raw sample                */
  uint32_t changed_ms;  /* Time raw last changed          */
} Debounce;

typedef struct {
  uint8_t  state;          /* PiState                                   */
  uint32_t state_ms;       /* millis() at state entry                   */

  Debounce button;         /* 1 = pressed (polarity already applied)    */
  Debounce safe;           /* 1 = SAFE_TO_OFF HIGH                      */

  uint8_t  armed;          /* Qualified LOW seen after boot protection  */
  uint8_t  ignore_button;  /* Ignore button until next debounced release */
  uint8_t  press_active;   /* Current press counts for hold / short     */
  uint8_t  press_short_ok; /* Release of this press is a short press    */
  uint32_t press_ms;       /* Debounced press start                     */

  /* Logical outputs, 1 = active */
  uint8_t  out_tps_en;
  uint8_t  out_shutdown_req;
  uint8_t  out_led;
} PiFsm;

/* Start in STANDBY with all outputs inactive. */
void pi_fsm_init(PiFsm *f, uint32_t now_ms);

/* Run one step. button_pressed / safe_high are raw logical samples. */
void pi_fsm_update(PiFsm *f, uint32_t now_ms, uint8_t button_pressed, uint8_t safe_high);

#ifdef __cplusplus
}
#endif

#endif /* PI_POWER_FSM_H */
