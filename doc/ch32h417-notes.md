# CH32H417 notes: where the chip and WCH's documents disagree

These notes come from porting LiteForth to the
[nanoCH32H417](../src/target/CH32H417/README.md) board, with the
*CH32H417/H416/H415 Reference Manual* V1.8 (RM below) and WCH's EVT SDK
([openwch/ch32h417](https://github.com/openwch/ch32h417), `EVT/EXAM/SRC`
at commit 6d1e469). Most were found by reading and writing registers live
from the LiteForth prompt (peripheral registers are VM page 5: register
`0x40000000 + o` is VM address `0x280000 + o/4`).

They were all seen on **one board**, so some may vary between chips or
revisions. Each item says what the documents say, what we saw, and how
LiteForth works around it. We hope WCH finds them useful.

## The chip

### Erased flash reads 0xE339E339

**Documents:** the RM's flash chapter (46) describes erasing by sector
but doesn't say what an erased cell reads. Most MCUs read all ones, and
it's easy to assume that.

**Seen:** after `FLASH_ROM_ERASE`, every word of the erased area reads
`0xE339E339`. Code that tests for blank flash with `0xFFFFFFFF` never
finds any.

**LiteForth:** `FLASH_ERASED_WORD` in `V5F/User/flash.h`; the boot check
in `V5F/User/main.c` accepts either value.

**Suggestion:** state the erased value in the flash chapter.

### UNIID3 reads like erased flash

**Documents:** RM 39.1 and Table 39-1: the unique ID is 96 bits, in
`ESIG_UNIID1` (`0x1FFFF7E8`), `UNIID2` (`0x1FFFF7EC`) and `UNIID3`
(`0x1FFFF7F0`, "U_ID[95:64]").

**Seen:** `UNIID1` and `UNIID2` hold plausible ID values, but `UNIID3`
reads `0xE339E339`, the erased-flash pattern above, so it seems never to
have been programmed. A serial number made from all 96 bits ended in
`E339E339` on this chip.

**LiteForth:** the V3F's USB serial number uses only `UNIID1` and
`UNIID2` (16 hex digits, `Common/usb_desc.c`).

**Suggestion:** say how many ID bits are really programmed, or program
`UNIID3`.

### RCC_BDCTLR: the SDK's byte writes didn't start the LSE

**Documents:** RM 3.4.10 describes `RCC_BDCTLR` (`0x40021024`) as an
ordinary 32-bit register, with no rule about access width or about
waiting between writes. The SDK's `RCC_LSEConfig` (`ch32h417_rcc.c`)
writes it a **byte** at a time (`*(__IO uint8_t *)0x40021024 = ...`), and
`RCC_RTCCLKConfig` / `RCC_RTCCLKCmd` do read-modify-writes.

**Seen:** with backup access enabled (`PWR_BackupAccessCmd`), the usual
sequence (`RCC_LSEConfig(RCC_LSE_ON)`, wait for LSERDY,
`RCC_RTCCLKConfig(RCC_RTCCLKSource_LSE)`, `RCC_RTCCLKCmd(ENABLE)`) left
the RTC dead: the register ended up at `0x100`, RTCEN with no clock
selected and the crystal off. Once RTCEN is set
with no clock, RTCSEL can't be changed without a backup-domain reset.

Writing whole 32-bit values by hand worked at once, each one reading back
immediately: `0x10000` (BDRST), `0`, `1` (LSEON; it read `3` when the
crystal was ready), `0x41` (RTCSEL = LSE), `0x141` (RTCEN). The register
then read `0x143` and the RTC counted seconds. We didn't isolate whether
the byte write itself or one of the read-modify-writes is what fails.

**LiteForth:** `bd_write` in `V5F/User/lftime.c` writes only whole
values, one step at a time, each read back before the next; nothing calls
the SDK's BDCTLR helpers.

**Suggestion:** check `RCC_LSEConfig` on the CH32H417, or say in the RM
that `RCC_BDCTLR` must be written as a word.

### The option bytes' complement bytes are written by the chip

**Documents:** each option byte is a halfword at 0x1FFFF800 + 2n: the data
byte, and its complement in the high byte. WCH's library writes whole
halfwords, with the complement (`FLASH_ProgramOptionByteData`: `~Data << 8
| Data`) or without it (`FLASH_EnableWriteProtection` writes `0x00XX`).

**Seen:** after one erase and a programming pass of all eight halfwords,
each one read back with the complement of its data byte in the high byte,
whatever was written there: `003F` read `C03F`, `0000` read `FF00`, and
`00FF` (already complemented) read `00FF`. Seen on the spare board's
lock-down (19 October 2026).

**LiteForth:** stage 1's `lockdown` compares only the data bytes when it
reads them back. (It first compared whole halfwords, called a good write
"NOT AS WRITTEN" and didn't reset.)

**Suggestion:** say in the option bytes chapter that the hardware writes
the complement bytes, so software needn't.

## The reference manual

### Standby mode is listed as absent but referred to elsewhere

RM 2.3 says the chip has **two** low-power modes, Sleep and Stop (Table
2-1), and there is no Standby section; WCH's EVT has Sleep and Stop
examples only. But several places still describe Standby, apparently
carried over from other chips:

- 2.3.4 *RTC Auto-wakeup*: "awakened periodically from Stop or Standby
  mode".
- Chapter 6 intro: "system reset and wake-up from Standby mode do not
  have any effect on it"; Figure 6-1 marks blocks "powered in Standby"
  and shows a WKUP pin and "exit Standby mode".
- 3.4.2 `RCC_CFGR0` SW note: "when returning from Standby and Stop mode".
- 7.2.1 (IWDG): "functions in shutdown and standby modes".
- Chapter 17 (LPTIM): "all power modes except standby mode".

**LiteForth:** `standby` uses Stop mode (both cores in deep sleep, woken
by the RTC alarm on EXTI line 17), then resets the chip itself to give a
clean boot.

### 2.3.3 "Stop Mode" starts with "Sleep mode"

The section's first sentence reads "Sleep mode combines the clock control
mechanisms of the peripherals with the fact that both the V3F and V5F
cores enter deep sleep", which describes Stop mode.

### SYS_CFGR0 and the chip version used by the Stop example

RM 2.4.3 documents `SYS_CFGR0` (`0x5003C000`) with `LDO_VDDK[2:0]` (bits
6:4) allowing only `011` (rated) and `101`, everything else "Reserved",
and bits 31:7 reserved, except for an external VDD12 supply.

WCH's `PWR/Stop_Mode` example (`Common/hardware.c`,
`PWR_STOPMode_LP_Cmd`) lowers VDDK before Stop with the regulator in
low-power mode by writing `LDO_VDDK` = `001` or `000` and clearing bits
19:17. It picks the values from a chip version read from bits 7:4 of
`0x1FFFF704`, an address the RM's ESIG chapter (39) doesn't list (the
SDK's `DBGMCU_GetREVID`/`GetDEVID` read it too). Low-power Stop is
undocumented without this.

**Suggestion:** document the Stop-mode VDDK settings and the
`0x1FFFF704` ID word.

### RCC_BDCTLR LSEBYP is marked read-only

RM 3.4.10 gives LSEBYP (bit 2) access **RO**, but it's a control bit
("bypass control bit") and the SDK sets it (`RCC_LSE_Bypass`). We didn't
test the bypass.

## The SDK and examples

### WCH's SD driver and the USB-serial example both want DMA1 channel 1

`SDIO/SDIO_SD`'s `sdio.c`, in DMA mode, uses DMA1 channel 1 and waits for
the transfer to finish. `USBFS/DEVICE/SimulateCDC` uses DMA1 channel 1
for USART4 TX and channel 2 for RX (seen in the DMA registers of a
running board). The two examples can't be combined as they are.

**LiteForth:** `V5F/User/blocks.c` runs the SD transfers itself on DMA2
channel 1 through DMAMUX channel 9 (request 111, SDIO), without waiting:
CMD18/CMD25, DMA, then CMD12 and CMD13 polls. DMA2 reaches the V5F's DTCM
fine, as `MEMARY_CFGR` in the RM says. About 0.5-0.9 ms per 4 KB read.

### sdio.h lacks two prototypes

`CmdResp1Error` and `IsCardProgramming` are global functions in `sdio.c`
and are needed for a non-blocking driver, but `sdio.h` doesn't declare
them. `blocks.c` declares them itself.

### DMA_MuxChannelConfig's comment

Its comment says "x from 1 to 16" for the mux channel, but the constants
are 0-based (`DMA_MuxChannel1` is 0, `DMA_MuxChannel9` is 8). The request
is stored minus 1, which matches the RM's bit table (`0000000`: request
input 1) but isn't mentioned in the function's comment; reading the
register back shows USART4's requests 91 and 92 as `0x5A` and `0x5B`.

### Delay_Us and Delay_Ms reprogram SysTick1

The debug helpers in `SRC/Debug/debug.c` take over the V5F's SysTick
(`SysTick1`), so any time base built on it stops working after a delay
call. Worth a note in `debug.h`.

### ECDC (the AES/SM4 engine): what its registers really do

Tested from the Forth prompt through VM page 5 (ECDC at `0x40016C00`, VM
cell `0x285B00`; its clock is bit 26 of `RCC_HB2PCENR`, its reset the same
bit of `RCC_HB2PRSTR`), with single-block operations only (SGSD in, SGRT
out), on 2026-10-18. The RM and the SDK disagree with the chip on several
points:

- **CTRL bit 9 set is CTR, clear is ECB.** `ch32h417_ecdc.h` has
  `ECDCBlockCipherMode_CTR = 0, ECDCBlockCipherMode_ECB = 1`, the other way
  round. WCH's examples ask for ECB and get CTR with a zero counter, which
  their print-only demos can't show.
- **Bit 8 set is AES, clear is SM4**, as the header says.
- **With DAT_MOD (bit 13, "big") set, everything is natural big-endian
  words:** `KEY_127T96` holds key bytes 0-3 and so on down to `KEY_31T0`
  (bytes 12-15) for a 128-bit key, `IV_127T96` counter bytes 0-3, `SGSD_127T96`
  data bytes 0-3, and `SGRT_127T96` result bytes 0-3. With it clear the data
  is taken byte-reversed and the key differently again. DAT_MOD reads back
  as 0 even when set.
- **Known answers match** with DAT_MOD set: AES-128 FIPS-197 (key
  `000102...0f`, `00112233...eeff` -> `69c4e0d8 6a7b0430 d8cdb780 70b4c55a`),
  in ECB and through the counter; SM4 GB/T 32907 (`0123456789abcdef
  fedcba9876543210` as key and data -> `681edf34 d206965e 86b3e94f 536e4246`);
  and both with all-zero key and data.
- **In single-block CTR the counter neither advances nor reloads.** Each
  operation uses the counter loaded at the last key expansion (KEYEX_EN 0 to
  1); writing the IV registers afterwards changes nothing, and they read
  back unchanged.
- **RAM-to-RAM mode** (`ecdc-test`, `V5F/User/ecdc.c`): with DAT_MOD
  *clear* the engine reads and writes memory in its natural byte order
  (with it set, each 16-byte unit comes out byte-reversed), and it steps the
  counter by one per unit, so one key setup covers a 4 KB block: 24 us for
  AES and 29 us for SM4, against 2.4 ms and 0.8 ms in software. It reaches
  the V5F's DTCM. Undivided clock (`CLOCK_SELECT`); at divider 6 a transfer
  didn't finish.
- **EKDONE comes a little early.** Started right after the flag, AES got
  the last word of its first units wrong by a fixed difference (4 units at
  divider 1, 7 at divider 2), as if the last round key wasn't ready; SM4
  didn't. A pause after the flag fixes it (20 us tested; `ecdc.c` waits 5).
- **Align its buffers:** 32-byte aligned buffers worked every time; the
  first check on plain `uint32_t` arrays (4-byte aligned) failed, and the
  board hung a little later, as if the engine had written outside them.
  With the check's arrays aligned it passed, so `ecdc.c` copies each block
  into an aligned buffer (112 us a block in all, against 30 us of AES).
- **Key expansion isn't reliable without a reset.** After a reset through
  `RCC_HB2PRSTR` it ran every time (`INT_FG` bit 16, EKDONE, set). Without
  one, successive expansions alternated between running and silently not
  running; when one doesn't run, the engine keeps the previous key (and
  algorithm) and still loads the new counter. Reset the engine before each
  new key, and check EKDONE.
