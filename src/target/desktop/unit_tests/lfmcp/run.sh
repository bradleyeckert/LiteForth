#!/bin/sh
# Test of scripts/lfmcp.py, the MCP server that gives an AI assistant
# LiteForth's terminal: test.py runs bin/lf on one pty and lfmcp.py on
# another, bridges them, and drives lfmcp.py with MCP requests.
# Usage: run.sh path/to/lf
set -e
LF=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
HERE=$(cd "$(dirname "$0")" && pwd)
if ! command -v python3 > /dev/null 2>&1; then
    echo "lfmcp tests skipped (no python3)"
    exit 0
fi
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"
TIMEOUT=""
command -v timeout > /dev/null 2>&1 && TIMEOUT="timeout 60"
$TIMEOUT python3 "$HERE/test.py" "$LF" "$HERE/../../../../../scripts/lfmcp.py"
