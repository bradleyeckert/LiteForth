# Target executable
TARGET = test_flash

# Compilers & Flags
CC ?= gcc
CFLAGS = -Wall -Wextra -std=c99 -g -I../../ -I.
LDFLAGS =

# Source files
SRC = ../../flash.c \
      test_flash.c \
      unity.c

OBJ = $(SRC:.c=.o)

.PHONY: all clean test

all: test

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

test: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(OBJ) $(TARGET) lfflash.bin test_flash.bin