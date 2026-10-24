# Provisioning new boards

A new board arrives with a blank CH32H417. Provisioning turns it into a
product: production firmware in flash, the chip locked down, its own key
made and recorded in `boards.csv`, its microSD card prepared, and the
current release installed. One person, the **provisioning operator**, does
it at a **provisioning station**, and a script, `scripts/admin/provision.py`,
does every step that a person doesn't have to.

This page is the procedure, and what `provision.py` does at each step
(section 10 has its commands and how it's tested). Background:
[updates.md](updates.md) (keys, `boards.csv`, packages),
[firmware-updates.md](firmware-updates.md) (stage 1, recovery, the card
files) and [security-plan.md](security-plan.md) section 7.

## 1. Who holds what

The station never holds a private key. The first app package a board takes
must be encrypted for the key it has just made, and signed, so provisioning
takes two passes over each batch of boards, with the vendor in between:

```
 Station (operator)                     Vendor (release manager)
 ------------------                     ------------------------
 Pass 1, each board:
   card, flash, lock, enroll  ------->  enrolled.csv, records
                                        review, add to boards.csv
                                        package the release for the batch
 Pass 2, each board:          <-------  batch.lfu
   install, check, label
```

| | Station | Vendor |
|---|---|---|
| Root key (`root.key`) | never | offline |
| Release key (`releaseN.key`) | never | yes |
| `root.pub` | yes: checks packages and the signed manifest | yes |
| The release's level 2 `LiteForth_V3F.bin` (and a `LiteForth_V5F.bin`, if the manifest names one) and its card file | yes | makes them |
| `boards.csv` | read-only copy, to refuse duplicates | the reviewed copy, in the repository |
| Enrollment records | writes them | keeps them |

A station that's stolen or tampered with can't sign firmware or decrypt
packages meant for other boards. The worst it can do is enroll boards that
aren't ours, which the vendor's review of each batch is there to catch.

## 2. The station

- **A Windows PC** used only for provisioning, with Python 3, `pyserial`,
  `cryptography` (for checking packages) and `wlink`
  ([scripts/README.md](../../scripts/README.md)).
- **A WCH-LinkE** on the board's debug header. It gives the probe (SWD)
  and the **debug port** (the WCH-Link's UART, where both cores log).
- **A USB cable** to the board's own USB connector: the **cli port**,
  LiteForth's terminal (1A86:FE0C), which is stage 1's recovery console
  until a release is installed.
- **A microSD card reader** for preparing cards.
- **A way to cut the board's power**: unplugging both USB cables, or a
  switchable USB hub. The option bytes take effect only at a power-on, so
  the script asks for one after the lock.

Its folder:

```
station/
  release/        from the vendor: the images, the card files, manifest.json, root.pub
  boards.csv      the vendor's current copy (read-only here)
  batches/
    2026-11-03-A/
      enrolled.csv      UID,public key: one line per board enrolled
      records/          one report per board, every port's output word for word
      batch.lfu         from the vendor, for pass 2
```

`manifest.json` names the release (its version and product), the sha256
of each image and card file, and the card files' sizes. It's signed with
the release key and carries that key's certificate from the root, as a
package does. So the station checks it against `root.pub` alone and
doesn't have to trust whoever copied the folder. A file in `release/` that
the manifest doesn't name, or whose hash differs, stops everything.

**Serial numbers:** a board's serial number is its UID, the 16 hex digits
`enroll` prints first (`e52116647f9c7a22`). It's in the chip, so it can't
be mislabelled in the records, and it's the first field of the board's
line in `boards.csv`.

## 3. Before a batch

1. The vendor builds both cores at `SECURITY_LEVEL` 2 with the product's
   `sealed_root.h` (clean builds, [updates.md](updates.md) section 2),
   writes and signs the manifest:

   ```
   sealblocks.py manifest release/ --release release1.key --revision R
   ```

   with `LiteForth_V3F.bin`, `lfblocks.fb4` and `root.pub` in `release/`,
   and copies `release/` to the station. R is the version pass 2 will
   install. `--v5f FILE` adds a V5F image to flash too, which isn't needed:
   at level 2 stage 1 starts only a V5F it installed, and pass 2 installs
   one. `--stage-size` and `--backup-size` set the card files' sizes.
2. `provision.py station-check` checks the folder:
   - the manifest's signature, and the release key's certificate against
     `root.pub` (the certificate names the product);
   - every file against the manifest, and no file the manifest doesn't name;
   - `boards.csv` reads;
   - `wlink` and the debug port are there.
3. `provision.py batch-new NAME` makes the batch's folder, tied to the
   release as it is: if `release/` changes, the batch's passes stop, and a
   new release gets a new batch.

## 4. Pass 1: card, flash, lock, enroll

For each board, about three minutes.

### 4.1 The card

Put a new card in the reader, then run:

```
provision.py card E:
```

The script:
- refuses a card that isn't exFAT, or has any files on it (format a used
  card as exFAT first, *Quick Format*, [sdcard.md](../sdcard.md));
- writes `lfblocks.fb4`, `lfstage.bin` and `lfbackup.bin` from the release
  at the manifest's sizes;
- reads them back.

On an empty, freshly formatted card the files are contiguous, which the
board needs. The board confirms this in pass 2, when it finds them.

The sizes today:

| File | Size | Holds |
|---|---|---|
| `lfblocks.fb4` | the release's, with room to grow | the application's blocks |
| `lfstage.bin` | 512 KB (an app package is 385024 bytes) | a package on its way in |
| `lfbackup.bin` | 512 KB (94 blocks are needed today) | what the last update replaced |

Eject the card and put it in the board.

### 4.2 Flash and lock

Connect the WCH-LinkE and the USB cable, then run:

```
provision.py pass1 --batch 2026-11-03-A
```

What it does, step by step:

1. **Reads the chip with the probe:** its UID, and the option bytes. It
   refuses a chip that's read- or write-protected, since wlink's flash
   would unprotect it, which erases it, and a UID that's in `boards.csv`
   or the batch already. It also refuses a chip that isn't blank, reading
   only the first 256 bytes of the V3F's image area, never stage 1's
   records: erased flash reads all 0xFF, or 0xE339E339 as the simulator
   has it. A board provisioned before is handled by section 8, not
   reflashed. The exception is a board this batch flashed but didn't
   lock (the operator didn't type `LOCK`, or a read-back failed): it's
   flashed again.
2. **Flashes the manifest's images,** the V3F's last, without erasing
   anything else and without a reset between them, and reads the start of
   each back. The V3F goes last because once a level 2 stage 1 runs, the
   probe often can't attach again (B25).
3. **Resets the chip and reads the debug port.** Stage 1 must report
   `security level 2, installed version 0, recovery` and `not locked down`,
   and the cli port's recovery console must offer `lockdown`. Anything else
   is the wrong build, and the script stops before locking.
4. **Locks the chip:** sends `lockdown yes` on the cli port. Stage 1
   programs the option bytes (read protection on, its own sectors
   write-protected, the boot ROM's downloads off), reads them back and
   restarts. The operator confirms once per run, not per board: `pass1`
   asks for `LOCK` at its first board.
5. **Waits for the board to come back locked:** the debug port must show
   recovery, `no V5F image installed`, without `not locked down`, which
   means stage 1 found the option bytes as it wants them. If they didn't
   load at that restart, the script asks for a power cycle (*unplug the
   WCH-LinkE and the board's USB, wait 2 s, plug them in*) and waits.

### 4.3 Enroll

Still in pass 1, on the cli port:

6. **Sends `enroll`.** Stage 1 makes its security record and the device
   token from the RNG, and with them the board's key, and prints:

   ```
   enroll: new key made (a new security record and device token)
   enroll: boards.csv line:
   e52116647f9c7a22,044974c7a23fe018fdb9f2182e9d71344c290ade351700d2057d83625b8a9104
   ```

   The script requires `new key made`: a board that already had a key
   wasn't blank. It also requires the line's UID to be the one the probe
   read in step 1 (in either byte order, which the record notes), and to
   be in neither `boards.csv` nor the batch already, and the public key to
   be no other board's: a repeated key would mean a broken RNG.
7. **Checks with the probe**, last of all, because it halts the V3F: the
   flash is read-protected and the option bytes are as stage 1 wants
   them. Then it lets the V3F run again.
8. **Records the board.** It adds `UID,public key` to the batch's
   `enrolled.csv`, and writes the board's report, `records/UID.md`: the
   UID, public key, the images' sha256, the option bytes before and after, the time,
   the station and the operator, and both ports' output word for word.

Label the board with its UID, its serial number, and unplug it. `pass1`
then asks for the next board; `q` ends the run. A board that stops gets
its report all the same, as `records/UID-failed-TIME.md`, and the run
goes on with the next.

A board stays in recovery until pass 2: at level 2 it starts only a
release that stage 1 installed.

## 5. The vendor's step

The operator sends the batch folder's `enrolled.csv` and `records/` to
the release manager, through a channel that shows who sent it, such as a
commit from the station's account to a provisioning repository.

The release manager then:
1. **Checks the batch:**

   ```
   provision.py batch-review 2026-11-03-A --boards boards.csv
   ```

   It refuses malformed lines; a UID twice, or one in `boards.csv`
   already; a public key another board has; and a line whose board's
   report doesn't end enrolled with that line, or doesn't show the lock
   and `new key made`. A line in `boards.csv` decides which boards can open
   our releases, so it goes in only for a board we provisioned.
2. **Adds the lines to `boards.csv`** in a reviewed commit, as
   [updates.md](updates.md) section 3 says. `--append` adds them, under
   a comment naming the batch.
3. **Packages the current release for the batch only:**

   ```
   sealblocks.py package LiteForth_V5F.bin batch.lfu --component app --pages lfpages.bin --release release1.key --boards enrolled.csv --revision R
   sealblocks.py check batch.lfu --root root.pub
   ```

   `batch-review` prints these commands. A package holds at most 157
   boards, so it splits a larger batch into parts (`enrolled-1.csv`, ...,
   or fewer per part with `--split N`), each with its own package.
4. **Returns `batch.lfu` and its sha256** to the station.

## 6. Pass 2: install the release

For each board of the batch:

```
provision.py pass2 --batch 2026-11-03-A --package batch.lfu
```

1. **Checks the packages**, once per run, before any board: against
   `root.pub` as `sealblocks.py check` does, for the manifest's product,
   an app package, of the manifest's version. A batch in parts gets one
   `--package` for each.
2. **Identifies the board:** `enroll` on the cli port gives its UID, which
   must have a pass 1 record in the batch, with the same public key (a key
   made again since means the board was erased), and be a recipient of one
   of the packages.
3. **Sends the package** by YMODEM to stage 1's recovery console (`-2
   ymodem`). Stage 1 writes it into `lfstage.bin`, checks it, installs it,
   says `update: version R installed, on trial` on the cli port, and
   restarts. Then the debug port must show:

   ```
   V3F stage 1: security level 2, installed version R
   V5F: LiteForth starting, security level 2, booting from flash
   V3F update: version R confirmed
   ```

   The last line comes when LiteForth's first `ok>` ends the trial.
4. **Checks the board on the cli port:**
   - `revision .` gives R;
   - `reboot`, then the same boot lines again: the signed boot check
     passes every time;
   - the application's own test, if the product has one
     (`--test FILE`: lines `COMMAND ==> TEXT`, each sent and TEXT expected
     in the reply).
5. **Adds the outcome to the board's report**, and the board to the
   batch's `installed.csv` (UID, version, the package's sha256, the time).
   A board that passes is done: unplug it.

## 7. Records

Each board's report, with `enrolled.csv` and the batch's `batch.lfu`
sha256, is the record of how that board was made. These records are part
of the product's technical documentation (the CRA): keep them for the
product's support period, at the vendor, with the commits that added the
boards to `boards.csv`.

## 8. When something goes wrong

| What the script says | Why | What to do |
|---|---|---|
| `the chip isn't blank` | A board provisioned before, or from another line | Set it aside for the release manager (below) |
| `the chip is protected` | Locked already | As above |
| `... doesn't read back` after flashing | A bad connection or chip | Check the probe's cable; run `pass1` again: the chip is still unlocked, and this batch flashes it again |
| `the release changed since batch ... began` | `release/` isn't what the batch started with | Start a new batch for the new release |
| `isn't a level 2 build in recovery` | The wrong images in `release/` | Stop the batch, tell the release manager |
| `still not locked down after the power cycle` | The option bytes didn't load | Power-cycle again; if it persists, set the board aside |
| `enroll didn't make a new key` | The board had a record already | Set it aside |
| `UID ... already enrolled` | The same board twice, or a clone | Set it aside, tell the release manager |
| `enroll's UID ... isn't the probe's` | The probe on one board, the USB on another | Connect both to the same board; this one is locked, so set it aside |
| `its public key is another board's` | A broken RNG | Set it aside, tell the release manager |
| `isn't a recipient of any package given` in pass 2 | The board isn't in these packages | Check the batch and its parts |
| `no recovery console` in pass 2 | A release is installed already, or the board isn't the batch's | Check its record |
| `stage 1 didn't install it` (`no lfstage.bin`, a fragmented file) | The card | Prepare a new card (4.1), put it in, run `pass2` again |
| `the release wasn't confirmed` | The release didn't start | Stage 1 rolls back or stays in recovery; set the board aside with its report |

A board set aside has been locked, and nothing short of a chip erase
takes a locked board back. A chip erase (`wlink unprotect`, done by hand by
the release manager) wipes its flash, its record and its key. It then
starts again at pass 1 as a blank board and gets a new key, keeping its
UID and so its serial number. The release manager first takes its old
line out of `boards.csv` in a reviewed commit (the old key no longer
exists, and no package should name it again), or `pass1` refuses the
board as `UID already enrolled`. Its old report stays with the records.

## 9. Security notes

- **Nothing on the station is secret.** The images and card files are
  public, and so is `root.pub`. What the station must protect is their
  integrity, which the signed manifest gives them: the station checks it
  against `root.pub` before every batch.
- **A board's key never leaves it.** `enroll` prints only the public half,
  and the probe reads only the start of the V3F's image area and of each
  image flashed, never stage 1's records.
- **No board leaves unlocked.** `pass1` doesn't record a board until the
  debug port shows it locked and the probe can't read its flash, and
  `pass2` won't install on a board without its pass 1 record.
- **The UID is checked twice**, by the probe and by `enroll`, so a board
  swapped between steps is caught.
- **A repeated public key is refused**, by `pass1` and by `batch-review`:
  boards making the same key would mean the RNG isn't random.

## 10. The scripts

`scripts/admin/provision.py` (pyserial, the cryptography package and
wlink, as `lockdown.py`; `batch-review` needs no pyserial):

| Command | Runs at | Does |
|---|---|---|
| `station-check [--station DIR]` | station | section 3 |
| `batch-new NAME` | station | section 3 |
| `card DRIVE` | station | 4.1 |
| `pass1 --batch NAME` | station | 4.2, 4.3; loops over boards until `q` |
| `batch-review BATCHDIR --boards CSV [--append] [--split N]` | vendor | section 5 |
| `pass2 --batch NAME --package P [--package P ...] [--test FILE]` | station | section 6; loops over boards |

The station's folder is the current one unless `--station` names it.
`pass1` and `pass2` find the debug port and the cli port by their USB
IDs, or take `--debug` and `--cli`, and `wlink` on the PATH or `--wlink`.

`sealblocks.py manifest DIR --release KEY --revision R` writes
`DIR/manifest.json` and signs it, and `sealblocks.py check-manifest DIR
--root root.pub` checks it as the station does. The signature is Ed25519
over `LiteForth manifest 1` and a NUL, then the manifest as canonical JSON
(sorted keys, no spaces) without its signature, so it can't be taken for
a package's, whose block 0 starts `LITEFORTHF`.

- **What it builds on:** `lockdown.py`'s probe, flashing, read-back and
  recovery console, which `lockdown.py run` uses for one board, and
  `hiltest.py`'s ports and YMODEM sender; the package and manifest checks
  from `sealblocks.py`.
- **How it's tested:** `unit_tests/stage1sim/test_provision.py` runs it
  against simulated level 2 boards (`sim_strict`) with a fake wlink, as
  `test_lockdown.py` does `lockdown.py`. It covers:
  - a station with a signed release, and a changed file refused;
  - a card, and a card that isn't empty refused;
  - pass 1 over seven boards: three enrolled, one of them flashed again
    after `LOCK` wasn't typed; one in `boards.csv` already, one not
    blank, one whose RNG repeats another board's key and one whose probe
    is on another board, all four refused;
  - the vendor's review, a split, a tampered record refused, and
    `--append`;
  - a package of the wrong version refused;
  - pass 2 installing on two boards with a product test, and refusing a
    board not in the batch and one already done.

  `unit_tests/seal` tests the manifest: every change after signing, another
  root, a changed, missing or extra file.
- **To see on the first real board** (and log in [board-tests.md](board-tests.md)):
  - **The UID's byte order:** whether wlink prints the UID in the same
    order as `enroll` (the 8 bytes at `ESIG_UNIID1` as they lie in
    memory). Both orders are taken, and the record notes a reversed one.
  - **The blank check:** what a new chip's flash reads.
  - **A switchable USB hub:** whether one can do the power cycles
    (`uhubctl` or similar), so the operator only connects and labels.
  - **Card contiguity:** the script doesn't check it on the PC (that needs
    raw access, administrator rights on Windows). The board does in pass
    2, when stage 1 takes the package into `lfstage.bin`.
