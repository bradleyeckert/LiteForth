# Hardware security features for the CH32H417 and its successors

*A note from the LiteForth project to the engineers at Nanjing Qinheng
Microelectronics (WCH).*

## Why we're writing

We built a root of trust for firmware updates on the CH32H417. The dual
QingKe cores map well onto the job: the V3F runs stage 1 (secure boot,
update install, rollback) and holds the keys, and the V5F runs the
application (a Forth system) in user mode, fenced from the V3F's secrets by
PMP. The design and its reasoning are in `doc/admin/firmware-updates.md`
(section 8) and `doc/quality/`; chip behaviour that differed from the
reference manual is in `doc/ch32h417-notes.md`.

Doing this exposed the edges of what the hardware lets software enforce.
Several of our security properties rest on software scaffolding that works,
but that we would gladly delete if the silicon enforced the property
directly. Each item below is one such place: **what we needed**, **what we
had to do without it**, **what hardware would help**, and **why it matters**.
These are meant as constructive suggestions from a real hardening effort, not
as defects — the CH32H417 is a capable part, and most of this would simply
raise the floor for every security-sensitive customer.

Where a feature may already exist and we missed it, please read the item as a
request to document it clearly: we found the security-relevant behaviour of
the PMP, the PFIC, the option bytes and the bus masters only by experiment.

## The recommendations, most impactful first

### 1. Per-bus-master memory protection (an IOPMP)

**What we needed.** PMP fences the *CPU*. It does nothing about the other bus
masters — DMA1/DMA2, the SDIO controller, USBFS. On this chip a user-mode
program that can program a DMA channel can make that channel read or write
anywhere, including the V3F's RAM and the flash controller, entirely around
PMP. That single gap would undo the whole isolation model.

**What we did without it.** We built "Guarded DMA" (`V5F/User/gdma.c`): user
mode cannot touch the DMA registers at all (they sit behind PMP entry 2,
read-only), and the *only* way it moves data by DMA is an `ecall` into a
machine-mode service that owns a fixed channel table, points the channel at
one 16 KB machine-mode-owned buffer, checks every user buffer against the
allowed RAM ranges, and "burns" the channel after each transfer. We also
keep the DMA clock off for user mode (an RCC-store guard) so no master can be
started behind our back. This is a lot of code and a permanent performance
cost on every block transfer, and it only covers the masters we thought of.

**What to add.** A memory-protection unit on the bus fabric, keyed by master
ID — the RISC-V **IOPMP** is exactly this. Each master (each DMA controller,
SDIO, USB) gets its own protection table, so a DMA engine is confined to the
regions its owner is allowed to reach, in hardware, regardless of what
software programmed the channel to do.

**Why.** This is the single biggest structural gap. PMP-based CPU isolation
is only as strong as the weakest bus master that bypasses it. An IOPMP would
let us delete Guarded DMA and would make every DMA-capable peripheral safe by
construction rather than by our vigilance.

### 2. A dedicated machine-mode interrupt stack (hardware trap-stack switch)

**What we needed.** When a trap or interrupt is taken, the hardware fast-
interrupt prologue (HPE) pushes context using the *current* stack pointer.
User mode controls its own `sp`, so it can point `sp` at a peripheral's
registers (or any address) and then take a fault — and the machine-mode
handler's hardware-stacked context lands there, a write that PMP entry 2
would otherwise have refused.

**What we did without it.** Our `HardFault_Handler` (`V5F/User/ch32h417_it.c`)
is `naked` and swaps `sp` with `mscratch` (a 1 KB machine-mode stack we carve
in DTCM and set in the start-up code) *before it stores anything*. That
closes the hole for the fault handler, but the hardware-vectored fast-
interrupt handlers still stack at user mode's `sp` through the HPE prologue,
which we cannot intercept — so that part of the property is still open (see
`SYS-SEC-11` in `doc/quality/system.md`).

**What to add.** A machine-mode scratch stack-pointer that the hardware swaps
in on a trap from a less-privileged mode — the RISC-V `mscratchcsw`/
`mscratchcswl` idiom, or simply an option bit that makes the HPE prologue
stack on a machine-mode-private pointer when entering M-mode from U-mode.

