# Unit tests for src/blake2s.c (a copy of github.com/bradleyeckert/blake2s):
# the vectors in vectors.h come from Python's hashlib.blake2s
# (`make vectors` regenerates them with vectors.py; needs python3).

SRC_DIR = ../../../..
TARGET  = test_blake2s

CC     ?= gcc
CFLAGS  = -Wall -Wextra -std=c99 -I$(SRC_DIR)

SRC = test_blake2s.c $(SRC_DIR)/blake2s.c

.PHONY: all test vectors clean

all: test

$(TARGET): $(SRC) $(SRC_DIR)/blake2s.h vectors.h
	$(CC) $(CFLAGS) $(SRC) -o $@

test: $(TARGET)
	./$(TARGET)

vectors:
	python3 vectors.py > vectors.h

clean:
	rm -f $(TARGET)
