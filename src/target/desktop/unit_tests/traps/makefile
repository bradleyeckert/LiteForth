# Unit tests for vmTrap in src/vm.c: a CPU exception during a VM run (on
# the CH32H417, HardFault_Handler in V5F/User/ch32h417_it.c) ends the
# innermost vmRun with an error, which goes to the app's yeet handler or
# back to the terminal. Here API calls stand in for the trap.

SRC_DIR     = ../../../..
DESKTOP_DIR = ../..
TARGET      = test_traps

CC     ?= gcc
CFLAGS  = -Wall -Wextra -std=c99 -O2 -I$(DESKTOP_DIR) -I$(SRC_DIR)

SRC = test_traps.c $(SRC_DIR)/vm.c

.PHONY: all test clean

all: test

$(TARGET): $(SRC) $(SRC_DIR)/vm.h
	$(CC) $(CFLAGS) $(SRC) -o $@

test: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
