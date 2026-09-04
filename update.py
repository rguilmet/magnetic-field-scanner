import sys

with open("c:/Users/rguilmet/Documents/Arduino/Magnetic_Field_Scanner/README.md", "r", encoding="utf-8") as f:
    content = f.read()

# Fix Badges
old_header = "# Magnetic Field Scanner (ESP32-S3 + Dual RM3100)\n\nA professional-grade"
new_header = """# Magnetic Field Scanner (MFS) Wand

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)
[![Firmware](https://img.shields.io/badge/Firmware-v5.1.1-green.svg)]()
[![Platform](https://img.shields.io/badge/Platform-ESP32--S3-orange.svg)]()
[![Build](https://img.shields.io/badge/Build-Arduino%20|%20PlatformIO-lightgrey.svg)]()

A professional-grade"""
content = content.replace(old_header, new_header)

# Fix Hardware Section
old_hw = """## Hardware Specifications

* **MCU:** Espressif ESP32-S3 (Dual Core 240MHz, OPI PSRAM).
* **Magnetic Sensors:** Dual PNI RM3100 (TIP and REF) in a spatial gradiometer configuration.
* **IMU:** 6-DoF or 9-DoF IMU (for AHRS attitude tracking and radar projection).
* **Display:** 172x640 QSPI/8080 LCD driven by LVGL.
* **Storage:** External SPI SD Card + Internal FFat (Flash).
* **RTC:** PCF85063 for precise timestamping.
* **I/O Expander:** TCA9554 to offload static control pins (Backlight, Resets) and free up high-speed GPIO."""

new_hw = """## Hardware Configuration

### Core Components
* **Base Platform:** Waveshare ESP32-S3-Touch-LCD-3.49 v3 (PCBA v1.1 silkscreen). This highly integrated device provides the MCU, display, audio, and power management core.
* **MCU:** ESP32-S3 (16MB Flash, OPI PSRAM) embedded on the Waveshare board.
* **Magnetometers:** 2x RM3100 (TIP (at 0") and REF (at 24") sensors), with hardware reserved for a 3rd (NEAR at 8").
* **IMU:** QMI8658 (6-axis Accelerometer & Gyroscope) offset mechanically by 60° relative to the wand axis.
* **Display:** 172x640 QSPI LCD with capacitive touch.
* **Audio Codec:** ES8311 / ES7210 via I2S for audio feedback.
* **Storage:** External SPI SD Card + Internal FFat (Flash).
* **RTC:** PCF85063 for precise timestamping.
* **I/O Expander:** TCA9554 to offload static control pins (Backlight, Resets) and free up high-speed GPIO."""
content = content.replace(old_hw, new_hw)

with open("c:/Users/rguilmet/Documents/Arduino/Magnetic_Field_Scanner/README.md", "w", encoding="utf-8") as f:
    f.write(content)
