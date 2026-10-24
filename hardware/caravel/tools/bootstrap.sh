#!/bin/bash
# Rebuild the FOSS ASIC toolchain used for hardware/caravel from GitHub only
# (no apt, PyPI, conda or Docker: the sandbox this was first built in blocked
# them). Ubuntu 24.04 host with gcc/g++, make, cmake, bison, libx11-dev,
# libcairo2-dev, zlib1g-dev, libreadline-dev, python3 (+click, pyyaml).
#
#   TOOLS=/home/claude/tools PDK_ROOT=/home/claude/pdk tools/bootstrap.sh
#
# Takes ~40 min on 2 cores (Python 3.10 is the slowest part), ~6 GB disk.
# Every step is idempotent-ish; rerun after fixing whatever broke.
set -euo pipefail
TOOLS=${TOOLS:-/home/claude/tools}
PDK_ROOT=${PDK_ROOT:-/home/claude/pdk}
SRC=${SRC:-/home/claude/src}
P=$TOOLS/local
HERE=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$TOOLS" "$SRC" "$PDK_ROOT"
GH=https://github.com

# 1. Yosys + iverilog + verilator: YosysHQ oss-cad-suite (self-contained)
if [ ! -x "$TOOLS/oss-cad-suite/bin/yosys" ]; then
  curl -sSL $GH/YosysHQ/oss-cad-suite-build/releases/download/2024-10-01/oss-cad-suite-linux-x64-20241001.tgz \
    | tar xz -C "$TOOLS"
fi

# 2. zstd (to unpack the PDK)
[ -x "$SRC/zstd/programs/zstd" ] || { git clone -q --depth 1 $GH/facebook/zstd "$SRC/zstd"; make -C "$SRC/zstd" -j"$(nproc)" zstd-release >/dev/null; }
ZSTD=$SRC/zstd/programs/zstd

# 3. sky130A PDK, the open_pdks build OpenLane 1.0.2 pins (volare release assets)
V=0fe599b2afb6708d281543108caf8310912f54af
if [ ! -d "$PDK_ROOT/sky130A" ]; then
  d=$PDK_ROOT/volare/sky130/versions/$V; mkdir -p "$d"
  for a in common sky130_fd_sc_hd sky130_fd_pr; do
    curl -sSL $GH/chipfoundry/volare/releases/download/sky130-$V/$a.tar.zst | "$ZSTD" -dc | tar x -C "$d"
  done
  rm -rf "$d/sky130B"; ln -sfn "volare/sky130/versions/$V/sky130A" "$PDK_ROOT/sky130A"
fi

# 4. Tcl/Tk 8.6, tclreadline, tcllib (OpenLane 1 needs `package require json`)
if [ ! -x "$P/bin/tclsh8.6" ]; then
  git clone -q --depth 1 -b core-8-6-branch $GH/tcltk/tcl "$SRC/tcl"
  git clone -q --depth 1 -b core-8-6-branch $GH/tcltk/tk "$SRC/tk"
  (cd "$SRC/tcl/unix" && ./configure --prefix="$P" --enable-shared >/dev/null && make -j"$(nproc)" >/dev/null && make install >/dev/null)
  (cd "$SRC/tk/unix" && ./configure --prefix="$P" --with-tcl="$P/lib" >/dev/null && make -j"$(nproc)" >/dev/null && make install >/dev/null)
  ln -sf tclsh8.6 "$P/bin/tclsh"; ln -sf wish8.6 "$P/bin/wish"
fi
if [ ! -e "$P/lib/libtclreadline-2.3.8.so" ]; then
  git clone -q --depth 1 $GH/flightaware/tclreadline "$SRC/tclreadline"
  (cd "$SRC/tclreadline" && autoreconf -fi >/dev/null 2>&1; ./configure --prefix="$P" --with-tcl="$P/lib" --with-tcl-includes="$P/include" >/dev/null && make -j"$(nproc)" >/dev/null && make install >/dev/null)
  ln -sf libtclreadline-2.5.0.so "$P/lib/libtclreadline-2.3.8.so"   # OpenROAD deb wants 2.3.8
fi
if ! echo 'puts [package require json]' | "$P/bin/tclsh8.6" >/dev/null 2>&1; then
  git clone -q --depth 1 $GH/tcltk/tcllib "$SRC/tcllib"
  (cd "$SRC/tcllib" && ./configure --prefix="$P" >/dev/null && make install-libraries >/dev/null 2>&1)
fi

