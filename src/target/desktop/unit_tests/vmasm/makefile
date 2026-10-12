# vm_rv32.S test: runs the same random VM cases through vm.c's C vmExec
# (native) and through vm_rv32.S (RV32, in the rv32emu emulator here) and
# compares the results, with one and two TOS registers. Needs clang with the
# RISC-V target and ld.lld; without them the test is skipped.
#
#   make test        the comparison (CASES of each kind, default 2000)
#   make bench       RV32 instructions per VM instruction: C -Os, C -O2, asm

SRC_DIR     = ../../../..
DESKTOP_DIR = ../..

CC     ?= gcc
CLANG  ?= clang
CASES  ?= 2000

CFLAGS = -O2 -Wall -Wextra -I$(DESKTOP_DIR) -I$(SRC_DIR)
RV     = $(CLANG) --target=riscv32-unknown-elf -march=rv32imc -mabi=ilp32 \
         -ffreestanding -nostdlib -fuse-ld=lld -Wl,-Ttext=0x10000 \
         -Wall -Wextra -I$(DESKTOP_DIR) -I$(SRC_DIR)

VM     = $(SRC_DIR)/vm.c $(SRC_DIR)/vm.h $(SRC_DIR)/vm_labels.h
DEPS   = difftest.c $(VM) $(SRC_DIR)/vm_rv32.S rt_rv32.c

.PHONY: all test compare bench clean

all: test

rv32emu: rv32emu.c
	$(CC) $(CFLAGS) rv32emu.c -o $@

# One configuration: $(1) is a name, $(2) extra flags
define compare
	@$(CC) $(CFLAGS) $(2) -DCASES=$(CASES) difftest.c $(SRC_DIR)/vm.c -o ref_$(1)
	@./ref_$(1) > ref_$(1).txt
	@$(RV) -O2 $(2) -DCASES=$(CASES) -DVM_ASM difftest.c rt_rv32.c \
		$(SRC_DIR)/vm.c $(SRC_DIR)/vm_rv32.S -o asm_$(1).elf
	@./rv32emu asm_$(1).elf > asm_$(1).txt
	@if cmp -s ref_$(1).txt asm_$(1).txt; then \
		echo "vm_rv32.S matches vm.c ($(1), `wc -l < ref_$(1).txt` lines)"; \
	else \
		diff ref_$(1).txt asm_$(1).txt | head -20; \
		echo "vm_rv32.S test FAILED ($(1)): see ref_$(1).txt and asm_$(1).txt"; \
		exit 1; \
	fi
endef

test: $(DEPS)
	@if ! echo 'int main(void){return 0;}' | $(RV) -x c - -o /dev/null 2>/dev/null; then \
		echo "vmasm test skipped: needs clang with the riscv32 target and ld.lld"; \
	else \
		$(MAKE) --no-print-directory compare; \
	fi

compare: rv32emu
	$(call compare,1tos,)
	$(call compare,2tos,-DTWO_REGISTER_TOS)
	@rm -f ref_* asm_*

bench: rv32emu
	$(RV) -Os -DBENCH difftest.c rt_rv32.c $(SRC_DIR)/vm.c -o bench.elf
	@echo "== C vmExec, -Os"; ./rv32emu bench.elf
	$(RV) -O2 -DBENCH difftest.c rt_rv32.c $(SRC_DIR)/vm.c -o bench.elf
	@echo "== C vmExec, -O2"; ./rv32emu bench.elf
	$(RV) -O2 -DBENCH -DVM_ASM difftest.c rt_rv32.c $(SRC_DIR)/vm.c $(SRC_DIR)/vm_rv32.S -o bench.elf
	@echo "== vm_rv32.S"; ./rv32emu bench.elf
	rm -f bench.elf

clean:
	rm -f rv32emu ref_* asm_* bench.elf
