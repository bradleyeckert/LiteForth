# Regression test for QUIT (the terminal loop) (runs the lf executable)

LF = ../../../../../bin/lf

.PHONY: all test clean

all: test

test: $(LF)
	sh ./run.sh $(LF)

$(LF):
	$(MAKE) -C ../../../../.. bin/lf

clean:
	rm -f got.txt
