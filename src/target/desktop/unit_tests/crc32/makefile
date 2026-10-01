# Unit tests for src/crc32.c (uses the vendored Unity framework)

SRC_DIR     = ../../../..
UNITY_DIR   = ../unity

TARGET = test_crc32

CC     ?= gcc
CFLAGS  = -Wall -Wextra -std=c99 -g -I$(SRC_DIR) -I$(UNITY_DIR)

SRC = $(SRC_DIR)/crc32.c test_crc32.c $(UNITY_DIR)/unity.c

.PHONY: all test clean

all: test

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $@

test: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
