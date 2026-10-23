## What and why

<!-- One paragraph: what changes, and the need or requirement behind it. -->

## Requirements (doc/quality)

<!-- The items this changes or verifies: SYS-UPD-03, MOD-UPDATE, ...
     New behavior: a new or changed item. No item covers it? Say why. -->

- Items:
- `scripts/rtm.py` run and `doc/quality/rtm.md` committed: [ ]

## Verification

- [ ] Unit tests for the modules changed
- [ ] Integration or system tests for the requirements changed
- [ ] `make test` passes
- [ ] CH32H417 code: `scripts/ch32build.sh V3F` and `V5F` pass
- [ ] Board: run logged in `doc/admin/board-tests.md` as Bnn (or: not needed, because ...)
- [ ] Security mechanism changed: independent review done, each finding fixed with a test
