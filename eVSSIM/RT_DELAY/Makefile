# rt_delay - real-time delay library.
#
#   make            librt_delay.a
#   make test       unit tests, normal build and under ThreadSanitizer
#   make bench      timing benchmark (quiet host, root to block C-states)
#   make clean
CC      ?= gcc
CFLAGS  ?= -O2 -g
CFLAGS  += -std=gnu99 -Wall -Wextra -Werror -I.
LDLIBS  += -lpthread

BUILD   := build
TSAN    := -fsanitize=thread -O1 -g

.PHONY: all test test-plain test-tsan bench clean

all: $(BUILD)/librt_delay.a

$(BUILD):
	@mkdir -p $@

$(BUILD)/rt_delay.o: rt_delay.c rt_delay.h | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/librt_delay.a: $(BUILD)/rt_delay.o
	$(AR) rcs $@ $^

$(BUILD)/test_rt_delay: tests/test_rt_delay.c $(BUILD)/librt_delay.a
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

# TSan build compiles the library from source so its atomics are instrumented.
$(BUILD)/test_rt_delay_tsan: tests/test_rt_delay.c rt_delay.c rt_delay.h | $(BUILD)
	$(CC) $(CFLAGS) $(TSAN) -o $@ tests/test_rt_delay.c rt_delay.c $(LDLIBS)

$(BUILD)/bench_rt_delay: tests/bench_rt_delay.c $(BUILD)/librt_delay.a
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test: test-plain test-tsan

test-plain: $(BUILD)/test_rt_delay
	./$<

test-tsan: $(BUILD)/test_rt_delay_tsan
	TSAN_OPTIONS=halt_on_error=1 ./$<

bench: $(BUILD)/bench_rt_delay
	./$< $(ITERS)

clean:
	rm -rf $(BUILD)
