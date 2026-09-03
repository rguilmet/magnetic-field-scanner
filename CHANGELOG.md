# Changelog

## [v5.1.1] - 2026-09-03
### Fixed
- **Tip Sensor CC Initialization Failure:** Fixed the Tip sensor returning tiny incorrect values (e.g., `-28`) instead of valid physics. The previous `v5.0.9` firmware left the RM3100 running in Continuous Measurement Mode (CMM), causing I2C writes to the `REG_CCX` registers to silently fail on soft-reboot. `initRM3100` now strictly enforces `CMM = 0x00` (IDLE Mode) before writing to Cycle Count registers, ensuring hot-flashes never trigger a parasitic lockup state.
### Changed
- **UI Dynamic Scaling:** The UI Gradiometer gauge and color bands now dynamically scale based on whether `TARE` is active or not. When `TARE` is active, the baseline is exactly `0 nT`, so the gauge uses a highly sensitive `0 to 5,000 nT` range. When `RAW` is active, the baseline incorporates the physical misalignment offset of the sensors (e.g. `~3000 nT`), so the gauge widens to `0 to 25,000 nT`.

## [v5.1.0] - 2026-09-03
### Changed
- **Single Measurement Polling (POLL) Architecture:** Completely abandoned the RM3100's internal Continuous Measurement Mode (CMM). The ESP32 now manually orchestrates synchronized measurements by broadcasting `REG_POLL` across the I2C bus. This forces both the Tip and Reference sensors to sample the physical world at the exact same microsecond, eliminating the false Gradiometer noise caused by internal silicon timer drift. 
- **I2C Collision Fix:** By migrating to `POLL` mode, the ESP32 is mathematically guaranteed to only read the sensors when they are totally idle. This permanently eliminates the random lockups caused by the ESP32 attempting to read `REG_RESULTS` at the exact same moment the free-running CMM timer started a new measurement.
## [v5.0.9] - 2026-09-02
### Fixed
- **Idle Watchdog False Positives:** Fixed the hardware watchdog triggering "stuck data" panics when the wand was sitting perfectly idle on a desk. The sensor's noise floor is so low that it occasionally outputs 10 identical valid frames in a row. The watchdog now only triggers if it reads exactly `0,0,0` (a true I2C read failure).
- **Safe TMRC Mapping:** Fixed `DRDY` timeout panics caused by constantly bombarding the RM3100's internal state machine with 600Hz `TMRC` ticks while it was calculating slow Cycle Count physics. `initRM3100()` now utilizes a rigorous table to map the `TMRC` hardware register to a value mathematically guaranteed to be slower than the active Cycle Count execution time.

## [v5.0.8] - 2026-09-02
### Fixed
- **Absolute EMI Filter Drop:** Fixed the CC=3200 screen freeze. The extreme hardware gain of CC=3200 outputs raw values of ~120,000 LSB, which was immediately triggering the `MFS_MAX_GLITCH_MAGNITUDE` absolute EMI filter (previously set to `50,000`). The threshold was bumped to `250,000` to allow all valid CC=3200 frames to pass.

## [v5.0.7] - 2026-09-02
### Changed
- Increased FreeRTOS `xEventGroupWaitBits` timeouts in `SensorTask` from 500ms to 1000ms to theoretically accommodate the massive time it takes for CC=3200 to sweep its oscillators (~480ms max).

## [v5.0.6] - 2026-09-02
### Fixed
- **Blind I2C Recovery Crash:** Fixed a fatal flaw in the `SensorTask` timeout recovery block. It previously forced a blind `i2c_read_buff` on timeout, which permanently crashed the RM3100 if it was still busy. It now explicitly checks `digitalRead(DRDY) == HIGH` before performing the dummy recovery read.

## [v5.0.4] - 2026-09-02
### Fixed
- **I2C Driver Panics:** Reduced `i2c_data_pdMS_TICKS` from 5000ms to 50ms in the ESP-IDF driver to prevent I2C hanging from starving the `IDLE0` task and triggering Task Watchdog Panics. Hardware I2C timeouts now fail gracefully and trigger the `SensorTask` software recovery.

## [v5.0.0] - 2026-09-02
### Changed
- **Unified `nT` Architecture:** Completely decoupled the Calibration Matrix and Sensor Fusion math from the hardware Cycle Count gain. The firmware now immediately multiplies the raw LSB counts by the `0.38 / CC` scalar, conducting all spatial geometry and Kabsch alignments in physical nanoTeslas (`nT`). This permanently fixes the UI data drift when switching cycle counts.
- **I2C Bus Speed:** Bumped hardware I2C speed to `200kHz` (up from 100kHz). This provides the perfect "Goldilocks" bandwidth to poll both sensors at 150Hz without falling victim to RC time constant degradation on the 60-inch CAT6 cable.
- **Python Ecosystem:** Upgraded `calibrate_wand.py` to `v2.0.0` to support generating physical `nT`-native `calibration.json` matrices.

