# Unit tests for src/exfat.c, src/card.c and the card-image side of
# src/target/desktop/blocks.c, on images made by scripts/mkexfat.py
# (uses the vendored Unity framework)

SRC_DIR     = ../../../..
DESKTOP_DIR = ../..
UNITY_DIR   = ../unity
MKEXFAT     = python3 $(SRC_DIR)/../scripts/mkexfat.py

TARGET = test_exfat

CC     ?= gcc
CFLAGS  = -Wall -Wextra -std=c99 -g -I$(DESKTOP_DIR) -I$(SRC_DIR) -I$(UNITY_DIR)

SRC = $(SRC_DIR)/exfat.c $(SRC_DIR)/card.c $(DESKTOP_DIR)/blocks.c test_exfat.c $(UNITY_DIR)/unity.c

IMAGES = a.img b.img c.img d.img f.img
LF     = $(SRC_DIR)/../bin/lf
CORE   = $(SRC_DIR)/../forth/core.f

.PHONY: all test clean

all: test

$(TARGET): $(SRC) $(SRC_DIR)/exfat.h $(SRC_DIR)/card.h
	$(CC) $(CFLAGS) $(SRC) -o $@

# a: an MBR card with most kinds of entry
a.img: $(SRC_DIR)/../scripts/mkexfat.py
	$(MKEXFAT) $@ --label LITEFORTH --file notes.txt,size=1000 \
	  --file lfblocks.fb4,size=65536,pad=9 \
	  "--file=A long file name.dat,size=5000,deleted" \
	  --file frag.bin,size=20000,frag --file fatc.bin,size=20000,fat \
	  --file half.bin,size=65536,valid=30000 --file sub,size=4096,dir \
	  --file empty.txt,size=0 --file bad.txt,size=100,badsum > /dev/null
# b: no MBR, 512-byte clusters, a fragmented root, a file past cluster ends
b.img: $(SRC_DIR)/../scripts/mkexfat.py
	$(MKEXFAT) $@ --no-mbr --cluster 512 --root-frag \
	  --file x.bin,size=8192,pad=45 --file lfblocks.fb4,size=32768,nosig > /dev/null
# c: a fragmented lfblocks.fb4
c.img: $(SRC_DIR)/../scripts/mkexfat.py
	$(MKEXFAT) $@ --file lfblocks.fb4,size=40960,frag > /dev/null
# d: 32 KB clusters, a file that isn't whole blocks, VolumeDirty set
d.img: $(SRC_DIR)/../scripts/mkexfat.py
	$(MKEXFAT) $@ --size-mb 16 --cluster 32768 --dirty \
	  --file LFBLOCKS.FB4,size=100000 > /dev/null

# f: just lfblocks.fb4, for tests that spoil one field
f.img: $(SRC_DIR)/../scripts/mkexfat.py
	$(MKEXFAT) $@ --file lfblocks.fb4,size=65536 > /dev/null

# e: a block file built from core.f (as forth/build.bat does, without the
# fonts) on a card, for lf itself
core.fb4: $(CORE) $(SRC_DIR)/../scripts/putblocks.py
	python3 -c "import sys; open(sys.argv[1],'wb').write((b'LITEFORTH'+b' '*4087)+b' '*4096*63)" $@
	python3 $(SRC_DIR)/../scripts/putblocks.py $(CORE) $@ > /dev/null
e.img: $(SRC_DIR)/../scripts/mkexfat.py core.fb4
	$(MKEXFAT) $@ --file notes.txt,size=3000 --file LFBlocks.fb4=core.fb4 > /dev/null

test: $(TARGET) $(IMAGES) e.img
	./$(TARGET)
	sh ./run_lf.sh $(LF) e.img core.fb4

clean:
	rm -f $(TARGET) $(IMAGES) e.img core.fb4 scratch.img
