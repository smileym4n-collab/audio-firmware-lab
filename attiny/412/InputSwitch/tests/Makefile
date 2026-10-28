CC ?= cc
CFLAGS ?= -std=c99 -Wall -Wextra -Werror -pedantic -O1 -g

test: test_fsm
	./test_fsm

test_fsm: test_fsm.c ../input_switch_fsm.c ../input_switch_fsm.h ../input_switch_config.h
	$(CC) $(CFLAGS) test_fsm.c ../input_switch_fsm.c -o $@

sanitize:
	$(CC) $(CFLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer test_fsm.c ../input_switch_fsm.c -o test_fsm_sanitize
	./test_fsm_sanitize

clean:
	rm -f test_fsm test_fsm_sanitize
	rm -rf test_fsm.dSYM test_fsm_sanitize.dSYM

.PHONY: test sanitize clean
