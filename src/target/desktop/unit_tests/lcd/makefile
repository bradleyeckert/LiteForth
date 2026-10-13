# Unit tests for src/target/desktop/lcd.c, the ST7789 model, built headless
# (uses the vendored Unity framework). `make demo` builds lcd_demo, which
# opens the real window (Win32, or X11 if pkg-config finds it).

SRC_DIR     = ../../../..
DESKTOP_DIR = ../..
UNITY_DIR   = ../unity

TARGET = test_lcd

CC     ?= gcc
CFLAGS  = -Wall -Wextra -std=c99 -g -I$(DESKTOP_DIR) -I$(SRC_DIR) -I$(UNITY_DIR)

SRC = $(DESKTOP_DIR)/lcd.c test_lcd.c $(UNITY_DIR)/unity.c

ifeq ($(OS),Windows_NT)
DEMO_FLAGS = -lgdi32
else
X11_LIBS  := $(shell pkg-config --libs x11 2>/dev/null)
DEMO_FLAGS = -pthread $(if $(X11_LIBS),-DLCD_X11 $(shell pkg-config --cflags x11) $(X11_LIBS))
endif

.PHONY: all test demo clean

all: test

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -DLCD_HEADLESS $(SRC) -o $@

test: $(TARGET)
	./$(TARGET)

demo: lcd_demo.c $(DESKTOP_DIR)/lcd.c $(DESKTOP_DIR)/lcd.h
	$(CC) -Wall -Wextra -O2 -I$(DESKTOP_DIR) -I$(SRC_DIR) lcd_demo.c $(DESKTOP_DIR)/lcd.c $(DEMO_FLAGS) -o lcd_demo

clean:
	rm -f $(TARGET) $(TARGET).exe lcd_demo lcd_demo.exe test_lcd.ppm
