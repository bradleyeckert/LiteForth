py .\..\scripts\mkfont.py -f NotoSansCJKsc-Regular.otf -s 24 -o cjk24.fnt --proof cjk24.png --blocks glyphs.txt
py .\..\scripts\mkfont.py -f NotoSansCJKsc-Regular.otf -s 48 -o cjk48.fnt --proof cjk48.png --blocks digits.txt
py .\..\scripts\putbin.py cjk24.fnt 100 ../bin/lfblocks.fb4
py .\..\scripts\putbin.py cjk48.fnt 120 ../bin/lfblocks.fb4
