# Unit tests for src/target/desktop/rawdisk.c, on disk image files.
# (On Windows, rawdisk.c opens a removable disk by drive letter instead;
# that part can only be tried by hand: lf -k F: as administrator.)

SRC_DIR     = ../../../..
DESKTOP_DIR = ../..

TARGET = test_rawdisk

CC     ?= gcc
CFLAGS  = -Wall -Wextra -std=c99 -I$(DESKTOP_DIR) -I$(SRC_DIR)

SRC = test_rawdisk.c $(DESKTOP_DIR)/rawdisk.c

.PHONY: all test clean

all: test

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $@

test: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) test_rawdisk.img
