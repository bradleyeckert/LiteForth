# The Requirements Traceability Matrix (doc/CH32H417/quality/README.md):
# scripts/rtm.py --check on the real items of each target's V-model (every
# link resolves, the levels and parents are right, the target's rtm.md is
# current), then test_rtm.py, which breaks copies of the items one way at a
# time and expects rtm.py to fail each time with the right message.

RTM = python3 ../../../../../scripts/rtm.py

.PHONY: all test clean

all: test

test:
	@$(RTM) --check
	@$(RTM) --check \
		--dir ../../../../../doc/ESP32C5/quality \
		--out ../../../../../doc/ESP32C5/quality/rtm.md \
		--board-tests ../../../../../doc/ESP32C5/admin/board-tests.md
	@python3 test_rtm.py

clean:
	rm -rf work __pycache__
