# Caravel RV32E MCU (sky130, FOSS flow experiment)

A small microcontroller for the efabless/ChipFoundry Caravel user area, taken
through an all-FOSS flow: Icarus/Verilator for simulation, Yosys for
synthesis, OpenLane 1.0.2 (OpenROAD) for place and route, Magic for DRC and
Netgen for LVS, on the sky130A PDK. It was an experiment to find the pain
points, done by Claude in a sandbox with only GitHub access; the
architecture is expected to be reworked. `CLAUDE.md` has the detailed notes.

## Design

| Block | Details |
|---|---|
| CPU (`mcu_cpu.v`) | RV32E (16 registers), full RV32I base ISA minus CSRs; FENCE is a no-op, ECALL/EBREAK/illegal halt. Multi-cycle: ALU/branch 2 cycles, store 3, load 4 |
| RAM (`mcu_ram.v`) | 512 x 32 (2 KB) SRAM22 macro `sram22_512x32m4w8`, synchronous read, byte write mask |
| Peripherals (`mcu_top.v`) | 16-bit GPIO, 8N1 UART (`mcu_uart.v`), 32-bit timer, mailbox with IRQ to the management SoC |
| Caravel (`user_project_wrapper.v`) | Wishbone loader/control, LA[31:0] = PC, LA[63:32] = mailbox, `user_irq[0]` = mailbox |

Management SoC view (Wishbone, base 0x3000_0000): RAM at 0x3000_0000-07FF
(firmware is loaded here), CTRL at 0x3000_1000 (bit 0 run, bit 1 halted),
MAILBOX at 0x3000_1004 (reading clears the IRQ), PC at 0x3000_1008.
CPU view: RAM at 0, GPIO_OUT/OE/IN at 0x8000_0000/04/08, UART data/status/
divisor at 0x8000_0010/14/18, timer at 0x8000_0020, mailbox at 0x8000_0030.
Pads: io[8] UART TX, io[9] UART RX, io[25:10] GPIO.

## Verification

```
make random   # 200 random 300-instruction programs: RTL vs. Python ISS (sw/rv.py)
make sim      # firmware (sw/firmware.py): UART loopback, GPIO, timer, mailbox, byte/half stores
make sram22   # the same with SRAM22's own behavioral model
```

`sw/rv.py` is a tiny RV32 assembler and reference ISS; `sw/randtest.py`
compares all of RAM after each random program.

## Physical design status

| Run | RAM | Synthesis | P&R | Magic DRC | Netgen LVS | Timing |
|---|---|---|---|---|---|---|
| r4 | 4096 flip-flops (older RTL) | 17k cells | 0 routing violations | **clean** | **clean** | typical met; slow/fast corners failed (fixed in RTL since) |
| r6 | SRAM22 512x32 | ~6k cells + macro | 0 routing violations | - | 13 SRAM pins open (LEF fixed since, not rerun) | - |

Reports are in `results/`. Still to do: rerun the macro with the fixed LEF,
harden `user_project_wrapper` (its OpenLane config isn't written yet),
wrapper DRC/LVS, gate-level simulation.

## Reproducing

`tools/bootstrap.sh` builds the toolchain from GitHub only (OpenROAD .deb
plus its missing libraries, Magic, Netgen, Tcl/Tk, tcllib, oss-cad-suite,
the sky130A build OpenLane 1.0.2 pins) and applies
`tools/openlane1-1.0.2-openroad-2024.12.patch`. Then:

```
source tools/env.sh
tools/runflow.sh $PWD/openlane/mcu_top r7     # needs the Caravel template's
tools/waitflow.sh $PWD/openlane/mcu_top r7 3600   # verilog/rtl/defines.v (included)
```

The paths in `tools/*.sh` assume `/home/claude/{tools,src,pdk}`.

## Third-party material

- `macros/sram22_512x32m4w8/`: SRAM22 sky130 macros (github.com/rahulk29/sram22_sky130_macros,
  UC Berkeley's SRAM22 generator), BSD-3-Clause, Copyright (c) 2023 rahulk29 (`LICENSE` there). The LEF is modified: 13 pin
  rectangles shrunk (marked in the file).
- `verilog/rtl/defines.v`, `user_defines.v`, `uprj_netlists.v`,
  `user_project_wrapper.v`, `openlane/mcu_top/base_mcu_top.sdc` and
  `pdn_cfg.tcl` derive from efabless `caravel_user_project` / OpenLane,
  Apache-2.0.
