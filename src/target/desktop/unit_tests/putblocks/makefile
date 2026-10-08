# scripts/putblocks.py test (runs the lf executable)

LF = ../../../../../bin/lf

.PHONY: all test clean

all: test

test: $(LF)
	sh ./run.sh $(LF)

$(LF):
	$(MAKE) -C ../../../../.. bin/lf

clean:
	rm -f got.txt
