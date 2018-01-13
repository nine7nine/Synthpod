CC ?= clang
C_FLAGS ?= -I../ -Wall -Wextra -Wpedantic $(shell pkg-config --cflags lv2)
LD_FLAGS ?= -lpthread

all: mapper_assert mapper_speed

mapper_assert: mapper_test.c ../mapper.lv2/mapper.h
	$(CC) -std=c11 -g -o $@ $< $(C_FLAGS) $(LD_FLAGS)

mapper_speed: mapper_test.c ../mapper.lv2/mapper.h
	$(CC) -std=c11 -O3 -o $@ $< $(C_FLAGS) $(LD_FLAGS)

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

clean:
	rm -f mapper_assert mapper_speed
