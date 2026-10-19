# Unit tests for the guarded page in src/vm.c (vm_io_page, vm_io_ranges):
# the allowlist that keeps Forth away from the CH32H417's flash controller,
# DMA and the rest in a production build. unit_tests/vmasm checks that
# vm_rv32.S does the same.

SRC_DIR     = ../../../..
DESKTOP_DIR = ../..
TARGET      = test_ioguard

CC     ?= gcc
CFLAGS  = -Wall -Wextra -std=c99 -I$(DESKTOP_DIR) -I$(SRC_DIR)

SRC = test_ioguard.c $(SRC_DIR)/vm.c

.PHONY: all test clean

all: test

$(TARGET): $(SRC) $(SRC_DIR)/vm.h
	$(CC) $(CFLAGS) $(SRC) -o $@

test: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
