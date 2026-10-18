# Unit tests for src/ctr.c (AES-128, SM4, and the CTR mode of sealed block
# files). vectors.h comes from Python's cryptography package; `make vectors`
# regenerates it.

SRC_DIR = ../../../..
TARGET  = test_ctr

CC     ?= gcc
CFLAGS  = -Wall -Wextra -std=c99 -I$(SRC_DIR)

SRC = test_ctr.c $(SRC_DIR)/ctr.c

.PHONY: all test vectors clean

all: test

$(TARGET): $(SRC) $(SRC_DIR)/ctr.h vectors.h
	$(CC) $(CFLAGS) $(SRC) -o $@

test: $(TARGET)
	./$(TARGET)

vectors:
	python3 vectors.py > vectors.h

clean:
	rm -f $(TARGET)
