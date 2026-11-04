# Board tests: ESP32-C5

The record of security tests run on a real ESP32-C5 board, kept as evidence
for the cybersecurity technical file ([security-plan.md](security-plan.md)),
in the same form as the CH32H417's
[board-tests.md](../../CH32H417/admin/board-tests.md): what was tested, the
firmware and keys involved, how it was run, what was expected, and what the
board printed, word for word.

**No board yet.** The NM-CYD-C5 target builds under ESP-IDF
([esp32c5-notes.md](../esp32c5-notes.md)) but has not been run on hardware,
so there are no entries. The on-target security tests the plan calls for
(secure-boot enforcement, flash-encryption readback refusal, anti-rollback,
download-mode closed) are listed in
[security-plan.md](security-plan.md) section 7 and will be logged here, as
`Bnn` entries, once a board is provisioned. Until then the ESP32-C5
V-model's evidence is the build-time security gate (`scripts/ESP32C5/seccheck.py`,
`unit_tests/seccheck`) and source inspection.

## The setup

| | |
|---|---|
| Board | NM-CYD-C5 ("Cheap Yellow Display", ESP32-C5, 16 MB flash, 8 MB PSRAM) |
| Console | CH340 USB-UART (UART0), auto-reset for `idf.py flash` |
| Status | not yet provisioned; secure boot / flash encryption not burned |