# 5. Magic and Netgen (Tcl builds). Netgen ignores --with-tclincls: use C_INCLUDE_PATH.
if [ ! -x "$P/bin/magic" ]; then
  git clone -q --depth 1 $GH/RTimothyEdwards/magic "$SRC/magic"
  (cd "$SRC/magic" && ./configure --prefix="$P" --with-tcl="$P/lib" --with-tk="$P/lib" --disable-opengl >/dev/null && make -j"$(nproc)" >/dev/null && make install >/dev/null)
fi
if [ ! -x "$P/bin/netgen" ]; then
  git clone -q --depth 1 $GH/RTimothyEdwards/netgen "$SRC/netgen"
  (cd "$SRC/netgen" && ./configure --prefix="$P" --with-tcl="$P/lib" --with-tk="$P/lib" >/dev/null && C_INCLUDE_PATH="$P/include" make -j"$(nproc)" >/dev/null && make install >/dev/null)
fi

# 6. OpenROAD: Precision Innovations' Ubuntu 22.04 .deb (2024-12-14), unpacked.
#    Its missing shared libraries: OR-Tools 9.11 (Google release), Python 3.10
#    (built), Tcl/tclreadline (above) and Qt5Charts (stubbed: GUI never used).
if [ ! -x "$TOOLS/openroad/usr/bin/openroad" ]; then
  curl -sSL -o /tmp/openroad.deb $GH/Precision-Innovations/OpenROAD/releases/download/2024-12-14/openroad_2.0-17598-ga008522d8_amd64-ubuntu-22.04.deb
  dpkg-deb -x /tmp/openroad.deb "$TOOLS/openroad"; rm /tmp/openroad.deb
fi
if [ ! -d "$TOOLS/ortools/lib" ]; then
  mkdir -p "$TOOLS/ortools"
  curl -sSL $GH/google/or-tools/releases/download/v9.11/or-tools_amd64_ubuntu-22.04_cpp_v9.11.4210.tar.gz | tar xz --strip 1 -C "$TOOLS/ortools"
fi
if [ ! -e "$TOOLS/py310/lib/libpython3.10.so.1.0" ]; then
  git clone -q --depth 1 -b 3.10 $GH/python/cpython "$SRC/cpython310"
  (cd "$SRC/cpython310" && ./configure --prefix="$TOOLS/py310" --enable-shared --without-ensurepip >/dev/null && make -j"$(nproc)" >/dev/null 2>&1 && make install >/dev/null 2>&1)
fi
if [ ! -e "$P/lib/libQt5Charts.so.5" ]; then
  python3 - "$HERE/qtcharts_syms.txt" > /tmp/qtstub.c <<'EOF'
import sys
for i, s in enumerate(open(sys.argv[1]).read().split()):
    if s.startswith('_ZTI') or 'staticMetaObject' in s:
        print(f'__attribute__((visibility("default"))) char d{i}[256] __asm__("{s}");')
    else:
        print(f'__attribute__((visibility("default"))) void f{i}(void) __asm__("{s}");\nvoid f{i}(void){{}}')
EOF
  echo 'Qt_5 { global: *; };' > /tmp/qtstub.map
  gcc -shared -fPIC -o "$P/lib/libQt5Charts.so.5" -Wl,-soname,libQt5Charts.so.5 -Wl,--version-script=/tmp/qtstub.map /tmp/qtstub.c
fi
# pure-Python deps for OpenROAD's embedded 3.10 (OpenLane's odbpy scripts)
SP=$TOOLS/py310/lib/python3.10/site-packages
if [ ! -d "$SP/xlsxwriter" ]; then
  for r in pallets/click yaml/pyyaml jmcnamara/XlsxWriter; do git clone -q --depth 1 $GH/$r "$SRC/$(basename $r)"; done
  cp -r "$SRC/click/src/click" "$SRC/pyyaml/lib/yaml" "$SRC/XlsxWriter/xlsxwriter" "$SP/"
  mkdir -p "$SP/click-8.1.0.dist-info"; printf 'Metadata-Version: 2.1\nName: click\nVersion: 8.1.0\n' > "$SP/click-8.1.0.dist-info/METADATA"
fi

# 7. OpenLane 1.0.2 (Tcl flow, runs natively) + patch for OpenROAD 2024-12
if [ ! -d "$SRC/openlane1" ]; then
  git clone -q --depth 1 -b 1.0.2 $GH/The-OpenROAD-Project/OpenLane "$SRC/openlane1"
  (cd "$SRC/openlane1" && git apply "$HERE/openlane1-1.0.2-openroad-2024.12.patch")
  sed -n '/^proc run_placement_step/,/^proc run_non_interactive_mode/p' "$SRC/openlane1/flow.tcl" | sed '$d' > "$TOOLS/flow_steps.tcl"
fi

echo "done. source tools/env.sh (adjust its paths if TOOLS/PDK_ROOT differ)"
