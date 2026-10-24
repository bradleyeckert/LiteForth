# Provisioning new boards

A new board arrives with a blank CH32H417. Provisioning turns it into a
product: production firmware in flash, the chip locked down, its own key
made and recorded in `boards.csv`, its microSD card prepared, and the
current release installed. One person, the **provisioning operator**, does
it at a **provisioning station**, and a script, `scripts/admin/provision.py`,
does every step that a person doesn't have to.

This page is the procedure, and the specification for `provision.py`,
which is still to be written (section 10). Background:
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
| `root.pub` | yes: checks packages and the image manifest | yes |
| The release's images (level 2 `LiteForth_V3F.bin`, `LiteForth_V5F.bin`) and its card files | yes | makes them |
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
      enrolled.csv      UID,public key,name: one line per board enrolled
      records/          one report per board, every port's output word for word
      batch.lfu         from the vendor, for pass 2
```

`manifest.json` names the release (its version), the sha256 of each
image and card file, and the card files' sizes. **Decide:** sign it with
the release key, so the station can check it against `root.pub` rather
than trust whoever copied the folder.

## 3. Before a batch

1. The vendor builds both cores at `SECURITY_LEVEL` 2 with the product's
   `sealed_root.h` (clean builds, [updates.md](updates.md) section 2),
   and copies the images, the card files and `manifest.json` to
   `station/release/`.
2. `provision.py station-check` checks the folder:
   - every file against the manifest;
   - `wlink`, the probe and the ports are there;
   - `boards.csv` reads.
3. `provision.py batch-new NAME` makes the batch's folder. Board names are
   the batch name and a number (`2026-11-03-A-007`), unless `--names`
   gives a list, such as serial numbers already on labels.

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
   would unprotect it, which erases it. It also refuses a chip that isn't
   blank: a board provisioned before is handled by section 8, not
   reflashed.
2. **Flashes the V5F image, then the V3F image,** without erasing anything
   else and without a reset between them. It reads the start of each back.
   The V3F goes last because once a level 2 stage 1 runs, the probe often
   can't attach again (B25).
3. **Resets the chip and reads the debug port.** Stage 1 must report
   `security level 2, installed version 0, recovery` and `not locked down`,
   and the cli port's recovery console must offer `lockdown`. Anything else
   is the wrong build, and the script stops before locking.
4. **Locks the chip:** sends `lockdown yes` on the cli port. Stage 1
   programs the option bytes (read protection on, its own sectors
   write-protected, the boot ROM's downloads off) and reads them back. The
   operator confirms once per batch, not per board: `pass1` asks for `LOCK`
   at its first board.
5. **Asks for a power cycle:** *Unplug the board, wait 2 s, plug it in.*
   Then the debug port must show recovery without `not locked down`, which
   means stage 1 found the option bytes as it wants them.

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
   wasn't blank. It also requires the line's UID to match the one the
   probe read in step 1, and to be in neither `boards.csv` nor the batch
   already.
7. **Checks with the probe**, last of all, because it halts the V3F: the
   flash is read-protected. Then it asks for another power cycle, to leave
   the board running.
8. **Records the board.** It adds `UID,public key,name` to the batch's
   `enrolled.csv`, and writes the board's report: the name, UID, public
   key, the images' sha256, the option bytes before and after, the time,
   the station and the operator, and both ports' output word for word.

Label the board with its name.

A board stays in recovery until pass 2: at level 2 it starts only a
release that stage 1 installed.

## 5. The vendor's step

The operator sends the batch folder's `enrolled.csv` and `records/` to
the release manager, through a channel that shows who sent it, such as a
commit from the station's account to a provisioning repository.

The release manager then:
1. **Checks the batch:**

   ```
   provision.py batch-review enrolled.csv --boards boards.csv
   ```

   It refuses duplicates and malformed lines, and checks each line against
   the board's report. A line in `boards.csv` decides which boards can open
   our releases, so it goes in only for a board we provisioned.
2. **Adds the lines to `boards.csv`** in a reviewed commit, as
   [updates.md](updates.md) section 3 says.
3. **Packages the current release for the batch only:**

   ```
   sealblocks.py package LiteForth_V5F.bin batch.lfu --component app --pages lfpages.bin --release release1.key --boards enrolled.csv --revision R
   sealblocks.py check batch.lfu --root root.pub
   ```

   A package holds at most about 157 boards. `batch-review` splits a
   larger batch into parts (`enrolled-1.csv`, ...), and each part gets its
   own package.
4. **Returns `batch.lfu` and its sha256** to the station.

## 6. Pass 2: install the release

For each board of the batch:

```
provision.py pass2 --batch 2026-11-03-A --package batch.lfu
```

1. **Checks the package**, once per batch: its sha256, and
   `sealblocks.py check` against `root.pub`.
2. **Identifies the board:** `enroll` on the cli port gives its UID, which
   must be in the batch, and a recipient of the package.
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
     (`--test SCRIPT`: lines to send, and what each must print).
5. **Adds the outcome to the board's report.** A board that passes is done:
   unplug it.

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
| `read back differs` after flashing | A bad connection or chip | Check the probe's cable; run `pass1` again (the chip is still unlocked) |
| `not a level 2 build` (no `lockdown` in recovery) | The wrong images in `release/` | Stop the batch, tell the release manager |
| `still not locked down` after the power cycle | The option bytes didn't load | Power-cycle again; if it persists, set the board aside |
| `enroll didn't make a new key` | The board had a record already | Set it aside |
| `UID already enrolled` | The same board twice, or a clone | Set it aside, tell the release manager |
| `not a recipient` in pass 2 | The board isn't in this package | Check the batch; a board missing from `enrolled.csv` goes through pass 1's records again |
| `no lfstage.bin` / a fragmented file | The card | Prepare a new card (4.1), put it in, run `pass2` again |
| No `confirmed` | The release didn't start | Stage 1 rolls back or stays in recovery; set the board aside with its report |

