#!/bin/sh
# Test of the real-time clock words (rtc@ rtc! alarm! alarm? standby) on the
# desktop, where the clock is the host's plus an offset and standby sleeps
# until the alarm, then restarts lf (lfReset). Runs bin/lf in a temp dir
# and compares its output, without the version banners, with expected.txt.
# Takes about 3 s. Usage: run.sh path/to/lf
set -e
LF=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
HERE=$(cd "$(dirname "$0")" && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"

T=1767225600                            # 2026-01-01 00:00:00 UTC
NEG_T="$T inv 1 +"                      # core.f isn't loaded: no - or u<
{
    echo ".( [no alarm yet] ) alarm? ."
    echo ".( [set] ) $T rtc! rtc@ $NEG_T + 2/ ."     # 0 or 1 s later: 0
    echo ".( [standby without alarm] ) standby"
    echo ".( [alarm too soon] ) rtc@ 1 + alarm! standby"
    echo ".( [alarm off] ) 0 alarm! standby"
    echo ".( [not yet] ) rtc@ 5 + alarm! alarm? ."
    echo ".( [standby] ) rtc@ 2 + alarm! standby .( never )"
    echo ".( [woke] ) alarm? . alarm? ."
    echo ".( [time kept] ) rtc@ $NEG_T + -2 + 2/ 2/ ."   # 2 to 5 s later: 0
    echo "bye"
} > in.txt
"$LF" -o 3 < in.txt > out.txt 2>&1 || true
# Drop the first version banner and mark where lf restarted
sed 's/\[[^]]*\] v[0-9][0-9.]*/[restart]/' out.txt | tail -n +2 > got.txt
# --strip-trailing-cr: a Windows checkout may have given expected.txt CRLF endings
if diff -u --strip-trailing-cr "$HERE/expected.txt" got.txt; then
    echo "rtc tests passed"
else
    cp got.txt "$HERE/got.txt"
    echo "rtc tests FAILED (output saved as got.txt)"
    exit 1
fi
