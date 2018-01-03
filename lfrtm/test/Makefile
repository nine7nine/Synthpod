CC ?= clang

TAR := lfrtm_assert
TAR += lfrtm_speed

all: $(TAR)

lfrtm_assert: lfrtm_test.c ../lfrtm/lfrtm.h
	$(CC) -std=gnu11 -O0 -g -Wall -Wextra -Wpedantic -o $@ $< -I../ -lpthread -lm

lfrtm_speed: lfrtm_test.c ../lfrtm/lfrtm.h
	$(CC) -std=gnu11 -O3 -Wall -Wextra -Wpedantic -o $@ $< -I../ -lpthread -lm

test_assert: lfrtm_assert
	./$< 1
	./$< 2
	./$< 4
	./$< 8
	./$< 16
	./$< 32

test_speed: lfrtm_speed
	./$< 1
	./$< 2
	./$< 4
	./$< 8
	./$< 16
	./$< 32

test: test_assert test_speed

clean: $(TAR)
	rm $(TAR)
