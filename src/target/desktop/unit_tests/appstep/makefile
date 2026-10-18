# appstep tests: a full port with the app running (vmRunning), and the app's yeet handler

LF = ../../../../../bin/lf

.PHONY: all test clean

all: test

test: $(LF)
	python3 test_appstep.py $(LF)
	python3 test_yeet.py $(LF)

$(LF):
	$(MAKE) -C ../../../../.. bin/lf

clean:
