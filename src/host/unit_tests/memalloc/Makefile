CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -I../..

TARGET = test_memalloc
SRC = test_memalloc.c ../../memalloc.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $^ -o $@

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all run clean
