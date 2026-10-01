# Checks the order of the API 0 table in src/api0_list.h

SRC_DIR     = ../../../..
DESKTOP_DIR = ../..

TARGET = test_api0

CC     ?= gcc
CFLAGS  = -Wall -Wextra -std=c99 -I$(DESKTOP_DIR) -I$(SRC_DIR)

.PHONY: all test update clean

all: test

$(TARGET): api0_order.c $(SRC_DIR)/api0_list.h $(SRC_DIR)/api0.h
	$(CC) $(CFLAGS) api0_order.c -o $@

test: $(TARGET)
	./$(TARGET) > got.txt
	@if diff -u --strip-trailing-cr expected.txt got.txt; then \
		echo "api0 order test passed"; rm -f got.txt; \
	else \
		echo "api0 order test FAILED: API 0 indices changed (see api0_list.h)"; \
		exit 1; \
	fi

# Accept the current table after appending an entry on purpose
update: $(TARGET)
	./$(TARGET) > expected.txt

clean:
	rm -f $(TARGET) got.txt
