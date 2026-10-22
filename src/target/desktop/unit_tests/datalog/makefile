# Data logs (doc/admin/data-logs.md): src/datalog.c, src/logfile.c and
# exfat.c's exfat_create/exfat_trim, built into dltest and driven by
# test_datalog.py on card images from scripts/mkexfat.py, against
# scripts/admin/lflog.py (the server's side). Needs python3 and the
# cryptography package; skipped without them.

SRC = ../../../..
CC ?= gcc
CFLAGS = -Wall -Wextra -std=c99 -O2 -I../.. -I$(SRC)
UNITS = dltest.c $(SRC)/datalog.c $(SRC)/logfile.c $(SRC)/exfat.c $(SRC)/ctr.c \
        $(SRC)/blake2s.c $(SRC)/monocypher.c

.PHONY: all test clean

all: test

dltest: $(UNITS) $(SRC)/datalog.h $(SRC)/logfile.h $(SRC)/exfat.h
	$(CC) $(CFLAGS) $(UNITS) -o $@

test: dltest
	@python3 test_datalog.py

clean:
	rm -rf dltest work __pycache__
