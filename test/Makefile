CC ?= clang

TAR := cross_clock_test

all: $(TAR)

cross_clock_test: cross_clock_test.c ../cross_clock/cross_clock.h
	$(CC) -std=gnu11 -O0 -g -Wall -Wextra -Wpedantic -o $@ $< -I../

test: cross_clock_test
	./$<

clean: $(TAR)
	rm $(TAR)
