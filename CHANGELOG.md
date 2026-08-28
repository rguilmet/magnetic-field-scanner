# Changelog

## [v3.3.0] - 2026-08-28
### Added
- **Phase 6: Buttery Smooth Radar Dot:** Replaced the raw accelerometer tilt-compensation algorithm with the Madgwick Sensor Fusion quaternions. The UI Radar Dot is now completely stabilized by the gyroscope and immune to physical swinging/acceleration artifacts.
- **Wand-Frame Quaternions:** Shifted the imu_rotation_deg alignment to occur *before* the sensor fusion block. The Quaternions logged in the CSV now correctly represent the physical orientation of the 3D Wand rather than the internal PCB chip.
## [v3.2.3] - 2026-08-28
### Changed
- **Geiger Counter Mode (GCM) Architecture:** Completely rewrote GCM audio generation. It now runs inside the standard phase-accumulator loop as a mathematical 2ms impulse function. This allows GCM to inherit the global udio_gain, squelch, and EMA smoothing naturally, rather than relying on an isolated timing loop.
## [v3.2.2] - 2026-08-28
### Fixed
- **CSV Data Logging Bug:** Fixed an issue where the QW, QX, QY, QZ variables were missing from the snprintf argument list in data_logger.cpp, causing the logger to read deterministic garbage memory from the stack (resulting in frozen/NaN values in the CSV).
## [v3.2.1] - 2026-08-28
### Fixed
- **Madgwick AHRS NaN Error:** Replaced the legacy Fast Inverse Square Root hack with hardware sqrtf() to prevent GCC strict-aliasing optimizations from generating -nan quaternion values during compilation with -Os.
## [v3.2.0] - 2026-08-28
### Added
- **Quaternion Sensor Fusion (AHRS):** Integrated Madgwick filter for the QMI8658 to output precise q0, q1, q2, q3 orientation data.
- **Geiger Counter Mode (GCM):** Added a new audio mode that triggers discrete clicks with a logarithmic delay mapped to the gradient magnitude.
- **Enhanced Data Logging:** Added QW, QX, QY, and QZ quaternion values to the SD card CSV headers and telemetry rows.
## [v3.1.2] - 2026-08-28
### Fixed
- LVGL UI Bug: Constrained the Calibration Complete message box width to 160px to prevent horizontal overflow on the 172x640 portrait display.
All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]
### Added
- GPS tracking integration (Planned for `IO43`/`IO44`).
- Third Mid Sensor integration (Planned for `IO5`, Address `0x22`).

---

## [3.1.0] - 2026-08-28
### Changed
- (Architectural Refactoring Phase) Decomposed the monolithic `wifi_logger.cpp` into four single-responsibility modules: `settings_manager`, `data_logger`, `web_server`, and `screenshot`.
- Extracted IMU initialization and read logic from the I2C BSP into a dedicated `imu_bsp`.
- Created `mfs_api.h` to cleanly expose cross-module state functions, eliminating scattered `extern` declarations.
- Refactored manual calibration tare trigger to use a robust `volatile bool tare_requested` flag instead of relying on a fragile magic sentinel value (`0x7FFFFFFF`).
- Moved Battery ADC voltage reading out of the UI rendering loop into a dedicated background FreeRTOS task (`task_battery_monitor`).

---

## [3.0.42] - 2026-08-28
### Changed
- (Code Hygiene Phase) Removed duplicate `#include`s in `wifi_logger.cpp`.
- Replaced magic numbers in sensor task with named constants in `user_config.h`.
- Added `volatile` qualifier to variables shared across FreeRTOS tasks to prevent compiler optimizations from caching stale values.
- Cleaned up stale debug code and redundant declarations from `rtc_bsp.cpp` and `lvgl_port.c`.

---

## [3.0.41] - 2026-08-28
### Fixed
- Fixed duplicate `save_calibration()` call in `wifi_logger.cpp` which was overwriting identical data sequentially.
- Fixed a hot-path heap allocation bug in `i2c_write_buff()` (replacing `malloc` with a stack VLA) that occurred at 400Hz, greatly reducing heap fragmentation risk.

---

## [3.0.40] - 2026-08-27
### Changed
- Rebranded the entire project from `Magnetic_Field_Detector_Wand` to `Magnetic_Field_Scanner` to align with the main UI screen. 
- Updated the Web Server HTML to display "Magnetic Field Scanner Portal".
- Mass renamed all internal script and documentation references to reflect the new project structure.

---

