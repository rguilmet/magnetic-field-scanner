# Contributing to Magnetic Field Scanner

Thank you for your interest in contributing to the Magnetic Field Scanner (MFS) project! 
This project is open-source under the GPLv3 license, and we welcome contributions from the community to help improve spatial magnetic field mapping.

## Code of Conduct

By participating in this project, you agree to abide by basic open-source community standards:
- Be respectful and constructive in issues and pull requests.
- Focus on the technical goals and engineering quality.

## How to Contribute

1. **Fork the Repository:** Create a personal fork on GitHub.
2. **Create a Branch:** Create a feature branch (git checkout -b feature/your-feature-name).
3. **Commit your Changes:** Follow semantic versioning and provide clear commit messages.
4. **Push to the Branch:** Push your changes to your fork.
5. **Open a Pull Request:** Submit a PR against the main branch with a detailed description of your changes.

## Development Setup

### Arduino IDE
- Install Arduino IDE 2.x
- Add ESP32 core via Board Manager (v3.0.0+ required).
- Select **ESP32S3 Dev Module**.
- Set the following critical board settings:
  - USB CDC On Boot: **Enabled**
  - PSRAM: **OPI PSRAM**
  - Flash Size: **16MB (128Mb)**
  - Partition Scheme: **16M Flash (3MB APP/9MB FATFS)**

### PlatformIO
A platformio.ini is provided for VSCode/PlatformIO users. It automatically resolves dependencies and fetches the correct ESP32 Arduino 3.x core environment.

## Coding Standards

- **RTOS Tasks:** Long-running operations MUST be placed in dedicated FreeRTOS tasks (Core 0 for Sensors/SD, Core 1 for LVGL UI).
- **LVGL Thread Safety:** Any interaction with the LVGL UI outside of lv_timer_handler must be wrapped in mfs_lvgl_lock() and mfs_lvgl_unlock().
- **Heap Allocations:** Avoid dynamic memory allocation (malloc, 
ew) inside hot paths (e.g. 400Hz sensor read loops). Use stack allocation or statically sized buffers to prevent fragmentation.
- **Versioning:** Any modification to code should bump the FIRMWARE_VERSION in user_config.h per SemVer rules (Major.Minor.Patch).

## Reporting Bugs

Please use the GitHub Issues tracker to report bugs. Include:
- Hardware revision
- Firmware version (found on the Web Server Portal or boot screen)
- Steps to reproduce
- Serial monitor logs if a crash occurs (Enable Serial Logging in settings).

Thank you for contributing to MFS!