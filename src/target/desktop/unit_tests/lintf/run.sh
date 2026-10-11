#!/bin/sh
# scripts/lintf.py test: lints bad.f, a fixture with one problem of each
# kind (and some lines that must pass), and compares the report with
# expected.txt. Then checks that forth/*.f is clean. Usage: run.sh
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$HERE/../../../../..
LINT="$ROOT/scripts/lintf.py"
if ! command -v python3 > /dev/null 2>&1; then
    echo "lintf tests skipped (no python3)"
    exit 0
fi
cd "$HERE"
{ python3 "$LINT" bad.f; echo "(exit $?)"; } > got.txt 2>&1
# --strip-trailing-cr: a Windows checkout may have given expected.txt CRLF endings
if ! diff -u --strip-trailing-cr expected.txt got.txt; then
    echo "lintf tests FAILED (output saved as got.txt)"
    exit 1
fi
rm -f got.txt
if ! (cd "$ROOT" && python3 scripts/lintf.py forth/*.f); then
    echo "lintf tests FAILED: forth/*.f doesn't follow doc/coding-rules.md"
    exit 1
fi
echo "lintf tests passed"
