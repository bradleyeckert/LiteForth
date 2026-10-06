# Unit tests for the I/O page: src/vm.c built with VM_IO_PAGE, and the
# CH32H417's peripheral range table (src/target/CH32H417/V5F/User/iopage.c).
# The page is backed by a plain array here, so only the range checks are
# tested, not the hardware.

SRC_DIR     = ../../../..
DESKTOP_DIR = ../..
CH32_DIR    = ../../../CH32H417/V5F/User

TARGET = test_iopage

CC     ?= gcc
# The desktop directory comes first, so its options.h and lftime.h are used;
# iopage.h is only in the CH32H417 directory.
CFLAGS  = -Wall -Wextra -std=c99 -DVM_IO_PAGE=5 \
          -I$(DESKTOP_DIR) -I$(SRC_DIR) -I$(CH32_DIR)

SRC = test_iopage.c $(SRC_DIR)/vm.c $(CH32_DIR)/iopage.c

.PHONY: all test clean

all: test

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $@

test: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
