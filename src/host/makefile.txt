# --- SYSTEM AUTO-DETECTION ---
ifeq ($(OS),Windows_NT)
    # Windows configurations (MinGW / GCC)
    CC = gcc
    CFLAGS = -Wall -Wextra -O2 -D_WIN32_WINNT=0x0601
    EXE = serial_test.exe
    RM = del /Q
    CLEAN_TARGET = $(EXE) *.o
else
    # POSIX configurations (Linux / macOS)
    CC = gcc
    CFLAGS = -Wall -Wextra -O2
    EXE = serial_test
    RM = rm -f
    CLEAN_TARGET = $(EXE) *.o
endif

# --- BUILD TARGETS ---

# Default rule: builds the executable
all: $(EXE)

# Link object files together into the final executable
$(EXE): serial_io.o serial_test.o
	$(CC) $(CFLAGS) -o $(EXE) serial_io.o serial_test.o

# Compile serial_io.c into an object file
serial_io.o: serial_io.c serial_io.h
	$(CC) $(CFLAGS) -c serial_io.c

# Compile serial_test.c into an object file
serial_test.o: serial_test.c serial_io.h
	$(CC) $(CFLAGS) -c serial_test.c

# Clean up build artifacts safely based on host OS rules
clean:
	$(RM) $(CLEAN_TARGET)

# Declare targets that do not correspond to actual physical file outputs
.PHONY: all clean