**Why.** Without it, *every* trap and interrupt entry is a potential
user-steered machine-mode write. We can hand-patch the one handler we write
in C, but not the hardware-vectored ones. A hardware trap-stack switch makes
the whole class safe and removes a subtle, easy-to-get-wrong workaround.

### 3. More PMP entries, and machine-mode memory protection (ePMP / Smepmp)

**What we needed.** We have four PMP entries and had to make them do a lot:
entry 0 (TOR) locks the low flash window, entry 1 (NAPOT) fences the V3F's
RAM, entry 2 (NAPOT) covers one peripheral block read-only, and entry 3 is
the catch-all for everything else. We had no entries left for finer policy
(for example, fencing the interrupt controller needed us to *shrink* the
catch-all rather than add an entry), and we rely on the subtlety that an
*unmatched* region is machine-mode-only.

**What we did without it.** We packed policies to fit four entries and
documented the reasoning carefully so a future change doesn't silently lose a
protection. We lock every entry so the configuration can't be unwound, and we
read the PMP back at boot (`pmp_report`) to prove it took.

**What to add.** More PMP entries (8 or 16), and **ePMP (Smepmp: `mseccfg`
with `MML`/`MMWP`/`RLB`)**. ePMP's machine-mode-whitelist and machine-mode-
write-protect bits would let us make machine-mode code and data unreachable-
and-unmodifiable with intent, rather than relying on unmatched-region
defaults and lock bits. It would also give defence in depth for the root of
trust itself, not just for user mode.

**Why.** Four entries force uncomfortable trade-offs and leave no room for
layered policy. ePMP is the standard RISC-V answer to "protect machine mode
from itself and from supply-chain-compromised M-mode code," which is exactly
the threat our part 4 (a root signature over machine-mode code) is trying to
mitigate in software.

### 4. A finer peripheral address map, or a peripheral protection unit

**What we needed.** We wanted user mode to *read* RCC (so it can see clocks)
but never touch the DMA controllers, the flash controller, USBFS or the RNG.
On this chip those live together in one 16 KB region (0x40020000–0x40023FFF:
DMA1, DMA2, DMAMUX, RCC, the flash controller, CRC, USBFS, RNG). A single PMP
entry can only give one policy to the whole block.

**What we did without it.** We gave the whole block read-only to user mode
(PMP entry 2) and then emulate the few writes user mode legitimately needs
(to RCC and CRC) in the fault handler, decoding the faulting store and
allowing it only if it targets RCC or CRC (`V5F/User/guard.c`). Emulating
instructions in a fault handler to approximate per-register permissions is
not where anyone wants to be.

**What to add.** Either place the sensitive controllers (DMA, flash, RNG,
USB) on their own naturally-aligned regions well separated from the
clock/reset registers, or add a small peripheral protection unit that gates
register blocks by privilege/master. Even aligning each controller to its own
4 KB page would let PMP separate them.

**Why.** Good isolation needs the address map to be *separable*. When
unrelated controllers share a protection granule, every policy becomes
all-or-nothing and pushes software into instruction emulation.

### 5. A privilege-aware interrupt controller (PFIC)

**What we needed.** The PFIC and the system timers sit in the core-private
region (from 0xE0000000). User code that can reach the PFIC can change
interrupt enables, priorities and — through the VTF (hardware-vectored)
registers — redirect an interrupt to code of its choosing that then runs with
whatever privilege the vector implies.

**What we did without it.** We moved the whole region out of user mode's
reach with PMP (shrinking the catch-all entry to the low 2 GB, so 0xE0000000
up is machine-mode-only), moved every interrupt enable into the start-up code
in machine mode, and turned `reboot` and `standby` into machine-mode `ecall`
services because they touch the system-control register. It works, but it
means user mode cannot configure *any* of its own interrupts — the start-up
code has to know them all in advance.

**What to add.** A privilege split in the PFIC: the enable/pending/priority
state for an interrupt follows the privilege it is delivered at, and the VTF
and allocation registers are writable only from machine mode. Ideally user
mode can enable *its own* interrupts without being able to touch machine
mode's or to redirect vectors.

**Why.** "Fence the whole interrupt controller off from user mode" is a blunt
instrument we were forced into. A privilege-aware controller would let a
user-mode application manage its own interrupts safely, which is a normal
thing to want, without handing it the keys to the machine.

