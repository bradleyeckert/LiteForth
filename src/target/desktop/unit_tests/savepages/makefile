# save-pages test: LiteForth's flash pages copied into lfpages.bin for an
# app package, and the refusals (no file, too small, a page open)

LF = ../../../../../bin/lf

.PHONY: all test clean

all: test

test: $(LF)
	sh ./run.sh $(LF)

$(LF):
	$(MAKE) -C ../../../../.. bin/lf

clean:
