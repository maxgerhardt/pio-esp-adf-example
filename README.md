# PlatformIO ESP-ADF example

## Current State

This project does **not** compile with the current version of the Espressif32 platform since that one is targeting a newer ESP-IDF version that what ESP-ADF was supporting at the time of writing this project. 

Per Issue #5 and all the recent notifications about forks and stars of this project, I'll take it upon myself to update this project to the latest possible versions, or at least restore compilability by targeting a lower `platform = espressif32@<version>`. Don't want to let y'all down.

## Description

See https://community.platformio.org/t/esp32-adf-audio-dev-framework-please-help/

## Current application code

The current app will wait for the wakeword "alexa", then light up all LEDs on the MSC_V2_2 board. 

## Building

Look into the `platformio.ini` to understand how building works and what you have to change.

## License 

All of the ESP-ADF code is a momentary snapshot of the repo (https://github.com/espressif/esp-adf/) on the 14.09.2019 **with
the latest ESP-IDF**.
.
 

All ESP-ADF code is property of Espressif.
