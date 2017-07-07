CC ?= clang

all: mapper_assert mapper_speed

mapper_assert: mapper_test.c ../mapper.lv2/mapper.h
	$(CC) -std=gnu11 -g -o $@ $< -I../ $(shell pkg-config --cflags lv2) -lpthread

mapper_speed: mapper_test.c ../mapper.lv2/mapper.h
	$(CC) -std=gnu11 -O3 -o $@ $< -I../ $(shell pkg-config --cflags lv2) -lpthread

test_assert: mapper_assert
	./$< 1 0
	./$< 2 1
	./$< 4 0
	./$< 8 1
	./$< 16 0
	./$< 32 1

test_speed: mapper_speed
	./$< 1 0
	./$< 2 1
	./$< 4 0
	./$< 8 1
	./$< 16 0
	./$< 32 1

test: test_assert test_speed
