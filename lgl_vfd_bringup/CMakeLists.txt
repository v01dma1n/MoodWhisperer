# lgl_vfd_bringup — standalone bring-up project for the LGL Studio VFD.
#
# Separate IDF project so the main MoodWhisperer app (Sony PT6315 VFD, mood
# LEDs, ToF sensor) stays untouched and buildable. Only pulls in the new
# lgl_vfd_studio component from the shared components/ dir — no WiFi, no
# NVS, no NTP engine.
#
# Build:
#   cd lgl_vfd_bringup
#   idf.py set-target esp32
#   idf.py -p /dev/ttyUSB0 build flash monitor

cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS "${CMAKE_SOURCE_DIR}/../components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(lgl_vfd_bringup)
