CC ?= clang
C_FLAGS ?= -I../ -Wall -Wextra -Wpedantic $(shell pkg-config --cflags lv2)
LD_FLAGS ?= -lpthread -lm
SEED ?= 1234567890

all: mapper_assert mapper_speed

mapper_assert: mapper_test.c ../mapper.lv2/mapper.h
	$(CC) -std=c11 -g -o $@ $< $(C_FLAGS) $(LD_FLAGS)

mapper_speed: mapper_test.c ../mapper.lv2/mapper.h
	$(CC) -std=c11 -O3 -o $@ $< $(C_FLAGS) $(LD_FLAGS)

test_assert: mapper_assert
	./$< 1 0 $(SEED)
	./$< 2 1 $(SEED)
	./$< 4 0 $(SEED)
	./$< 8 1 $(SEED)
	./$< 16 0 $(SEED)
	./$< 32 1 $(SEED)

test_speed: mapper_speed
	./$< 1 0 $(SEED)
	./$< 2 1 $(SEED)
	./$< 4 0 $(SEED)
	./$< 8 1 $(SEED)
	./$< 16 0 $(SEED)
	./$< 32 1 $(SEED)

test: test_assert test_speed

clean:
	rm -f mapper_assert mapper_speed