A board set aside has been locked, and nothing short of a chip erase
takes a locked board back. A chip erase (`wlink unprotect`, done by hand by
the release manager) wipes its flash, its record and its key. It then
starts again at pass 1 as a blank board and gets a new key. Its old line
comes out of `boards.csv`: the old key no longer exists, and no package
should name it again.

## 9. Security notes

- **Nothing on the station is secret.** The images and card files are
  public, and so is `root.pub`. What the station must protect is their
  integrity, and the manifest's sha256 do that (signed by the release key,
  once decided in section 2).
- **A board's key never leaves it.** `enroll` prints only the public half,
  and the script never reads flash beyond the start of the two images.
- **No board leaves unlocked.** `pass1` doesn't record a board until the
  debug port shows it locked and the probe can't read its flash, and
  `pass2` won't install on a board without its pass 1 record.
- **The UID is checked twice**, by the probe and by `enroll`, so a board
  swapped between steps is caught.

## 10. `provision.py`, to be written

| Command | Runs at | Does |
|---|---|---|
| `station-check` | station | section 3 |
| `batch-new NAME [--names FILE]` | station | section 3 |
| `card DRIVE` | station | 4.1 |
| `pass1 --batch B` | station | 4.2, 4.3; loops over boards until stopped |
| `batch-review CSV --boards CSV` | vendor | section 5 |
| `pass2 --batch B --package P [--test SCRIPT]` | station | section 6; loops over boards |

- **What it builds on:** the ports, the probe and the YMODEM sender from
  `hiltest.py` and `lockdown.py`, whose `run` already does steps 1 to 5 of
  4.2 for one board; and the package checks from `sealblocks.py`.
- **How it's tested:** like `lockdown.py`, against `stage1sim` with a fake
  wlink (`unit_tests/stage1sim`): a whole batch of simulated boards
  through both passes, and every row of section 8. Then a board run logged
  in [board-tests.md](board-tests.md).
- **To find out while writing it:**
  - **The UID's byte order:** whether wlink prints it in the same order
    as `enroll`'s UID (the 8 bytes at `ESIG_UNIID1` as they lie in
    memory).
  - **The blank check:** whether a new chip reads all 0xFF, or comes with
    something from the factory.
  - **A switchable USB hub:** whether one can do the power cycles
    (`uhubctl` or similar), so the operator only connects and labels.
  - **Reading the card on Windows:** whether the script can check the
    files' contiguity on the PC (raw volume access needs administrator
    rights), or leaves that to the board in pass 2.
