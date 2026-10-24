# CLAUDE.md — hardware/caravel

Notes for Claude from the first attempt (2026-10-23) at taking an MCU through
a FOSS sky130 flow for the Caravel user area: Yosys, OpenLane 1 (OpenROAD),
Magic DRC, Netgen LVS. Read this before starting again; it is mostly a list
of what cost time. README.md describes the design and results for people.

## Where it got to

- RTL (`verilog/rtl`): RV32E multi-cycle core `mcu_cpu.v`, UART, 512x32
  SRAM22 RAM (`mcu_ram.v`), `mcu_top.v`, Caravel `user_project_wrapper.v`.
  Verified: `make random` (RTL vs. `sw/rv.py` ISS, RAM compared word for
  word), `make sim` (all peripherals), `make sram22` (vendor SRAM model).
- Run r4 (older RTL, RAM built from 4096 flip-flops): full flow to signoff.
  Magic DRC 0, Netgen LVS clean, typical-corner timing met; slow-corner
  setup (from `wb_rst_i`) and fast/slow-corner IO hold failed. Both fixed
  in the RTL since (reset synchronizer, registered Wishbone inputs), not
  rerun as flop RAM.
- Run r6 (current RTL, SRAM22): routing clean (0 DRT violations), hold
  repair fast, LVS failed: the router ended wires *exactly on the edge* of
  the SRAM's 0.32 um met1 pins (addr, ce, we, clk, rstb) and Magic's
  hierarchical extraction does not connect abutting parent/child shapes.
  Fixed in the LEF (those 13 pin rects shrunk to their 0.14 um centre, so
  access must overlap), **not yet rerun** (r7). Then: user_project_wrapper
  hardening (its OpenLane config is not written yet), wrapper DRC/LVS,
  gate-level sim.

## Environment (the big time sink)

- The sandbox allowed GitHub (clones and release downloads) and nothing
  else: no apt, PyPI, npm, conda, Docker registries, nix cache,
  raw.githubusercontent.com or the GitHub API (`gh api` 403s; read release
  asset names with WebFetch on `.../releases/expanded_assets/<tag>`).
  `tools/bootstrap.sh` rebuilds everything from GitHub in ~40 min; check
  first whether the session allows a package manager (then use OpenLane 2 /
  nix instead and skip most of this file's environment notes).
- 2 cores, 7 GB RAM, ~14 GB disk. Disk ran out twice: delete
  `runs/*/tmp` and old runs, keep only reports and final views.
- OpenROAD: Precision Innovations' 2024-12-14 Ubuntu 22.04 .deb. It needs
  libpython3.10 (built from cpython 3.10), OR-Tools 9.11.4210 (Google's
  release tarball), Tcl 8.6 + tclreadline (built; 2.5.0 symlinked as the
  2.3.8 soname) and Qt5Charts (no headers anywhere: a stub .so with the 44
  symbols, versioned `Qt_5`; `tools/qtcharts_syms.txt`). Its embedded
  Python needs click, pyyaml, xlsxwriter copied into py310's site-packages.
- OpenLane 1.0.2 runs natively once tcllib is installed (`package require
  json`). Always pass `-ignore_mismatches`. It refuses `-from/-to`.
- OpenLane 1 vs. OpenROAD 2024-12 API breaks (`tools/openlane1-*.patch`):
  `odb.read_def(db, f)` -> `odb.read_def(db.getTech(), f)`; databases from
  `odb.dbDatabase.create()` have no logger ("No logger is installed in
  odb") -> `openroad.Design.createDetachedDb()`; CTS needs
  `-dont_use_dummy_load` (inv_8 not in the cell set); `analyze_power_grid
  -outfile` -> `-voltage_file`. Each surfaced only when a run reached it.
- No resume: a failure late in the flow meant rerunning 60-90 min.
  `tools/resume.tcl` (+ `resumeflow.sh`) restarts the signoff steps on an
  existing run: it moves `runs/<tag>/config.tcl` aside, runs `prep` on the
  same tag, re-sources the saved state, then the steps (they live in
  flow.tcl, so `flow_steps.tcl` is extracted from it).
- `runflow.sh`/`waitflow.sh`: run the flow under nohup and poll it in
  <10 min chunks (tool calls time out at 10 min; the flow survives).
- Never `pkill -f <pattern>` in a command whose own text contains the
  pattern: it kills the calling shell (happened twice). Kill by pid.

## Design / flow lessons

- SystemVerilog keywords used as names break Verilator/Icarus: `mailbox`,
  `bit`, `cell` (a Verilog config keyword). Lint with
  `verilator --lint-only -Wall` before a run; OpenLane's linter stops the flow.
- Caravel template SDC: its clock *source latency range* (4.65-5.57 ns) is
  applied early/late to both ends of every reg-to-reg path (no CRPR for
  source latency). A single value (midpoint) is used here; note it if
  anyone signs off.
- The template's `PL_RESIZER_HOLD_SLACK_MARGIN` 0.4 with a flip-flop RAM
  made 4,700+ hold endpoints (every RAM bit's enable mux is a Q->D loop);
  OpenROAD's hold repair took >90 min and r5 never finished. Use a real
  SRAM macro (SRAM22, as now) or sky130 `edfxtp` cells (no external loop;
  in `no_synth.cells`, so instantiate them and give Yosys a blackbox).
- Hold violations at the slow corner were IO paths (Wishbone inputs):
  register macro inputs at the boundary, and synchronize `wb_rst_i`
  (raw reset on the datapath made the -1 ns slow-corner setup paths).
- SRAM22 (`github.com/rahulk29/sram22_sky130_macros`, BSD-3): power pins
  are horizontal met2 rails, obstructions only met1/met2. OpenLane's default
  macro PDN grid connects met4-met5 and did nothing. Working recipe
  (`openlane/mcu_top/pdn_cfg.tcl`): stdcell met4 straps (pitch 50) run over
  the macro; a macro grid with only `add_pdn_connect -layers "met4 met2"`
  drops via stacks. A ring + separate straps around the macro was isolated
  from the core grid (PSM-0069). Place the macro so its halo swallows any
  thin std-cell channel at the core edge (PDN-0179) and any ring stays in
  the die. Use the `_bb.v` with `/// sta-blackbox` for synthesis/STA (the
  vendor `.v` is behavioral and OpenSTA rejects it).
- SRAM22 read is synchronous: the CPU decodes straight from `mem_rdata` in
  EXEC and latches it; Wishbone reads take one extra cycle (`wb_ph1`).
- Debug PDN quickly outside the flow: source `runs/<tag>/config.tcl`,
  `read_db` the tapcell odb, source the PDN cfg, `pdngen`,
  `check_power_grid -net ... -error_file`.

## Checklist for next time

1. Check egress (package managers? GitHub only?) and resources first.
2. `tools/bootstrap.sh`, `source tools/env.sh`; smoke test `openroad
   -version`, an `odb` import under `openroad -python`, `magic`, `netgen`.
3. Lint, `make random`, `make sim` before any flow run.
4. Run synthesis + floorplan + PDN interactively (minutes) before a full run.
5. Expect the flow's late steps to break; resume them with `resume.tcl`.