## [3.0.39] - 2026-08-27
### Changed
- Refined the Screenshot Viewer web UI: The download button was moved to the top of the screen above the image to prevent scrolling on tall images, and the redundant filename title text was removed for a cleaner look.

---

## [3.0.38] - 2026-08-27
### Fixed
- Fixed an HTML routing bug where the Web UI root page still pointed `.bmp` links to the old `/download` endpoint instead of the newly created `/view` endpoint.

---

## [3.0.37] - 2026-08-27
### Fixed
- Re-architected screenshot web delivery. `.bmp` files now link to a dedicated viewer page (`/view`) instead of directly serving the image file. This viewer uses an HTML5 Canvas to instantly compress the raw BMP into a PNG purely on the client-side, completely neutralizing Chrome's HTTP Safe Browsing warnings when saving.

---

## [3.0.36] - 2026-08-27
### Fixed
- Fixed Chrome's "Keep" warning by removing the `download` HTML attribute from screenshot links in the Web UI. Clicking a screenshot now completely avoids the download pipeline and cleanly opens the image in a new tab (`target="_blank"`).

---

## [3.0.35] - 2026-08-27
### Fixed
- Fixed Chrome's "insecure download" warning for screenshots by serving `.bmp` files with the `image/bmp` MIME type and `inline` disposition. Clicking a screenshot link in the Web UI now safely opens the image directly in the browser.
- Fixed an issue where `lv_snapshot_take` would only capture the main HUD (Tile 1) by exposing the active TileView and capturing the dynamically active tile instead.

---

## [3.0.34] - 2026-08-27
### Changed
- Standardized file naming conventions across the firmware (`log_`, `calibration_`, `screenshot_`).
- Removed milliseconds from all filenames to improve file-explorer readability while preserving internal `.mmm` precision within the CSV log entries.

---

## [3.0.33] - 2026-08-27
### Added
- **On-Device Screenshots:** Long-pressing the BOOT button (`IO0`) for 1.5 seconds now dumps a 24-bit RGB888 pixel-perfect `.bmp` of the LVGL UI directly to the SD Card, bypassing PNG compression overhead.
- "Camera Shutter" double-beep audio cue for the new screenshot feature.

### Changed
- Configured LVGL (`lv_conf.h`) to use `LV_STDLIB_CLIB` and `LV_USE_SNAPSHOT 1` to leverage ESP32 PSRAM for large frame buffer allocations.

### Fixed
- **Audio Pitch-Bend Bug:** Bypassed the EMA filter in `task_audio_alert` for forced UI tones so they play instantly rather than sweeping up from 40Hz.
- **Missing Calibration Beep:** Added a 500ms `vTaskDelay` so the 1500Hz calibration completion beep is held long enough to be heard by the 32ms audio buffer chunking.

---

## [3.0.32] - 2026-08-27
### Added
- `critical_design_review.md` created to document architectural state and mechanical risks.
- `folder_review.md` created as a GitHub/Hackaday publishing roadmap.
- Comprehensive Python offline ecosystem (`analyze_log.py`, `calibrate_wand.py`, `generate_plots.py`) utilizing `argparse` and SemVer `v1.0.0`.
- Cycle Count (CC) scaling logic in `Magnetic_Field_Scanner.ino` to linearly scale `tip_hard` and `ref_hard` Center Offsets on the fly when the user alters the CC.

### Changed
- Hardware I2C0 speed (`scl_speed_hz`) hardcoded to `100000` (100kHz Standard Mode) to safely drive the 3-foot sensor wire harness.
- Refactored `wifi_logger.cpp` to correctly utilize the `kabsch_align_3x3` C++ array signatures without redundant heap allocations.
- Overhauled `project_overview.md` and `user_manual.md` with strict versioning headers, Tare operation mechanics, and exact data logging formats.

### Fixed
- **Sensor Lockup Bug:** Relaxed the I2C Watchdog. The system now requires >10 consecutive I2C failures (instead of 1) before triggering a hard `initRM3100()` reset.
- **Audio/Data Shriek Bug:** Implemented a mathematical Slew-Rate limit filter. If any axis jumps by >800 counts in a single 2.5ms frame, it is dropped as physical EMI ("cosmic meteor"), completely eliminating random audio spikes.
- **Calibration Crash:** Added `vTaskDelay(pdMS_TO_TICKS(10))` every 100 iterations inside the synchronous `wifi_logger.cpp` CSV string parsing loop to prevent the FreeRTOS Task Watchdog Timer (TWDT) from rebooting the core at 99%.






