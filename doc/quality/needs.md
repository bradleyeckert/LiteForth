# Stakeholder needs (UN)

The top of the V's left side: what the people who build, ship, use and
service a LiteForth product need from it, in their terms. Each need is
refined by system requirements ([system.md](system.md)) and validated on a
real board ([board-tests.md](../admin/board-tests.md)) or by a
demonstration. [README.md](README.md) explains the format.

The stakeholders: the **developer** (writes the Forth application), the
**vendor** (releases and supports the product, holds the keys), the
**operator** (uses a device in the field), the **service technician**
(updates and recovers devices), the **provisioning operator** (makes new
boards into products), the **data owner** (receives the recorded data on
a server), and the **auditor** (assesses the product's security
for a regulator or a customer).

### UN-01: Interactive Forth on a cheap board
The developer writes and tests Forth on the target board itself, over a
USB terminal: typing words, compiling into flash, loading source from
blocks, and seeing errors where they happen.

- **Verified by:** board:B24, board:B26
- **Status:** verified

### UN-02: The same system on a PC
The developer runs the same Forth, with the same words, storage and
update code, on a PC without the board, to develop and to test.

- **Verified by:** demo:scripts/regression.f::T{, demo:src/target/desktop/unit_tests/exfat::run_lf: lf finds lfblocks.fb4
- **Status:** verified

### UN-03: Applications that run on their own
A Forth application starts at power-up and keeps running when it hits an
error, while the terminal stays usable for the developer or technician.

- **Verified by:** board:B24, demo:src/target/desktop/unit_tests/appstep::yeet handler tests passed, board:B27
- **Status:** verified

### UN-04: Source and data on a card a PC can read
The developer keeps source, fonts and data in a block file on the
microSD card, which any PC can read and write without special tools.

- **Verified by:** board:B21, board:B26
- **Status:** verified

### UN-05: Text on small graphic displays
The application shows text, including Chinese, on small color LCDs.

- **Verified by:** demo:src/target/desktop/unit_tests/lcd::make demo
- **Status:** partial
- **Gap:** no board run logged

### UN-06: Clock and low-power sleep
The application keeps the time of day, and sleeps at low power until an
alarm wakes it.

- **Verified by:** demo:src/target/desktop/unit_tests/rtc/run.sh, board:B27
- **Status:** verified

### UN-07: Updates that never brick a device
The service technician updates a device's firmware and application in the
field, over the USB terminal. No power cut, bad file or broken image
leaves the device unusable, and a device that won't start can be
recovered over USB.

- **Verified by:** board:B3, board:B4, board:B5, board:B5a, board:B6, board:B10, board:B11, board:B19
- **Status:** partial
- **Gap:** power cuts during an update are tested on the desktop and the simulated board, not on a board

### UN-08: Only the vendor's firmware runs
On a production device, only firmware and applications the vendor
released start, and tampering with the flash doesn't survive a reset.

- **Verified by:** board:B25, board:B11
- **Status:** partial
- **Gap:** a changed image refused is shown on the desktop and the simulated board, not on a board (a locked board's flash can't be changed by the debugger)

### UN-09: Secrets that can't be extracted
A device's keys can't be read out, even by someone with the board and a
debug probe; such a person can at most erase the device.

- **Verified by:** board:B8, board:B18, board:B22, board:B23
- **Status:** partial
- **Gap:** bus masters and machine mode's data aren't fenced (SYS-SEC-01, SYS-SEC-11)

### UN-10: Recorded data only the data owner can read
The device records audio from its CODECs to the card. Only the data
owner's server can read a recording, the server can prove which device
made it and that nothing was changed, the device's identity isn't
visible on the card, and a power cut loses at most what was being
written.

- **Verified by:** demo:src/target/desktop/unit_tests/datalog/test_datalog.py
- **Status:** partial
- **Gap:** the board side (stage 1's key service, the Forth words, I2S capture) isn't built; no board run

### UN-11: Convenient development, locked production
One code base serves both: development boards reflash at will and reach
every peripheral; production boards are locked down. The difference is a
build setting, not a fork.

- **Verified by:** board:B7, board:B20, board:B25
- **Status:** verified

### UN-12: Evidence for an assessment
The auditor gets a bill of materials, a record of the security tests with
their results, and the trace from each requirement to its tests.

- **Verified by:** demo:sbom.spdx.json, demo:doc/admin/board-tests.md, demo:scripts/rtm.py::Requirements Traceability Matrix
- **Status:** verified

### UN-13: Tools on the PC
The developer and vendor prepare blocks, fonts, card images, keys and
packages, and drive a board, from ordinary PC scripts; an AI assistant
can use the board's terminal and debug probe safely.

- **Verified by:** board:B25, board:B26
- **Status:** verified

### UN-14: Provisioning new boards
The provisioning operator turns blank boards into products with a script:
production firmware, the chip locked down, the board's own key made and
recorded, its card prepared and the current release installed, without
holding a private key, and with a record of each board.

- **Verified by:** demo:src/target/desktop/unit_tests/stage1sim::provision.py on the simulated board: passed
- **Status:** partial
- **Gap:** no board run: provision.py has run on simulated boards only

### UN-15: Evidence the enclosure was opened
A fielded device records when its sealed enclosure is opened, so that
opening is evident afterwards even if the enclosure was closed again.
Opening may be legitimate -- replacing the backup battery, say -- so the
device only logs the event and takes no defensive action. The record
survives a loss of main power.

- **Status:** planned
- **Gap:** not implemented; the current boards have no enclosure tamper switch
