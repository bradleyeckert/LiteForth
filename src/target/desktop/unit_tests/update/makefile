# Tests src/update.c, stage 1's install logic (doc/admin/firmware-updates.md):
# installs, confirms, rollbacks, every refusal, and a power cut at every
# step. gen.py makes the packages with scripts/admin/sealblocks.py, so this needs
# python3 and the cryptography package; skipped (with a note) without it.

SRC_DIR     = ../../../..
DESKTOP_DIR = ../..
TARGET      = test_update

CC     ?= gcc
CFLAGS  = -Wall -Wextra -std=c99 -O2 -g -I$(DESKTOP_DIR) -I$(SRC_DIR)

SRC = test_update.c $(SRC_DIR)/update.c $(SRC_DIR)/sealed.c $(SRC_DIR)/ctr.c \
      $(SRC_DIR)/blake2s.c $(SRC_DIR)/monocypher.c $(SRC_DIR)/monocypher-ed25519.c

.PHONY: all test clean

all: test

$(TARGET): $(SRC) $(SRC_DIR)/update.h $(SRC_DIR)/sealed.h
	$(CC) $(CFLAGS) $(SRC) -o $@

test: $(TARGET)
	@if python3 -c "import cryptography" 2>/dev/null; then \
		python3 gen.py work && ./$(TARGET); \
	else \
		echo "update tests skipped: python3 with the cryptography package not found"; \
	fi

clean:
	rm -rf $(TARGET) work __pycache__
