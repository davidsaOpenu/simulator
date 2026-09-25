# Spike: real-time delay injection in eVSSIM. Builds the host interference workers used by
# scripts/run_guest_latency.sh (NOISE= / NOISE_SPEC=).
CC      ?= gcc
CFLAGS  ?= -O2 -g -std=gnu11 -Wall -Wextra -Wno-unused-parameter -pthread -Isrc
LDLIBS  ?= -lpthread -lrt -lm

LIB_SRC := src/core_affinity.c src/delay_inject.c
LIB_OBJ := $(LIB_SRC:.c=.o)

BINS := bin/worker_cpu_bound bin/worker_io_bound bin/worker_mixed_5050

all: $(BINS)

bin:
	@mkdir -p bin

bin/worker_%: workers/worker_%.c $(LIB_OBJ) | bin
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(LIB_OBJ) $(BINS)

.PHONY: all clean
