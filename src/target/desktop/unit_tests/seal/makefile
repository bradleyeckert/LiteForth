# Tests scripts/admin/sealblocks.py (doc/encrypted-blocks.md): seal, check and open
# round trips with AES and SM4, and every refusal a board would make. Needs
# python3 and the cryptography package; skipped (with a note) without it.

.PHONY: all test clean

all: test

test:
	@if python3 -c "import cryptography" 2>/dev/null; then \
		python3 test_seal.py; \
	else \
		echo "seal tests skipped: python3 with the cryptography package not found"; \
	fi

clean:
	rm -rf __pycache__