### Fixed
- **DRDY Lapping Lockup:** Fixed the ESP32 permanently freezing after an SD card SPI flush. The sensor would lap the blocked CPU and leave `DRDY` permanently `HIGH`, missing the `RISING` edge ISR trigger forever. `SensorTask` now implements an asynchronous `digitalRead()` bypass to catch these missed frames and resume operations.

## [v4.0.0] - 2026-08-30
### Changed
- **Zero-Latency ISR Architecture:** Ripped out the old blocking delays. Sensor acquisition is now completely event-driven by FreeRTOS Event Groups triggered directly by RM3100 `DRDY` hardware interrupts, allowing the core to sleep at 0% CPU until data is physically ready.
- **Dynamic Madgwick `dt`:** Eliminated time-dilation distortion across varying sampling rates (150Hz to 4.5Hz) by injecting a dynamic `micros()` timestamp into the Sensor Fusion loop.
- **SD Logging Decimation:** Intelligently decimates 150Hz readings down to 75Hz during SD Card writes to prevent SPI bus latency from dropping sensor frames.## [v3.6.26] - 2026-08-29
### Added
- **UI Tweaks (Calibration Screen):** Added an LVGL timer (`cal_msg_timer`) that automatically blanks the progress bar and resets the status label back to "Ready for Calibration" a few seconds after a calibration completes or is stopped, preventing the result text from lingering indefinitely.

## [v3.6.25] - 2026-08-29
### Changed
- **UI Tweaks (Calibration Screen):** Changed the Tile 2 title from "Calibration & Tracking" to simply "Calibration" for a cleaner look.

## [v3.6.24] - 2026-08-29
### Changed
- **UI Tweaks (Main Screen):** Separated the visual logic for `AUTO` tare and `TARE` on the main page title label. When Auto Tare is active, the title now reads `(AUTO)` in green. When Manual Tare is active, the title reads `(TARED)` in red.

## [v3.6.23] - 2026-08-29
### Changed
- **UI Tweaks (Calibration Screen):** Reduced the width of the calibration progress bar from 200px to 140px to fit on the 172px wide screen. Enabled long-mode wrapping on the calibration status text label and centered it to prevent text clipping during active sweeps.

## [v3.6.22] - 2026-08-29
### Fixed
- Fixed a C++ compile error in `settings_manager.cpp` caused by variable shadowing (`char ts[64]` conflicting with `JsonArray ts`).
## [v3.6.21] - 2026-08-29
### Added
- Added `"tip_soft_note"` to `calibration.json` generation (both Python and C++) to explicitly document that the `tip_soft` matrix includes the Kabsch rotational alignment.

## [v3.6.20] - 2026-08-29
### Fixed
- **CRITICAL:** Fixed the Kabsch physical alignment logic. The rotational matrix was previously being baked into the Reference Sensor, twisting it away from the physical IMU frame and causing the AHRS North/Attitude estimation to completely fail. The rotation is now correctly baked into the Tip Sensor, forcing the Tip to align with the Reference (and IMU).
### Added
- The on-wand JSON generator now outputs `calibration_date_ms`, `calibration_date`, and `matrix_version` tags to match the Python script output.

## [v3.6.19] - 2026-08-29
### Fixed
- Fixed compile errors related to missing `<lvgl.h>` includes and `is_calibrating` scope context.
- Changed the aborted calibration status text from `"Calibration Stopped"` to `"Stopped"`.

## [v3.6.18] - 2026-08-29
### Changed
- **UI Redesign (Main Screen):** Shrunk the scan button and moved it left to make room for a new unified TARE button. The new Tare button cycles smoothly through `RAW` ➔ `TARE` (Manual) ➔ `AUTO`.
- **UI Redesign (Calibration Screen):** Cleaned up Tile 2 by removing old tare switches. Replaced the intrusive message box popups with an embedded `cal_progress_bar` and `cal_status_label`.
- **Calibration Loop:** The "Calibrate" button now turns Red and toggles to "STOP" during active sweeps. Pressing it mid-sweep halts calibration and cleanly saves the partial log as `_stopped.csv`.
## [v3.6.17] - 2026-08-29
### Fixed
- **CRITICAL:** Fixed the in-wand C++ ellipsoid calibration algorithm failing to converge. A CSV column parsing offset error caused the tipZ and refZ values to be misaligned, resulting in a 0 variance flatline that instantly aborted the math solver.
- **CRITICAL:** Fixed a sign error in the denominator calculation of the soft-iron matrix extraction (`denom = 1.0 - offset` instead of `offset + 1.0`).

