# Unit tests for blk_read_imm / blk_write_imm runs of 1 to BLK_MAX_LENGTH
# blocks in src/target/desktop/blocks.c (uses the vendored Unity framework)

SRC_DIR     = ../../../..
DESKTOP_DIR = ../..
UNITY_DIR   = ../unity

TARGET = test_blkrun

CC     ?= gcc
CFLAGS  = -Wall -Wextra -std=c99 -g -I$(DESKTOP_DIR) -I$(SRC_DIR) -I$(UNITY_DIR)

SRC = $(DESKTOP_DIR)/blocks.c $(DESKTOP_DIR)/rawdisk.c test_blkrun.c $(UNITY_DIR)/unity.c

.PHONY: all test clean

all: test

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $@

test: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) test_blkrun.fb4
