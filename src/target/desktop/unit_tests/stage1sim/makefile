# stage1sim: the V3F's stage 1 (V3F/User/stage1.c) and USB bridge
# (Common/cdc_bridge.c), built unchanged on a simulated board (sim.c,
# fake/), and test_stage1.py playing every update, rollback and recovery
# scenario on it, with resets and power cuts. test_stage1.py makes the keys
# and packages with scripts/admin/sealblocks.py, then builds `sim` and
# `sim_strict` (SECURITY_LEVEL 2) against them with `make sims`; its report, every
# scenario's output word for word, goes to reports/stage1sim.md. Then
# test_hil.py runs scripts/admin/hiltest.py, the tests for a real board,
# against the simulated one (its ports as ptys; needs pyserial), writing
# reports/hil-sim.md, and test_lockdown.py runs scripts/admin/lockdown.py
# on a level 2 board (sim_strict) with a fake wlink, and test_provision.py
# runs scripts/admin/provision.py's two passes over a batch of them. Needs Linux (the
# board's memory is mapped at its own addresses), python3 and the
# cryptography package; skipped otherwise.

SRC     = ../../../..
CH      = $(SRC)/target/CH32H417
V3F     = $(CH)/V3F/User
WORK    = work
CC     ?= gcc
CFLAGS  = -Wall -Wextra -std=gnu99 -O1 -g -Wno-unused-parameter -Wno-int-to-pointer-cast \
          '-DCDC_FENCE()=__sync_synchronize()' -DSTAGE1_V5F_START_MS=300 \
          -include $(WORK)/sim_root.h -Ifake -I$(V3F) -I$(CH)/Common -I$(SRC)

UNITS   = sim.c $(WORK)/cdc_bridge.c $(V3F)/stage1.c $(V3F)/lf_update.c $(V3F)/lf_sealed.c \
          $(V3F)/lf_ctr.c $(V3F)/lf_blake2s.c $(V3F)/lf_monocypher.c \
          $(V3F)/lf_monocypher_ed25519.c $(V3F)/lf_card.c $(V3F)/lf_exfat.c $(V3F)/lf_ymrecv.c
DEPS    = $(UNITS) fake/*.h $(V3F)/*.h $(CH)/Common/cdc_shared.h $(CH)/Common/security.h $(CH)/Common/cdc_bridge.h \
          $(SRC)/update.h $(SRC)/ymrecv.h $(SRC)/sealed.h $(WORK)/sim_root.h

.PHONY: all test sims clean

all: test

# cdc_bridge.c is copied, so that its includes find fake/'s USB driver
# state before the real ch32h417_usbfs_device.h beside it
$(WORK)/cdc_bridge.c: $(CH)/Common/cdc_bridge.c
	cp $< $@

$(WORK)/sim: $(DEPS)
	$(CC) $(CFLAGS) -DSECURITY_LEVEL=0 $(UNITS) -o $@

$(WORK)/sim_strict: $(DEPS)
	$(CC) $(CFLAGS) -DSECURITY_LEVEL=2 $(UNITS) -o $@

sims: $(WORK)/sim $(WORK)/sim_strict

test:
	@if [ "$$(uname -s)" != Linux ]; then \
		echo "stage1sim tests skipped: they need Linux"; \
	elif ! python3 -c "import cryptography" 2>/dev/null; then \
		echo "stage1sim tests skipped: python3 with the cryptography package not found"; \
	else \
		mkdir -p reports && python3 test_stage1.py --report reports/stage1sim.md && \
		python3 test_hardness.py && \
		if python3 -c "import serial" 2>/dev/null; then \
			python3 test_hil.py && python3 test_lockdown.py && python3 test_provision.py; \
		else \
			echo "hiltest.py, lockdown.py and provision.py on the simulated board skipped: python3's pyserial not found"; \
		fi; \
	fi

clean:
	rm -rf $(WORK) reports __pycache__
