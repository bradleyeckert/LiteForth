# LiteForth ESP32-C5 target (ESP-IDF application).
# Build with: idf.py set-target esp32c5 && idf.py build
# Needs ESP-IDF v5.x with ESP32-C5 support. See doc/ESP32C5/esp32c5-notes.md.
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(liteforth_esp32c5)
