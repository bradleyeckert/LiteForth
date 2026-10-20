# Unit tests for the CH32H417 V5F's machine-mode guard checks
# (src/target/CH32H417/V5F/User/guard.c): the store decoder behind the
# HardFault handler's RCC/CRC store emulation, the stores it lets through,
# and the Guarded DMA buffer rule (gdma.c).

V5F_DIR = ../../../CH32H417/V5F/User
TARGET  = test_guard

CC     ?= gcc
CFLAGS  = -Wall -Wextra -std=c99 -I$(V5F_DIR)

SRC = test_guard.c $(V5F_DIR)/guard.c

.PHONY: all test clean

all: test

$(TARGET): $(SRC) $(V5F_DIR)/guard.h
	$(CC) $(CFLAGS) $(SRC) -o $@

test: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