- **The flags are bits 16-18 of `INT_FG`** (EKDONE, single done, RAM done),
  cleared by writing 1s there: writing `0xFF`, as `ECDC_Init` does, clears
  none of them. In the SDK, `ECDC_IT_*` are cast to `uint8_t`, which makes
  them all 0, and `ECDC_ITConfig` clears with a `uint8_t` mask, so neither
  can work.

### Unused peripheral addresses read as 0, without a fault

Reads from the V5F at the middle of three holes in the peripheral map
(`ch32h417.h` has nothing at 0x4000C000, 0x4001C000 or 0x4002C000) gave 0
and raised no exception; UHSIF at 0x40038000, for comparison, read
E71618B3. Tried from Forth on the main board at `SECURITY_LEVEL` 0
(2026-10-20), where VM page 5 maps 0x40000000..0x40038400 unfiltered:

```
283000 @ .  0H        ( 0x4000C000 )
287000 @ .  0H        ( 0x4001C000 )
28B000 @ .  0H        ( 0x4002C000 )
28E000 @ .  E71618B3H ( 0x40038000, UHSIF )
```

So an `@` in the I/O page can't raise a CPU exception there, and the VM
rejects anything past the page with -9 before the CPU reads it. The trap
routing (`vmTrap`, the V5F's `HardFault_Handler`) is then mainly for faults
in the C code of API words during a VM run. Writes to the holes weren't
tried.

## The nanoCH32H417 board

- **VIO18 for the LCD connector.** VIO18 supplies the high-speed GPIOs,
  including the LCD's SPI2 pins (PB13/PB15). At the board's hardware
  setting SPI2 ran but the ST7789 module never
  saw valid high levels; setting VIO18 to 3.3 V in software
  (`PWR_VIO18LevelCfg(PWR_VIO18Level_MODE3)`), as the board's `SPI_LCD`
  example does, fixed it.
- **VBAT is not connected.** The RTC keeps time through resets and Stop,
  but loses it when the board loses power.
- **Some microSD cards brown the board out.** With a new SanDisk Ultra
  256 GB SDXC card (A1), an
  8-sector read of one particular spot on the card reset the whole chip
  every time, partway through the transfer. Other sectors read fine, and
  so did the same code on an older card. The reset left no fault behind:
  `RCC_RSTSCKR` showed the power-down flag (PORRSTF, with SFTRSTF also set),
  and breadcrumbs kept in the shared SRAM showed the last step was the
  read command, with the data still arriving. Large cards can draw sharp
  current peaks while they read. When feeding 3.3 V to a microSD slot, give
  the card a supply that can take them: a regulator with headroom, and bulk
  plus local decoupling capacitance right at the slot.

## What matched the documents

Some behavior we checked against the RM and found exactly as described,
which may save the next person some time:

- `PFIC_CSTAR0` bits 15:14 show the V3F's sleep state (`10` deep sleep),
  which the V5F waits for before entering Stop.
- Stop mode needs both cores in deep sleep, and the RTC alarm on EXTI
  line 17 wakes it through `WFE`.
- The backup domain (RTC counter, `RCC_BDCTLR`) survives the reset pin
  and `NVIC_SystemReset`.
- `STK_ISR` is shared: bit 0 is SysTick0 (V3F), bit 1 SysTick1 (V5F).
- The DMAMUX request numbers in Table 10-2 are right (SDIO = 111).

## Privilege modes and PMP

Found with `pmp-test` on the V5F (details in
[firmware-updates.md](firmware-updates.md) section 8):

- WCH's start-up code (`startup_ch32h417_v5f.S`, and the V3F's) writes
  `mstatus = 0x6088` before its `mret` into `main`. MPP is 0, so `main`
  runs in **user mode**, not machine mode: any machine CSR (`pmpcfg0`,
  `mtvec`, `mstatus`...) is an illegal instruction there (mcause 2).
  Machine mode runs only in trap handlers.
- An `ecall` from user mode goes to vector 8, `Ecall_U_Mode_Handler`;
  from machine mode, vector 5, `Ecall_M_Mode_Handler`. Not to
  `HardFault_Handler`. Both default to a weak handler that loops forever.
- After reset `pmpcfg0` is 0 but the `pmpaddr` registers hold junk.
- A locked PMP entry applies to machine mode.
- A fault inside a trap handler resets the chip with LOCKUP rather than
  being taken.
- MPRV seems to be ignored: with it set and MPP = user, machine-mode loads
  and stores weren't checked as user mode.