### 6. A verified boot ROM and a root key in OTP/eFuse

**What we needed.** A root of trust needs an immutable first instruction and
an immutable public key to verify against. On this chip the first code that
runs is in flash, and the key material we check against lives in flash and in
a device record we build — all of which is only as immutable as the option-
byte write protection we set.

**What we did without it.** Stage 1 is software in the V3F (`src/update.c`,
`V3F/User/stage1.c`): it verifies an Ed25519 signature over each update, and
we lean hard on the option bytes (read protection, write-protect masks, boot
source) to keep stage 1 and its records from being rewritten. The chain's
root is therefore a flash region we protect, not a hardware anchor.

**What to add.** A small mask-ROM first-stage bootloader that verifies the
next stage against a public-key hash burned into **OTP/eFuse**, with the fuse
readable by software but never writable after lock. Equivalently: a hardware
secure-boot option that will only execute flash whose signature matches a
fused key.

**Why.** Software secure boot can protect against a remote attacker with an
update channel, but not against someone who can rewrite flash and the option
bytes. A fused root key and a ROM that enforces it move the trust anchor from
"flash we hope stays protected" to "silicon that cannot be changed."

### 7. Hardware key storage for the crypto engine, and a unique device secret

**What we needed.** Keys that the AES/SM4 engine can *use* but that software
(even compromised machine-mode software) cannot *read*, and a per-die secret
to derive device identity from.

**What we did without it.** The ECDC engine (`V5F/User/ecdc.c`) is driven with
keys that live in RAM while in use, so any code that can read that RAM can
read the key. For device identity we derive a token from the RNG at
provisioning and store it; there is no hardware-rooted per-die secret.

**What to add.** A key store with write-only key slots feeding the crypto
engine (load or derive a key into a slot; the engine uses the slot; software
cannot read it back), and a factory-programmed **unique device secret** in
OTP usable only as a key-derivation input. Binding the UID to a secret, not
just a serial number, would also let us bind data and updates to a specific
die.

**Why.** Today a single memory-disclosure bug anywhere privileged exposes
every key. Hardware key slots make key *use* and key *disclosure* separate
capabilities, which is the whole point of having a crypto engine on the die.

### 8. Authenticated, tiered debug access

**What we needed.** A way to ship a locked device whose debug port is closed
to attackers but can still be opened by the vendor for returns/analysis,
without that same path being an unlock for anyone with a programmer.

**What we did without it.** We rely on the option bytes to disable read-out,
and we treat the debug tooling as hostile: the WCH-Link can unprotect-and-
erase, so our provisioning refuses to flash a chip whose read protection is
on, and we never unprotect. The only "re-open" is a mass erase, which is
destructive. Our MCP tooling even refuses to flash unless read protection is
already off (`scripts/lfmcp.py`), to avoid an accidental unprotect-erase.

**What to add.** A debug-authentication mechanism: a challenge-response (ECDSA
over a device nonce) that unlocks debug only for a holder of the vendor key,
with levels (open / closed / closed-but-authenticated-unlock /
permanently-closed). The ARM ADIv6 debug authentication and the RISC-V debug
security extension are the models.

**Why.** Right now "secure" means "debug off, re-openable only by destroying
the contents." That is safe but makes field diagnosis impossible and tempts
people to ship with debug on. Authenticated debug gives both security and
serviceability.

### 9. Anti-rollback and robust, fine-grained flash protection

**What we needed.** To stop an attacker (or a buggy update) from reinstalling
an older, vulnerable-but-validly-signed image, and to protect stage 1 and its
records at a finer grain than the whole image.

**What we did without it.** We enforce a minimum version in the signed update
header and check it in `src/update.c`, and we carve the write-protect masks by
hand so the slots line up with protectable regions. A monotonic version in
software can be rewritten by anyone who can rewrite the flash it lives in.

**What to add.** A monotonic counter in OTP (a few fuse bits, or a dedicated
anti-rollback counter) that the ROM/stage-1 can compare against, and flash
write-protection with enough regions (and a lock that survives reset) to
protect stage 1, its records and the application slot independently.

**Why.** Anti-rollback is only meaningful if the "lowest allowed version" is
itself immutable. A hardware monotonic counter is the standard anchor for it.

