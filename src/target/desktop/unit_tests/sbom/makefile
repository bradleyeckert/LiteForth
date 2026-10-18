# The SBOM (sbom.spdx.json, doc/sbom.md) must match the repository:
# scripts/mksbom.py --check fails when a third-party file listed in it has
# changed, and --scan when a file carries a copyright notice that no
# component claims. Needs python3; skipped without it.

MKSBOM = ../../../../../scripts/mksbom.py

.PHONY: all test clean

all: test

test:
	@if command -v python3 > /dev/null 2>&1; then \
		python3 $(MKSBOM) --check && python3 $(MKSBOM) --scan; \
	else \
		echo "sbom tests skipped (no python3)"; \
	fi

clean:
