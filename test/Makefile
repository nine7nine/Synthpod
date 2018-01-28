CC ?= gcc
C_FLAGS ?= -I../ -Wall -Wextra -Wpedantic -fprofile-arcs -ftest-coverage
LD_FLAGS ?= -lpthread

TAR := dummy_calloc.so
TAR += lfrtm_assert
TAR += lfrtm_speed

all: $(TAR)

dummy_calloc.so: dummy_calloc.c
	$(CC) -std=c11 -O0 -g -fPIC -shared -o $@ $< $(C_FLAGS) $(LD_FLAGS)

lfrtm_assert: lfrtm_test.c ../lfrtm/lfrtm.h
	$(CC) -std=c11 -O0 -g -o $@ $< $(C_FLAGS) $(LD_FLAGS)

lfrtm_speed: lfrtm_test.c ../lfrtm/lfrtm.h
	$(CC) -std=c11 -O3 -o $@ $< $(C_FLAGS) $(LD_FLAGS)

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

test_valgrind: lfrtm_assert
	valgrind ./$< 4

test_coverage: lfrtm_speed dummy_calloc.so
	LD_LIBRARY_PATH=./ LD_PRELOAD=dummy_calloc.so ./$< 2 1
	gcov lfrtm_test.c

test: test_assert test_speed test_valgrind test_coverage

clean: $(TAR)
	rm -f $(TAR) *gcov *.gc*
