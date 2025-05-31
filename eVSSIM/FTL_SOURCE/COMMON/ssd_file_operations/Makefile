# Makefile

SRC_DIR = .
TEST_DIR = test
UNITY_DIR = $(TEST_DIR)/unity

SRC_FILES = $(SRC_DIR)/ssd_file_operations.c
TEST_FILES = $(TEST_DIR)/test_ssd_file_ops.c
UNITY_SRC = $(UNITY_DIR)/src/unity.c
UNITY_HEADER = $(UNITY_DIR)/src/

CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -Iinclude -I$(UNITY_HEADER)

TARGET = test_runner

.PHONY: all test clean unity

all: test

unity:
	@if [ ! -d "$(UNITY_DIR)" ]; then \
		echo "Cloning Unity..."; \
		git clone https://github.com/ThrowTheSwitch/Unity.git $(UNITY_DIR); \
	else \
		echo "Unity already exists."; \
	fi

test: unity
	$(CC) $(CFLAGS) $(SRC_FILES) $(TEST_FILES) $(UNITY_SRC) -o $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
