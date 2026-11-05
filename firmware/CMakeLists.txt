# ESP-IDF firmware project — matrix_clock
# Build (PowerShell): . "$HOME\esp\esp-idf\export.ps1" ; idf.py -C firmware build
# Flash: idf.py -C firmware -p COM21 flash monitor   (always pass the port: several boards are usually plugged in)
cmake_minimum_required(VERSION 3.16)

# Firmware version (semver). Published to Home Assistant and, in phase 2, to the hub.
# Bump before each release.
set(PROJECT_VER "0.1.0")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(matrix_clock)
