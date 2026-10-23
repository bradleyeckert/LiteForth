# lf -L 2: LiteForth's flash pages locked as at the CH32H417's SECURITY_LEVEL 2

LF = ../../../../../bin/lf

.PHONY: all test clean

all: test

test: $(LF)
	sh ./run.sh $(LF)

$(LF):
	$(MAKE) -C ../../../../.. bin/lf

clean:
	rm -f got.txt
