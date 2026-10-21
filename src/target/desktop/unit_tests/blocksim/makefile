# blocksim: the V5F's block driver (V5F/User/blocks.c) with Guarded DMA
# (gdma.c, guard.c), built unchanged on a simulated SD card, SDIO and DMA
# channel (sim.c, fake/), and tested through blocks.h: reads and writes of
# one block, 8 blocks (two 32-sector commands) and odd sector counts; a
# refused command, a CRC error, a DMA error, a stall and a card that stays
# busy; buffers Guarded DMA must refuse; and that the channel is burned
# after every transfer. The cards come from gen_cards.py (mkexfat.py).
# Needs Linux (the registers are mapped at the chip's addresses) and
# python3; skipped otherwise. The desktop's own blocks.c is untouched.

SRC     = ../../../..
CH      = $(SRC)/target/CH32H417
V5F     = $(CH)/V5F/User
WORK    = work
CC     ?= gcc
CFLAGS  = -Wall -Wextra -std=gnu99 -O1 -g -fno-pie -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast \
          '-DCDC_FENCE()=__sync_synchronize()' -Ifake -I$(V5F) -I$(CH)/Common -I$(SRC)
# Not PIE, so .data and .bss lie below 4 GB as uint32_t addresses; they're
# what Link_v5f.ld calls _data_vma.._susrstack (gdma.c's buffer rule)
LDFLAGS = -no-pie -Wl,--defsym=_data_vma=__data_start -Wl,--defsym=_susrstack=_end

SEALED  = $(SRC)/sealed.c $(SRC)/ctr.c $(SRC)/blake2s.c $(SRC)/monocypher.c $(SRC)/monocypher-ed25519.c
UNITS   = sim.c $(V5F)/blocks.c $(V5F)/gdma.c $(V5F)/guard.c $(SRC)/card.c $(SRC)/exfat.c $(SEALED)
DEPS    = $(UNITS) fake/*.h $(V5F)/blocks.h $(V5F)/gdma.h $(V5F)/guard.h $(V5F)/sdio.h

.PHONY: all test clean

all: test

$(WORK)/sim: $(DEPS)
	@mkdir -p $(WORK)
	$(CC) $(CFLAGS) $(UNITS) $(LDFLAGS) -o $@

$(WORK)/card.img: gen_cards.py $(SRC)/../scripts/mkexfat.py
	@mkdir -p $(WORK)
	python3 gen_cards.py $(WORK)

test:
	@if [ "$$(uname -s)" != Linux ]; then \
		echo "blocksim tests skipped: they need Linux"; \
	elif ! command -v python3 >/dev/null; then \
		echo "blocksim tests skipped: python3 not found"; \
	else \
		$(MAKE) --no-print-directory $(WORK)/sim $(WORK)/card.img && \
		$(WORK)/sim $(WORK)/card.img $(WORK)/unsigned.img; \
	fi

clean:
	rm -rf $(WORK)
