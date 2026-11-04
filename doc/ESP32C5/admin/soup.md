# ESP-IDF as SOUP: the bill of materials and CVE monitor

This is the working detail behind [security-plan.md](security-plan.md)
section 4: ESP-IDF is software of unknown provenance (SOUP) that ships more
third-party code inside it and reaches the network, so it carries its own
controls, independent of LiteForth's code. Two of those controls are built
and automated now — an SBOM of the pinned SOUP, and a CVE monitor against it;
this document describes them and how to keep them current.

The existing `scripts/mksbom.py` / `sbom.spdx.json` cover the third-party code
**in this repository** (Monocypher, blake2s, WCH's EVT, …). ESP-IDF is not in
the repository — it is a pinned external toolchain — so its SBOM is generated
from a manifest rather than by hashing files. The two SBOMs are complementary.

## 1. The pinned manifest — `src/target/ESP32C5/soup.lock.json`

The single source of truth, maintained by hand like `mksbom.py`'s component
list. It records:

- **The framework:** the exact ESP-IDF version (**v5.5.6**), its git commit,
  the toolchain version, and the lockfiles (`dependencies.lock`,
  `managed_components/`) that pin a reproducible image (plan §4.1).
- **Each bundled component:** name, version, upstream, licence, a CPE and/or
  purl for CVE matching, and — the part that matters most (plan §4.2) — its
  **`linked`** state:

  | `linked` | meaning |
  |---|---|
  | `app` | linked into the application image |
  | `bootloader` | linked into the 2nd-stage bootloader (the secure-boot path) |
  | `unlinked` | present in ESP-IDF but garbage-collected out of the current (milestone-1, no Wi-Fi/BLE) image |

  The milestone-1 image has **no Wi-Fi/BLE**, so `lwip`, `wpa_supplicant`,
  `zlib` and `tinyusb` are *unlinked* — present in the framework but not in the
  binary, so not attack surface yet. They become `linked` when the feature
  that needs them is turned on, and the CVE monitor's verdict changes with
  them. The genuinely linked SOUP today is ESP-IDF's runtime, the FreeRTOS
  kernel, newlib, and (in the bootloader) mbedTLS for signature verification.

Fields marked `CONFIRM`/`confirm` (the git commit, toolchain, and a few
versions) must be checked against the installed IDF. `soup_sbom.py
--verify-idf $IDF_PATH` reads the real versions from the install and warns on
drift; the manifest stays the source of truth.

### Keeping it current

- **Bumping ESP-IDF** (e.g. for a security fix): change `framework.version`
  and the affected component versions, run `soup_sbom.py`, and run `cvecheck.py`.
  The regression gate (the full build + `seccheck.py` + the on-board checks)
  must pass before the bump ships, and the field path is an OTA release
  (plan §4.5). Record the trigger (which advisory) in the commit.
- **Turning on a feature** (Wi-Fi, TLS, USB): flip the relevant components
  from `unlinked` to `app`, regenerate, and re-run the monitor — the newly
  linked surface is now gated.

## 2. The SBOM — `scripts/ESP32C5/soup_sbom.py`

Reads the manifest and writes `src/target/ESP32C5/soup.spdx.json`, an SPDX 2.3
document: each component a package with its licence, version, CPE/purl and a
`DEPENDS_ON` (linked) or `OPTIONAL_DEPENDENCY_OF` (unlinked) relationship to
the product. The creation time is the repository's last commit time, so the
file changes only when the manifest does.

```
soup_sbom.py              write soup.spdx.json
soup_sbom.py --check      fail if it is out of date (CI, unit_tests/soup)
soup_sbom.py --list       the components and their linked state
soup_sbom.py --verify-idf $IDF_PATH   check versions against an install
```

So a CVE against any bundled component can be matched to this device, and the
SBOM can be handed to an assessor as the "what is in it" evidence.

## 3. The CVE monitor — `scripts/ESP32C5/cvecheck.py`

Matches the pinned versions against advisories and flags any affecting a
**linked** component — an unpatched, networked SOUP CVE being the
highest-probability finding for an automated attacker (plan §4.4). Two feeds:

- **Offline backstop** — `src/target/ESP32C5/advisories.json`, a curated,
  dated list of advisories with affected/fixed version ranges. It always runs,
  with no network, so CI is deterministic (`unit_tests/soup`). Add confirmed
  advisories here with their fix status.
- **Live feed** — `cvecheck.py --online` queries [OSV](https://osv.dev) for
  each component with a purl, merges the results, and degrades gracefully to
  the backstop if the network is unavailable. This is the standing monitor,
  run weekly by `.github/workflows/soup-cve.yml` (and on a change to the pins
  or the backstop), where a hosted runner has the internet.

```
cvecheck.py            offline check against advisories.json
cvecheck.py --online   also query the OSV feed
cvecheck.py --strict   fail on unlinked matches too (not just linked)
cvecheck.py --list     what is monitored
```

**Verdict:** a match on a linked component is a `FAIL` and exits nonzero (fails
the gate / the scheduled run); a match on an unlinked component is reported as
*informational* — it is not in the current image — unless `--strict`. The fix
for a real `FAIL` is to pin a patched IDF/component (§1) or, if genuinely not
exploitable here, record the exception in `advisories.json` with the reasoning.

The seed `advisories.json` carries two long-fixed zlib CVEs as worked examples:
they are past the pinned zlib 1.3.1 (and zlib is unlinked), so the monitor is
clean — while still exercising the matcher end to end.

## 4. What this does and doesn't cover

- **Covers:** knowing exactly what SOUP is in the device and at what version
  (SBOM), and a standing check that none of the *linked* SOUP has a known
  unpatched CVE (monitor), both automated and in CI.
- **Doesn't cover yet:** the *field patch path* — an OTA release that actually
  delivers a bumped IDF to devices — which depends on the OTA mechanism
  (`ARC-OTA`, still to build). Until then the monitor tells us *when* to cut a
  release; the release mechanism itself is the open item (plan §4.5, §7.5).

Traceability: `SYS-SEC-06` (SOUP controls) and `ARC-SOUP` in the
[V-model](../quality/) carry this, verified by the `soup` suite; `MOD-SOUP` is
the two scripts.
