CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -I../../

SRC = ../../serial_io.c
TEST_SRC = test_serial_io.c
DEMO_SRC = demo_terminal.c

TEST_TARGET = test_serial_io
DEMO_TARGET = demo_terminal

all: $(TEST_TARGET) $(DEMO_TARGET)

$(TEST_TARGET): $(TEST_SRC) $(SRC)
	$(CC) $(CFLAGS) -o $(TEST_TARGET) $(TEST_SRC) $(SRC)

$(DEMO_TARGET): $(DEMO_SRC) $(SRC)
	$(CC) $(CFLAGS) -o $(DEMO_TARGET) $(DEMO_SRC) $(SRC)

run: $(TEST_TARGET)
	./$(TEST_TARGET)

demo: $(DEMO_TARGET)
	./$(DEMO_TARGET)

clean:
	rm -f $(TEST_TARGET) $(DEMO_TARGET) *.o

.PHONY: all run demo clean