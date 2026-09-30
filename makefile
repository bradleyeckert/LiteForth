# Executable name and output directory
BIN_DIR = bin
TARGET = $(BIN_DIR)/lf

# Directory for intermediate object files
BUILD_DIR = build

# Source directories
SRC_DIRS = src src/target/desktop

# Tell Make where to look for source (.c) files
vpath %.c $(SRC_DIRS)

# Collect all .c files directly under src/ and src/target/desktop/ (non-recursive)
SRCS = $(wildcard $(addsuffix /*.c,$(SRC_DIRS)))

# Map source file paths to flat object file names in build/
# e.g., src/vm.c -> build/vm.o, src/target/desktop/blocks.c -> build/blocks.o
OBJS = $(addprefix $(BUILD_DIR)/, $(notdir $(SRCS:.c=.o)))

# Compiler and flags (-I adds both directories to header search paths)
# -MMD -MP generate header dependency files so edits to .h files trigger rebuilds
CC = gcc
CFLAGS = -Wall -Wextra -O2 $(addprefix -I,$(SRC_DIRS))
DEPFLAGS = -MMD -MP

# Unit test suites under src/target/desktop/unit_tests
UNIT_TEST_DIRS = $(sort $(dir $(wildcard src/target/desktop/unit_tests/*/makefile src/target/desktop/unit_tests/*/Makefile)))

# Default target
all: $(TARGET)

# Link the executable
$(TARGET): $(OBJS)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) $^ -o $@

# Pattern rule: vpath automatically locates the source file in SRC_DIRS
$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

# Run the Forth primitives regression script and all C unit tests.
# regression.f runs in a temp directory on copies of the flash and block
# images in bin/, so the tracked images are never modified.
test: $(TARGET)
	@tmp=$$(mktemp -d) && cp $(BIN_DIR)/lfflash.bin $(BIN_DIR)/lfblocks.bin $$tmp/ && \
	echo "cd $$tmp && $(CURDIR)/$(TARGET) -o 39 < scripts/regression.f" && \
	(cd $$tmp && $(CURDIR)/$(TARGET) -o 39 < $(CURDIR)/scripts/regression.f); \
	status=$$?; rm -rf $$tmp; [ $$status -eq 0 ] || { echo "regression.f failed (exit $$status)"; exit 1; }
	@for d in $(UNIT_TEST_DIRS); do \
		echo "== $$d"; \
		$(MAKE) -C $$d test || exit 1; \
	done

# Remove build products. Leaves bin/*.bin (tracked block and flash images) alone.
clean:
	rm -rf $(BUILD_DIR)
	rm -f $(TARGET) $(TARGET).exe
	@for d in $(UNIT_TEST_DIRS); do $(MAKE) -C $$d clean; done

-include $(OBJS:.o=.d)

.PHONY: all test clean