### 10. Fault-injection and glitch detection on the boot/verify path

**What we needed.** Confidence that a single voltage or clock glitch can't
skip the signature check or the PMP lock during boot.

**What we did without it.** Nothing in hardware. We write the critical checks
defensively (verify, then re-verify state; read the PMP back after locking),
but double-checking in software does not stop a well-placed glitch.

**What to add.** Voltage and clock glitch detectors, and a brown-out/clock-
integrity monitor that faults into a safe state, especially armed during
boot verification. A redundant/mirrored representation of the security-level
and lock state would also help.

**Why.** Fault injection is the standard attack against secure boot on cheap
MCUs. A few detectors raise the cost of that attack by orders of magnitude.

### 11. A hardware inter-core memory firewall

**What we needed.** The V3F's RAM and secrets must be unreachable from the V5F
core, and vice-versa where appropriate — a property of the *system*, not of
whichever core happens to have set its PMP correctly.

**What we did without it.** We fence the V3F's RAM from the V5F's CPU with a
locked PMP entry on the V5F, and we keep the V3F's image and records below a
locked TOR entry. This depends on the V5F's own PMP being set and locked
correctly at every boot; there is no independent enforcement.

**What to add.** A bus-level firewall that makes one core's (and its masters')
address range inaccessible to the other core, configured once by the trusted
core and not dependent on the untrusted core's cooperation. (This overlaps
with the IOPMP in item 1 — a per-master unit that also distinguishes the two
cores' masters would cover both.)

**Why.** In a root-of-trust-plus-application split, the application core is
the untrusted one. Its isolation should not be enforced *by* the untrusted
core's own configuration.

### 12. A TRNG with built-in health tests and a clear status interface

**What we needed.** Keys only from a healthy entropy source, with a reliable
way to know the source is healthy.

**What we did without it.** We read the RNG words and check in software for
data-ready, seed and clock errors and a stuck/repeat value, hashing several
samples together, and refuse to make a key on any failure (`SYS-SEC-12`). This
is us re-implementing health tests the hardware should own.

**What to add.** A TRNG with continuous health tests to a recognised standard
(NIST SP 800-90B / AIS-31), a sticky health-fail flag, and an output that
stalls rather than returning low-entropy data when unhealthy.

**Why.** Entropy health is subtle and easy to get wrong in software. A
certified TRNG with a trustworthy status bit is one less thing every customer
has to re-derive, and one less place to get device keys wrong.

## Smaller things we noticed

- **`pmpaddr` loses its top two bits.** Writing `0xFFFFFFFF` to a `pmpaddr`
  reads back `0x3FFFFFFF`; the core doesn't keep address bits 33:32. It didn't
  bite us (we use `0x0FFFFFFF`/`0x1FFFFFFF`), but it surprised us and isn't in
  the manual. Either keep the full width or document it.
- **DMA access to ITCM/DTCM.** We considered using `MEMARY_CFGR` to deny DMA
  access to the tightly-coupled memories and left it out because its behaviour
  wasn't clear. A documented, per-master TCM access control would be useful
  (and overlaps with item 1).
- **Peripheral registers that read as 0 without faulting.** Reads of some
  unused peripheral addresses return 0 rather than faulting (see
  `doc/ch32h417-notes.md`), which makes "is this peripheral present/clocked"
  harder to detect defensively.
- **Document the security-relevant defaults.** The reset state of the PFIC
  allocation registers, the SysTick ownership between cores, and which bus
  masters are clocked at reset all matter to a hardening effort and were
  learned by experiment.

## In short

The CH32H417 gave us enough — two cores, PMP, a crypto engine, option-byte
protection — to *build* a root of trust, and we're glad of it. Almost all of
the fragile parts of that build come down to three missing hardware
capabilities: **memory protection for the bus masters, not just the CPU**
(items 1, 11); **a hardware trust anchor** — fused root key, verified boot,
key storage, anti-rollback (items 6, 7, 9); and **privilege-aware core
peripherals** — the interrupt controller and a hardware trap-stack switch
(items 2, 5). Any one of them would let us delete a layer of software that
today stands between an attacker and the keys.

We'd be glad to talk through any of this in detail, and to share the specific
code and the test evidence (`doc/admin/board-tests.md`) behind each point.

— The LiteForth project
