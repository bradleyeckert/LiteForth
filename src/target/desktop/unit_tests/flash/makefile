# Unit tests for src/target/desktop/flash.c (uses the vendored Unity framework)

SRC_DIR     = ../../../..
DESKTOP_DIR = ../..
UNITY_DIR   = ../unity

TARGET = test_flash

CC     ?= gcc
CFLAGS  = -Wall -Wextra -std=c99 -g -I$(DESKTOP_DIR) -I$(SRC_DIR) -I$(UNITY_DIR)

SRC = $(DESKTOP_DIR)/flash.c test_flash.c $(UNITY_DIR)/unity.c

.PHONY: all test clean

all: test

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $@

test: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) lfflash.bin test_flash.bin
