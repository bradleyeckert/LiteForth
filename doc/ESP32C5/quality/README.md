# Quality: the ESP32-C5 V-model and traceability matrix

LiteForth on the ESP32-C5 is security-relevant firmware, and the user's
direction is that cybersecurity be built in from the beginning rather than
bolted on ([security-plan.md](../admin/security-plan.md)). So this target
gets the same V-model and Requirements Traceability Matrix (RTM) as the
CH32H417: stakeholder needs, system requirements, architecture and modules,
each item naming its parent and the evidence that verifies it, checked
against the repository by `scripts/rtm.py` at every `make test`.

This folder is the ESP32-C5's own V. The format, the evidence kinds and the
rules are exactly those the CH32H417 V-model documents, so rather than
repeat them, **read
[../../CH32H417/quality/README.md](../../CH32H417/quality/README.md)** for
the conventions. What follows is only what differs for this target.

## 1. The V, on this target

```
  Stakeholder needs (UN) ............................ Validation
  needs.md                                            board tests (ESP32-C5),
     \                                                demonstrations
      System requirements (SYS) .................. System tests
      system.md                                    the shared core on the desktop,
         \                                         board tests, the security gate
          Architecture (ARC) .................. Integration / inspection
          architecture.md                       the IDF app, drivers, the
             \                                  boot/encryption chain, SOUP
              Modules (MOD) ................ Unit tests
              modules.md                     unit_tests/seccheck, the drivers
                 \                          /
                  Implementation: src/target/ESP32C5/, scripts/ESP32C5/, src/
```

## 2. What differs from the CH32H417 V

- **The board is not here yet.** The target builds under ESP-IDF but has
  not run on hardware, so most items are `planned` or `partial`, and
  `board:` evidence is absent — [../admin/board-tests.md](../admin/board-tests.md)
  is a stub. The honest present state is: scaffolding and drivers that
  compile against real IDF headers, a security plan, and a build-time
  security-configuration gate that runs in CI today.

- **Security is the spine.** Where the CH32H417 builds its own root of
  trust, this target adopts ESP-IDF's (Secure Boot v2, flash encryption,
  anti-rollback, OTA) and treats ESP-IDF as SOUP with its own controls.
  The `SEC` system requirements and the `ARC-SEC*` elements carry that,
  and the one piece already enforced automatically is the configuration
  baseline: `scripts/ESP32C5/seccheck.py`, exercised by
  `src/target/desktop/unit_tests/seccheck` and gated in CI
  (`ci:seccheck`).

- **The matrix is generated separately** from the CH32H417's, into
  [rtm.md](rtm.md), because the two targets have different needs and
  different board-test logs:

  ```
  scripts/rtm.py --dir doc/ESP32C5/quality \
                 --out doc/ESP32C5/quality/rtm.md \
                 --board-tests doc/ESP32C5/admin/board-tests.md
  ```

  `unit_tests/rtm` runs this for both targets, so a stale `rtm.md` or a
  broken link in either fails CI. The evidence suites
  (`unit:`/`ci:`/`demo:` targets) live under the shared
  `src/target/desktop/unit_tests` tree, as on the CH32H417.

## 3. The files

| File | Level | Holds |
|---|---|---|
| [needs.md](needs.md) | UN | What the developer, vendor, operator, technician and auditor need from an ESP32-C5 LiteForth product |
| [system.md](system.md) | SYS | What the product must do: the REPL and storage, and the security, update and process requirements |
| [architecture.md](architecture.md) | ARC | The IDF application, the driver layer, the partition/OTA layout, the secure-boot chain, the SOUP controls, key storage |
| [modules.md](modules.md) | MOD | The target's C sources and the security-gate script, with their unit evidence |
| [rtm.md](rtm.md) | — | The generated matrix (do not edit by hand) |
