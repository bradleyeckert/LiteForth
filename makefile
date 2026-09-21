# Executable name
TARGET = lf

# Source directory containing the C files
SRC_DIR = src/host

# Explicitly list source files (prefixed with the source directory)
SRCS = $(SRC_DIR)/main.c $(SRC_DIR)/forth.c $(SRC_DIR)/vm.c $(SRC_DIR)/serial_io.c $(SRC_DIR)/utils.c $(SRC_DIR)/api0.c

# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -O2

# Directory for intermediate object files
BUILD_DIR = build

# Map source files in src/host to object files in build/
# e.g., src/host/main.c becomes build/main.o
OBJS = $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(SRCS))

# Default target
all: $(TARGET) clean_build

# Link the executable
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@

# Compile source files from the src/host directory into the build directory
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Post-build cleanup: Removes the intermediate build directory after successful compilation
clean_build:
	rm -rf $(BUILD_DIR)

# Standard cleanup: Removes the build directory AND the final executable
clean: clean_build
	rm -f $(TARGET)

.PHONY: all clean_build clean
