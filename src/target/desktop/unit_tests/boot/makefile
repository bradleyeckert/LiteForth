# Regression test for booting from flash (-o 8) (runs the lf executable)

LF = ../../../../../bin/lf

.PHONY: all test clean

all: test

test: $(LF)
	sh ./run.sh $(LF)

$(LF):
	$(MAKE) -C ../../../../.. bin/lf

clean:
	rm -f got.txt
