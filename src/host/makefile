# Executable name
TARGET = lf

# Explicitly list source files (in the same directory as this Makefile)
SRCS = main.c forth.c vm.c serial_io.c

# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -O2

# Directory for intermediate object files
BUILD_DIR = build
OBJS = $(patsubst %.c, $(BUILD_DIR)/%.o, $(SRCS))

# Default target
all: $(TARGET) clean_build

# Link the executable
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@

# Compile source files to object files inside the build directory
$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Post-build cleanup: Removes the intermediate build directory after successful compilation
clean_build:
	rm -rf $(BUILD_DIR)

# Standard cleanup: Removes the build directory AND the final executable
clean: clean_build
	rm -f $(TARGET)

.PHONY: all clean_build clean
