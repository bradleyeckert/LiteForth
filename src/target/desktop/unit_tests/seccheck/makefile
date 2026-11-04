# scripts/ESP32C5/seccheck.py, the ESP32-C5 security-config gate, checked on
# fixtures and on the committed production overlay (test_seccheck.py). Needs
# python3; skipped without it.

.PHONY: all test clean

all: test

test:
	@if command -v python3 > /dev/null 2>&1; then \
		python3 test_seccheck.py; \
	else \
		echo "seccheck tests skipped (no python3)"; \
	fi

clean:
	rm -rf __pycache__
