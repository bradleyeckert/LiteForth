# Unit tests and interactive demo for src/target/desktop/serial_io.c

SRC_DIR     = ../../../..
DESKTOP_DIR = ../..

CC     ?= gcc
CFLAGS  = -Wall -Wextra -std=c99 -I$(DESKTOP_DIR) -I$(SRC_DIR)

SRC = $(DESKTOP_DIR)/serial_io.c

TEST_TARGET = test_serial_io
DEMO_TARGET = demo_terminal

.PHONY: all test demo clean

all: test

$(TEST_TARGET): test_serial_io.c $(SRC)
	$(CC) $(CFLAGS) $^ -o $@

$(DEMO_TARGET): demo_terminal.c $(SRC)
	$(CC) $(CFLAGS) $^ -o $@

test: $(TEST_TARGET)
	./$(TEST_TARGET)

demo: $(DEMO_TARGET)
	./$(DEMO_TARGET)

clean:
	rm -f $(TEST_TARGET) $(DEMO_TARGET)
