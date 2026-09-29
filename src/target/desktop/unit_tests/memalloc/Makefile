# Unit tests for src/memalloc.c

SRC_DIR     = ../../../..
DESKTOP_DIR = ../..

TARGET = test_memalloc

CC     ?= gcc
CFLAGS  = -Wall -Wextra -std=c99 -I$(DESKTOP_DIR) -I$(SRC_DIR)

SRC = test_memalloc.c $(SRC_DIR)/memalloc.c

.PHONY: all test clean

all: test

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $@

test: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
