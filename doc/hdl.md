# HDL

In hardware (FPGA, ASIC), synchronous code memory would be addressed by the PC.
The instruction arrives two clock cycles after PC changes.
When `;` is '1', the instruction bus settles while the group is executing.
The unified address space means that instruction pairs would be registered in `inst32`.
For a synchronous read, `inst32` gets registered right on time.
Two instruction groups typically execute in sequence, with `inst32` being right-shifted
by 16 bits after the first instruction group executes.
Random data memory access would just insert a couple of wait states to get the instruction back on the bus.

*other* instructions that take input from `inst32` settle quickly, not having to wait for decode.
Hardware optimizations to be realized are:

- Skip to the next instruction group if there are only nops left to execute.
- If none of the slots contain `>r`, `r>`, or `r@`, `;` can be taken early.

A real chip runs with real memory, which tends to be half the speed of a Han-Carlson 32-bit adder.
The ratio of SRAM delay to CPU logic delay increases with smaller process geometries.
Allowing the memory to run at half or less the system clock is compatible with die shrinks.
The CPU would output a `ce` to memory, where `ce` is active for 1 beat every 2 or more system clock cycles.
The FSM selects which of the PC, A, or B registers to feed to the address bus.
A typical code execution sequence proceeds as follows:

- `ce` is set to `1` and PC is placed on the address bus.
- The synchronous memory registers the address, `ce` is set to `0`.
- *The data has not necessarily settled yet.
- The 32-bit instruction pair is registered from the data bus.
- Slot 0 executes.
- Slot 1 executes.
- Slot 2 executes. The instruction is right-shifted 16 places.
- Slot 0 executes. The PC changes, so `ce` is set to `1`.
- Slot 1 executes. The synchronous memory registers the address, `ce` is set to `0`.
- Slot 2 executes. *The data has not necessarily settled yet.*
- Delay slot. The 32-bit instruction pair is registered from the data bus.

The cost of keeping adders out of the address bus path is a 1/7 drop in efficiency.
The idea is that this is made up for with a higher core clock.

The FSM would generate timing for data fetch and store, registering the incoming data
for the benefit of the barrel shifter and masking logic needed for `@`.
A typical data fetch proceeds as follows:

- `ce` is set to `1` and A or B is placed on the address bus.
- The synchronous memory registers the address, `ce` is set to `0`.
- *The data has not necessarily settled yet.
- The 32-bit `mem_data_reg` is registered from the data bus.
- The barrel shifted and masked is registered in T.

How about a RMW?

- `ce` is set to `1` and A or B is placed on the address bus.
- The synchronous memory registers the address, `ce` is set to `0`.
- *The data has not necessarily settled yet.
- The 32-bit `mem_data_reg` is registered from the data bus.
- `mem_data_reg` is barrel shifted, masked, and updated.
- A write is issued to that same address, `ce` is set to `1`.
- Delay slot, `ce` is set to `0`.

It looks like 2 cycles for write, 5 for read, and 7 for RMW.

If you compare that to an MCU with 400 MHz core clock and 200 MHz APB clock,
it looks about the same.

The code compiled by LiteForth will run in a real Forth chip, but it would have to avoid API calls.