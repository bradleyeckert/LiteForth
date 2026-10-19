# ymrecv test: test_ymrecv.py plays a YMODEM sender (with the packet code of
# unit_tests/ymodem/sender.py) against src/ymrecv.c, stage 1's receiver,
# run over pipes by drv.c: good transfers, damaged, repeated and short
# packets, refusals and cancels.

SRC_DIR = ../../../..
CC     ?= gcc
CFLAGS  = -Wall -Wextra -std=c99 -D_DEFAULT_SOURCE -O2 -I$(SRC_DIR)

.PHONY: all test clean

all: test

drv: drv.c $(SRC_DIR)/ymrecv.c $(SRC_DIR)/ymrecv.h
	$(CC) $(CFLAGS) drv.c $(SRC_DIR)/ymrecv.c -o $@

test: drv
	@if command -v python3 > /dev/null 2>&1; then python3 test_ymrecv.py ./drv; \
	else echo "ymrecv tests skipped (no python3)"; fi

clean:
	rm -f drv
