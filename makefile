# Executable name
TARGET = lf

# Source directories
SRC_DIRS = src src/host

# Tell Make where to look for source (.c) files
vpath %.c $(SRC_DIRS)

# Collect all .c files directly under src/ and src/host/ (non-recursive)
SRCS = $(wildcard $(addsuffix /*.c,$(SRC_DIRS)))

# Map source file paths to flat object file names in build/
# e.g., src/vm.c -> build/vm.o, src/host/blocks.c -> build/blocks.o
OBJS = $(addprefix $(BUILD_DIR)/, $(notdir $(SRCS:.c=.o)))

# Compiler and flags (-I adds both directories to header search paths)
CC = gcc
CFLAGS = -Wall -Wextra -O2 $(addprefix -I,$(SRC_DIRS))

# Directory for intermediate object files
BUILD_DIR = build

# Default target
all: $(TARGET) clean_build

# Link the executable
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@

# Pattern rule: vpath automatically locates the source file in src/ or src/host/
$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Post-build cleanup: Removes intermediate build directory after successful compilation
clean_build:
	rm -rf $(BUILD_DIR)

# Standard cleanup: Removes build directory AND final executable
clean: clean_build
	rm -f $(TARGET)

.PHONY: all clean_build clean