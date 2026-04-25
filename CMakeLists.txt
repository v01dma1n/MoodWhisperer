# MoodWhisperer — ESP-IDF top-level project file.
#
# Expects ESP-IDF >= 5.1.
# Build:
#   idf.py set-target esp32
#   idf.py menuconfig     (set WiFi, partition table, etc. under "MoodWhisperer")
#   idf.py build flash monitor

cmake_minimum_required(VERSION 3.16)

# Point IDF at our extra components directory.
set(EXTRA_COMPONENT_DIRS "${CMAKE_SOURCE_DIR}/components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(mood_whisperer)