## [v3.6.16] - 2026-08-29
### Changed
- Converted the internal in-wand algebraic ellipsoid fitting algorithm in `matrix_math.h` to use `double` precision (64-bit) for improved numerical stability.
## [v3.6.15] - 2026-08-29
### Fixed
- **CRITICAL:** Fixed the Magnetometer NED Sign Inversion Bug. The magnetometer's Y and Z axes were missing their required negations, causing the Madgwick filter to fight the accelerometer (which thought +Z was down while the magnetometer thought -Z was down). Elevation tracking is now perfectly geometrically anchored to the wand frame.
- **CRITICAL:** Fixed `calibrate_wand.py` outputting `ref_offset` instead of `ref_hard`, which caused the firmware to silently ignore hard-iron calibrations.
- **Fixed:** The firmware now properly loads `imu_rotation_deg` from `calibration.json` rather than falling back to a hardcoded 60.0.
- **Fixed:** Removed a redundant double radian conversion in `MadgwickAHRS.c`.

### Python Ecosystem
- Established a new rule in `GEMINI.md` that all Python scripts (`calibrate_wand.py`, `golden_analysis.py`, etc.) must include a `__filename__` and `__version__` string at the top of the file that prints on execution. Python scripts now evolve on their own independent SemVer track and do not lockstep with the firmware's `user_config.h` version.
- Created `golden_analysis.py` (`v1.0.1`) to automatically generate 3D scatter plots and IMU/Euler time-series of calibration files.
- `analyze_log.py`, `calibrate_wand.py`, `generate_plots.py` upgraded to `v1.1.1`. `visualize_3d.py` upgraded to `v1.0.1`.

## [v3.5.8] - 2026-08-29
### Fixed
- **CRITICAL:** Implemented Gyroscope Fast Offset Compensation (FOC). The BMI270 Gyroscope exhibited a massive baseline bias (~-3.5 dps) on Z, causing the Madgwick filter to constantly integrate a false rotation and violently fight the magnetometer. gyr_offset is now extracted during PC Calibration and subtracted before fusion.
- **CRITICAL:** Fixed missing Azimuth, Elevation, and Declination variables in log_data signature which caused stack-garbage  .0, 0.0, 0.2 logging in CSVs.
## [v3.5.5] - 2026-08-28
### Added
- **Surveyor Mode:** Formally renamed "Yaw" to "Azimuth". Added "Elevation" (Pitch) to the CSV payload and live LVGL UI. The wand now functions as a digital theodolite.
## [v3.5.4] - 2026-08-28
### Fixed
- **NED Y-Axis Sign Error:** Fixed a sign typo in the Madgwick filter inputs (
ed_my was accidentally inverted) that caused the compass to behave wildly in magnetically noisy environments due to left/right handed coordinate system conflicts. 

### Added
- **CSV Data:** Appended Yaw and Declination directly to the end of the CSV logging payload.
## [v3.5.1] - 2026-08-28
### Added
- **UI:** Added a "MAG" (Magnetic North) or "TN" (True North) indicator label to the top right of the compass ring to denote if declination compensation is active.
## [v3.5.0] - 2026-08-28
### Added
- **Magnetic Declination:** Added mag_declination_deg to System Settings (default 0). This allows the firmware to output True/Geographic North instead of just Magnetic North.
- **Videogame Minimap UI:** Integrated a live, rotating compass ring directly onto the main Radar Arc. It dynamically animates N, E, S, W letters around the perimeter based on the 9-DOF Yaw heading. The top of the screen always represents the wand's forward physical direction (Heading-Up).
## [v3.4.1] - 2026-08-28
### Fixed
- **9-DOF Coordinate System Flip:** Fixed a massive inversion bug where the IMU inputs (North-West-Up) and the Magnetometer inputs were fighting the Madgwick filter's native expectation (North-East-Down). Translated all 9 sensor axes perfectly into the NED frame before sensor fusion. The Quaternions are now right-side up and physically accurate.
## [v3.4.0] - 2026-08-28
### Added
- **Phase 7: 9-DOF Compass Integration:** Upgraded the Madgwick filter from 6-DOF to 9-DOF. 
- Mapped the RM3100 Reference sensor (acting as an Earth compass) to the BMI270 IMU's coordinate system using a custom translation key. The Quaternions in the CSV log are now permanently anchored to Magnetic North, providing absolute 3D orientation (Pitch, Roll, and Yaw/Heading) for post-analysis mapping.
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













