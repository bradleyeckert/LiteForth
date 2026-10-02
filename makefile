# Windows without a Unix shell: when make runs from PowerShell or cmd and no
# sh.exe is on the PATH, recipes run in cmd.exe, which has no mkdir -p, rm,
# mktemp etc. cmd expands %OS%, sh doesn't, which tells them apart.
# Override with `make WINCMD=` (or WINCMD=1) if the detection gets it wrong.
ifeq ($(OS),Windows_NT)
  ifeq ($(shell echo %OS%),Windows_NT)
    WINCMD ?= 1
  endif
endif

# Executable name and output directory
BIN_DIR = bin
ifdef WINCMD
TARGET = $(BIN_DIR)/lf.exe
else
TARGET = $(BIN_DIR)/lf
endif

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
# -pthread is for serial_io.c's reader thread; on Windows it uses Win32
# threads, so plain MinGW needs no pthread library.
CC = gcc
ifdef WINCMD
PTHREAD =
else
PTHREAD = -pthread
endif
CFLAGS = -Wall -Wextra -O2 $(PTHREAD) $(addprefix -I,$(SRC_DIRS))
DEPFLAGS = -MMD -MP

# Unit test suites under src/target/desktop/unit_tests
UNIT_TEST_DIRS = $(sort $(dir $(wildcard src/target/desktop/unit_tests/*/makefile src/target/desktop/unit_tests/*/Makefile)))

# Default target
all: $(TARGET)

ifdef WINCMD
MKDIR = if not exist $(subst /,\,$(1)) mkdir $(subst /,\,$(1))
else
MKDIR = mkdir -p $(1)
endif

# Link the executable
$(TARGET): $(OBJS)
	@$(call MKDIR,$(BIN_DIR))
	$(CC) $(CFLAGS) $^ -o $@

# Pattern rule: vpath automatically locates the source file in SRC_DIRS
$(BUILD_DIR)/%.o: %.c
	@$(call MKDIR,$(BUILD_DIR))
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

ifdef WINCMD
# cmd.exe: run regression.f in build\regression on copies of the images.
# The unit tests are shell scripts and need WSL or MSYS2.
WIN_RT = $(BUILD_DIR)\regression
test: $(TARGET)
	@if exist $(WIN_RT) rmdir /s /q $(WIN_RT)
	@mkdir $(WIN_RT)
	@copy /y $(BIN_DIR)\lfflash.bin $(WIN_RT) >NUL
	@copy /y $(BIN_DIR)\lfblocks.bin $(WIN_RT) >NUL
	cd $(WIN_RT) && ..\..\$(BIN_DIR)\lf.exe -o 39 < ..\..\scripts\regression.f
	@rmdir /s /q $(WIN_RT)
	@echo Unit tests skipped: they need a Unix shell (WSL or MSYS2).

clean:
	if exist $(BUILD_DIR) rmdir /s /q $(BUILD_DIR)
	if exist $(BIN_DIR)\lf.exe del /q $(BIN_DIR)\lf.exe
else

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
endif

-include $(OBJS:.o=.d)

.PHONY: all test clean
