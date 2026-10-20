# Software Bill of Materials

[`sbom.spdx.json`](../../sbom.spdx.json), in the repository root, lists what
LiteForth is made of besides its own code, in
[SPDX 2.3](https://spdx.github.io/spdx-spec/v2.3/) JSON: each component's
name, version, supplier, source, license and copyright, and every file of
this repository that holds it, with its SHA-1 and SHA-256. Files changed
from what the supplier published say so. LiteForth's own files are MIT
([LICENSE](../../LICENSE)).

`scripts/mksbom.py` writes it from a list of components kept in the
script. Checksums are of the files as git stores them (LF line endings),
so a Windows checkout gives the same ones.

## Components

| Component | Version | License | Where | Used for |
|---|---|---|---|---|
| [Monocypher](https://monocypher.org) | 4.0.2 | CC0-1.0 or BSD-2-Clause | `src/monocypher*` | X25519 and Ed25519 for [sealed block files](encrypted-blocks.md). One cast added for MSVC |
| [blake2s](https://github.com/bradleyeckert/blake2s) | d887870 | CC0-1.0, OpenSSL or Apache-2.0 (the BLAKE2 reference code) | `src/blake2s.*` | BLAKE2s hashes for sealed block files |
| [CH32H417 EVT](https://github.com/openwch/ch32h417) (WCH) | 6d1e469 | WCH's notice: for use with WCH microcontrollers | `src/target/CH32H417/SRC/`, and files from the SimulateCDC and SDIO examples in `Common/`, `V3F/User/`, `V5F/User/` | The CH32H417 target: peripheral library, start-up, linker scripts, USB device, SD card. 18 of the 111 files are changed for LiteForth |
| [Unity](https://github.com/ThrowTheSwitch/Unity) | 2.7.2 | MIT | `src/target/desktop/unit_tests/unity/` | The desktop unit tests only; not in `lf` or the firmware |
| [Noto Sans CJK SC](https://github.com/notofonts/noto-cjk) | (as installed when the fonts were built) | OFL-1.1 | Blocks 100 and 120 of `bin/lfblocks.fb4` | The LCD fonts, rendered from it by `scripts/mkfont.py` |
| [cryptography](https://pypi.org/project/cryptography/) | any recent | Apache-2.0 or BSD-3-Clause | Not in this repository | `scripts/admin/sealblocks.py` and its tests |
| [pyserial](https://pypi.org/project/pyserial/) | any recent | BSD-3-Clause | Not in this repository | `scripts/admin/hiltest.py` and its test on the simulated board |
| [Pillow](https://pypi.org/project/pillow/), [fontTools](https://pypi.org/project/fonttools/) | any recent | MIT-CMU, MIT | Not in this repository | `scripts/mkfont.py` |

Not listed: the compilers (gcc, clang, MSVC, MounRiver's RISC-V GCC), and
the C library each links against.

The WCH files carry only WCH's notice, "This software (modified or not)
and binary are used for microcontroller manufactured by Nanjing Qinheng
Microelectronics"; the SBOM records it as `LicenseRef-WCH-EVT`.

## Keeping it right

`make test` runs `unit_tests/sbom`, which fails when:

- a file the SBOM lists has changed (`mksbom.py --check`): look at the
  change, mark the file in `MODIFIED` in the script if it now differs from
  the supplier's, and run `python3 scripts/mksbom.py` to write the SBOM
  again;
- a tracked source file carries a copyright notice that no component
  claims (`mksbom.py --scan`): someone else's code came in, so add it to
  `COMPONENTS` with its license and source.

After updating a component (a new Monocypher, say), change its version
and source in the script as well.
