# Unit tests for src/iomap.c, the VM's I/O ports, through vm.c's `>sys`
# and `sys>` (indexes from IOMAP_FIRST up): the terminal on a fake serial
# port, the math unit's um*, m*, mu/mod and star-slash-mod against 64-bit C,
# division by zero, and ports that aren't there. unit_tests/vmasm checks
# that vm_rv32.S calls the ports as vm.c does.

SRC_DIR     = ../../../..
DESKTOP_DIR = ../..
TARGET      = test_iomap

CC     ?= gcc
CFLAGS  = -Wall -Wextra -std=c99 -O2 -I$(DESKTOP_DIR) -I$(SRC_DIR)

SRC = test_iomap.c $(SRC_DIR)/iomap.c $(SRC_DIR)/vm.c

.PHONY: all test clean

all: test

$(TARGET): $(SRC) $(SRC_DIR)/iomap.h $(SRC_DIR)/vm.h
	$(CC) $(CFLAGS) $(SRC) -o $@

test: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
