#!/bin/sh
# lf on a card image: it must find lfblocks.fb4 and show the same blocks
# as the file itself. Usage: run_lf.sh LF IMAGE FB4
LF=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
IMG=$(pwd)/$2
FB4=$3
blocks=$(( $(wc -c < "$FB4") / 4096 ))
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
cp "$FB4" "$tmp/copy.fb4"
cd "$tmp"
# load core.f from the blocks as block 1 does (into this temp dir's flash)
cmds='capacity .\nhex 7 block @ . 8 block 100 + @ . decimal\n0 open-flash 7 load 50 load close-flash\nbye\n'
got=$(printf "$cmds" | "$LF" -o 3 -k "$IMG" 2>&1)
rm -f lfflash.bin
want=$(printf "$cmds" | "$LF" -o 3 -k copy.fb4 2>&1)
echo "$got" | grep -q "blocks: lfblocks.fb4, $blocks blocks at sector" || { echo "run_lf: no lfblocks.fb4 found"; echo "$got"; exit 1; }
g=$(echo "$got" | grep -v "^blocks:\|^\[")
w=$(echo "$want" | grep -v "^blocks:\|^\[")
if [ "$g" != "$w" ]; then
    echo "run_lf: the card image and the file differ"
    echo "card: $g"
    echo "file: $w"
    exit 1
fi
echo "$g" | grep -q "value test: 11 10" || { echo "run_lf: loading core.f failed"; echo "$g"; exit 1; }
echo "run_lf: lf finds lfblocks.fb4 on the card image ($blocks blocks), reads the same data, and loads core.f from it"
