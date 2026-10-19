# lf on sealed block files (doc/encrypted-blocks.md): test_lf.py seals a
# copy of bin/lfblocks.fb4 with scripts/admin/sealblocks.py and runs bin/lf -K on
# it. Needs python3 and the cryptography package; skipped (with a note)
# without them.

LF = ../../../../../bin/lf

.PHONY: all test clean

all: test

test:
	@if python3 -c "import cryptography" 2>/dev/null; then \
		python3 test_lf.py $(LF); \
	else \
		echo "sealed lf tests skipped: python3 with the cryptography package not found"; \
	fi

clean:
	rm -rf __pycache__
