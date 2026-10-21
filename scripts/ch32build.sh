#!/bin/bash
# Builds the CH32H417's V3F or V5F image with an upstream RISC-V GCC (xPack
# riscv-none-elf-gcc), as the MounRiver projects do (V3F/.cproject,
# V5F/.cproject), for CI: it catches compile errors, new warnings, and an
# image that no longer fits (the linker refuses .bss running into the
# stack). The images aren't the ones to flash:
#   - upstream GCC lacks WCH's XW compressed opcodes, so the code is a
#     little bigger (RAM use is the same);
#   - it ignores __attribute__((interrupt("WCH-Interrupt-fast"))) with a
#     warning, so those handlers don't end in mret. Build in MounRiver for
#     the board.
#
# usage: scripts/ch32build.sh V3F|V5F [outdir]
#   RISCV_PREFIX  the compiler's prefix (default riscv-none-elf-, on PATH)
# Prints the section sizes; exits 1 on an error or a warning other than
# the interrupt attribute.
set -euo pipefail

CORE=${1:?usage: ch32build.sh V3F|V5F [outdir]}
case "$CORE" in
    V3F) STD=gnu99; OTHER=V5F ;;
    V5F) STD=gnu11; OTHER=V3F ;;
    *) echo "ch32build.sh: V3F or V5F, not $CORE" >&2; exit 2 ;;
esac
ROOT=$(cd "$(dirname "$0")/.." && pwd)
T=$ROOT/src/target/CH32H417
OUT=${2:-$ROOT/build/ch32/$CORE}
CC=${RISCV_PREFIX:-riscv-none-elf-}gcc
LC=$(echo "$CORE" | tr A-Z a-z)
mkdir -p "$OUT/obj"

# The projects' settings: rv32imac + B (XW left out), ilp32, -Os, ...
ARCH="-march=rv32imac_zba_zbb_zbc_zbs_zicsr_zifencei -mabi=ilp32 -msmall-data-limit=8 -msave-restore"
CFLAGS="$ARCH -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common
        -Wunused -Wuninitialized -std=$STD -DCore_$CORE"
INC="-I$T/SRC/Startup -I$T/$CORE/User -I$T/SRC/Debug -I$T/SRC/Core -I$T/SRC/Peripheral/inc -I$T/Common"

# What the project compiles: its User folder and the linked folders, but
# the other core's start-up code
SRCS=$(ls "$T/$CORE"/User/*.c "$T/$CORE"/User/*.S "$T"/Common/*.c "$T"/SRC/Core/*.c \
          "$T"/SRC/Debug/*.c "$T"/SRC/Peripheral/src/*.c 2>/dev/null; \
       echo "$T/SRC/Startup/startup_ch32h417_$LC.S")

OBJS=()
BAD=0
for f in $SRCS; do
    o="$OUT/obj/$(echo "${f#$T/}" | tr / _).o"
    if ! $CC $CFLAGS $INC -c "$f" -o "$o" 2>"$o.log"; then
        cat "$o.log"
        echo "ch32build.sh: $CORE: ${f#$ROOT/} doesn't compile" >&2
        exit 1
    fi
    # Upstream GCC's one expected warning: WCH's interrupt attribute
    if grep -v "argument to 'interrupt' attribute is not\|__attribute__((interrupt\|^ *[0-9]* | \|^ *| \|In function\|^In file included" "$o.log" \
            | grep -q "warning:"; then
        cat "$o.log"
        BAD=1
    fi
    OBJS+=("$o")
done

if ! $CC $ARCH -T "$T/SRC/Ld/$CORE/Link_$LC.ld" -nostartfiles -Xlinker --gc-sections \
        -Wl,-Map,"$OUT/$CORE.map" --specs=nano.specs --specs=nosys.specs \
        -o "$OUT/$CORE.elf" "${OBJS[@]}" -lm 2>"$OUT/link.log"; then
    grep -v "is not implemented and will always fail\|does not take linker garbage\|in function \`_" "$OUT/link.log" >&2
    echo "ch32build.sh: $CORE doesn't link" >&2
    exit 1
fi
${RISCV_PREFIX:-riscv-none-elf-}size -A "$OUT/$CORE.elf" | grep -E '^\.[a-z]' || true
if [ "$BAD" -ne 0 ]; then
    echo "ch32build.sh: $CORE: warnings above" >&2
    exit 1
fi
echo "ch32build.sh: $CORE builds and links"
