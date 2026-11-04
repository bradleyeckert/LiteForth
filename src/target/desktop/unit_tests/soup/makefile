# The ESP32-C5 SOUP tooling: scripts/ESP32C5/soup_sbom.py (SBOM generator) and
# scripts/ESP32C5/cvecheck.py (CVE monitor), checked on the real manifest and
# on fixtures (test_soup.py). Offline and deterministic -- no network. Needs
# python3; skipped without it.

.PHONY: all test clean

all: test

test:
	@if command -v python3 > /dev/null 2>&1; then \
		python3 test_soup.py; \
	else \
		echo "soup tests skipped (no python3)"; \
	fi

clean:
	rm -rf __pycache__ fixtures/__pycache__
