# The Requirements Traceability Matrix (doc/quality/README.md):
# scripts/rtm.py --check on the real items (every link resolves, the
# levels and parents are right, doc/quality/rtm.md is current), then
# test_rtm.py, which breaks copies of the items one way at a time and
# expects rtm.py to fail each time with the right message.

.PHONY: all test clean

all: test

test:
	@python3 ../../../../../scripts/rtm.py --check
	@python3 test_rtm.py

clean:
	rm -rf work __pycache__
