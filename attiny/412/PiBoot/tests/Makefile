# Host tests for the power FSM, plus an AVR size check of the FSM core.
#   make            build and run host tests
#   make avr-size   compile pi_power_fsm.c for ATtiny412 and show its size

CC      ?= cc
CFLAGS  ?= -std=c99 -Wall -Wextra -Werror -O1
AVR_GCC ?= avr-gcc
AVR_SIZE ?= avr-size

test: test_fsm
	./test_fsm

test_fsm: test_fsm.c ../pi_power_fsm.c ../pi_power_fsm.h ../pi_power_config.h
	$(CC) $(CFLAGS) -o $@ test_fsm.c ../pi_power_fsm.c

avr-size:
	$(AVR_GCC) -mmcu=attiny412 -Os -std=gnu11 -Wall -Wextra -c ../pi_power_fsm.c -o pi_power_fsm.o
	$(AVR_SIZE) pi_power_fsm.o

clean:
	rm -f test_fsm pi_power_fsm.o

.PHONY: test avr-size clean
