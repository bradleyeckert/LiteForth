py ../scripts/putblocks.py core.f ../bin/lfblocks.fb4
py ../scripts/putblocks.py block1.f ../bin/lfblocks.fb4
py ../scripts/putblocks.py block2_6.f ../bin/lfblocks.fb4
py ../scripts/mkfont.py -f NotoSansCJKsc-Regular.otf -s 24 -o cjk24.fnt --blocks glyphs.txt
py ../scripts/mkfont.py -f NotoSansCJKsc-Regular.otf -s 48 -o cjk48.fnt --blocks digits.txt
py ../scripts/putbin.py cjk24.fnt 100 ../bin/lfblocks.fb4
py ../scripts/putbin.py cjk48.fnt 120 ../bin/lfblocks.fb4
del *.fnt
echo "Copy lfblocks.fb4 to the first DA partition on your microSD card to use it on your nanoCH32H417 board"
