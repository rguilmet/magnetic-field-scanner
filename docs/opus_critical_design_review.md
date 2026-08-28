# Opus Critical Design Review - Magnetic Field Scanner

**Document Version:** `v1.0.0`
**Date:** August 28, 2026
**Firmware Target:** `v3.0.40`
**Reviewer:** Independent Software Architecture Review (Opus)
**Scope:** Full codebase audit - firmware, companion scripts, documentation, and project structure

---

## 1. Executive Summary

The Magnetic Field Scanner (MFS) is a remarkably ambitious embedded systems project that successfully integrates dual geomagnetic sensors, a 6-axis IMU, real-time DSP, on-device calibration with Kabsch rotational alignment, a full LVGL touchscreen UI, audio feedback, Wi-Fi web portal, and dual-filesystem data logging - all running on a single ESP32-S3 under FreeRTOS.

For a project at this stage of maturity, the engineering substance is genuinely impressive. The physics are sound, the math is correct, and the system demonstrably works in the field. However, when examined through the lens of a professional peer review on GitHub and Hackaday, the codebase exhibits patterns common to rapid prototyping - monolithic files, implicit coupling between subsystems, and inconsistent code hygiene - that would benefit from targeted refactoring before public release.

This review is structured as **The Good, The Bad, and The Ugly**, followed by concrete recommendations for improving detection performance, accuracy, and usability.

---

## 2. The Good - What This Project Gets Right

### 2.1 On-Device Calibration Pipeline (Outstanding)

The crown jewel of this project is the self-contained calibration pipeline in `matrix_math.h`. Implementing a full ellipsoid fitting algorithm (9-parameter least-squares with Tikhonov regularization), a Jacobi eigenvalue solver, and Kabsch rotational alignment *entirely on an MCU* - with no external dependencies - is a serious engineering achievement. The code is numerically stable, uses double precision where needed (the 9x9 solver), and correctly handles reflection detection in the SVD decomposition. This puts the project in a different league from typical hobbyist magnetometer projects.

### 2.2 Multi-Layer Signal Filtering (Excellent)

The firmware implements a thoughtful, multi-stage filtering strategy that demonstrates deep understanding of the sensor physics:

1. **Slew-Rate Filter** (Magnetic_Field_Scanner.ino:336-354) - Rejects physically impossible >800-count inter-frame jumps caused by I2C EMI bit-flips.
2. **Absolute Magnitude Filter** (Magnetic_Field_Scanner.ino:372-376) - Clamps values beyond the physical range of Earth's field.
3. **Stuck-Sensor Watchdog** (Magnetic_Field_Scanner.ino:328-365) - Detects both stuck data and DRDY timeout conditions, triggering automatic sensor reset.
4. **Auto-Tare Low-Pass** (Magnetic_Field_Scanner.ino:422-438) - Brilliant conditional drift compensation that only engages below a 50-count threshold, preventing it from eating real anomalies.
5. **Audio EMA Smoothing** (Magnetic_Field_Scanner.ino:644-649) - With a deliberate bypass path for forced system tones.

Each layer addresses a distinct physical failure mode. This is exactly how professional sensor firmware should be designed.

### 2.3 FreeRTOS Task Architecture (Good)

The system correctly isolates concerns across three pinned FreeRTOS tasks:

| Task | Core | Priority | Responsibility |
|:---|:---|:---|:---|
| `SensorTask` | 0 | 2 | I2C polling, DSP, calibration math, logging |
| `DisplayTask` | 1 | 1 | LVGL UI updates from queue |
| `AudioTask` | 1 | 1 | Continuous I2S audio synthesis |

The use of `xQueueOverwrite` for the sensor-to-UI and sensor-to-audio data paths is an excellent pattern for "latest value" semantics - it guarantees the consumer always gets the most recent reading without blocking the producer.

### 2.4 Robust Filesystem Strategy (Good)

The dual-drive architecture with smart fallback (wifi_logger.cpp:8-13) is well-designed for field use. The ability to transparently load `settings.json` and `calibration.json` from FFat when a new SD card is inserted prevents a common field failure mode.

### 2.5 Documentation Suite (Good)

The project includes a `README.md`, `CHANGELOG.md`, `user_manual.md`, `project_overview.md`, and a prior `critical_design_review.md`. This is significantly above average for an open-source embedded project. The `CHANGELOG.md` is particularly well-maintained with granular per-version entries.

---

## 3. The Bad - Technical Debt and Code Quality Issues

### 3.1 Monolithic God-Files

The three primary source files carry disproportionate responsibility:

| File | Lines | Responsibility Count |
|:---|:---|:---|
| Magnetic_Field_Scanner.ino | 864 | Sensor init, I2C comms, audio init, ADC, FreeRTOS tasks, calibration orchestration, screenshot trigger, power management |
| wifi_logger.cpp | 855 | Settings persistence, calibration I/O, HTTP server, file management, timestamp formatting, BMP screenshot capture, data logging, calibration math orchestration |
| lvgl_port.c | 1034 | LCD driver init, SPI config, touch input, LVGL display init, all UI widget creation, all event callbacks, all UI update logic, battery ADC reading |

`wifi_logger.cpp` is especially problematic. Its name suggests it handles Wi-Fi logging, but it actually owns calibration file I/O, settings persistence, screenshot capture, timestamp formatting, and the entire HTTP web server. This violates the principle of least astonishment - a reviewer looking for "where does calibration get saved?" would never intuitively search `wifi_logger.cpp`.

**Recommendation:** Decompose into purpose-named modules:
- `settings_manager.cpp` - JSON settings and calibration persistence
- `web_server.cpp` - HTTP endpoints and HTML generation
- `data_logger.cpp` - CSV logging and timestamp formatting
- `screenshot.cpp` - BMP capture and LVGL snapshot logic

### 3.2 Duplicate Include Statements

wifi_logger.cpp contains blatant duplicate includes. `FFat.h`, `esp_heap_caps.h`, `dirent.h`, `sys/stat.h`, `WiFi.h`, and `WebServer.h` are each included two or three times. This is a clear sign of incremental copy-paste development. While harmless due to include guards, it signals to reviewers that the file has not undergone a cleanup pass.

### 3.3 Duplicate Function Call - Critical Bug

wifi_logger.cpp lines 715-716 contain:

`save_calibration(ref_center, ref_soft, tip_center, tip_soft);`
`save_calibration(ref_center, ref_soft, tip_center, tip_soft);`

`save_calibration()` is called **twice** in succession. This writes the calibration JSON to the filesystem twice, wasting I/O cycles and increasing wear on the flash/SD card. More critically, it suggests the second call was accidentally introduced and never caught - a symptom of missing code review.

### 3.4 Magic Numbers

The codebase contains numerous unexplained numeric literals:

| Location | Value | What it means |
|:---|:---|:---|
| .ino:50 | `42, 5000, 8` | Backlight pin, PWM freq, bit depth |
| .ino:84 | `20.0` | Audio volume percentage |
| .ino:343-344 | `800` | Slew-rate threshold (counts) |
| .ino:430 | `50.0f` | Auto-tare engagement threshold |
| .ino:435 | `0.995f, 0.005f` | Auto-tare EMA coefficients |
| .ino:450 | `0.38f` | RM3100 counts-to-nT conversion factor |
| .ino:521 | `0.0666f, 0.05416f` | Audio gain curve coefficients |
| .ino:657 | `8192` | Square wave amplitude (attenuation) |

While many of these are commented inline, they should be promoted to named constants in `user_config.h` for maintainability.

### 3.5 extern "C" Interface Inconsistency

The C/C++ boundary is handled inconsistently. Some functions use `extern "C"` blocks in the header (wifi_logger.h:38-59), which is correct. But many functions in `lvgl_port.c` rely on ad-hoc `extern` declarations scattered throughout the file body. Note that `toggle_mute` is declared twice (lines 362 and 395). These should be consolidated into a single `mfs_api.h` header that defines the cross-module interface contract.

### 3.6 volatile Usage

Several global variables use `volatile` correctly (`is_calibrating`, `force_audio_tone`, `pending_cycle_count`), but others that are shared across tasks do not:

- `is_scanning` - Written from UI task, read from sensor task. **Should be volatile or use atomic operations.**
- `is_muted` - Same pattern.
- `current_audio_gain` - Same pattern.
- `auto_tare_enabled` - Same pattern.

On ESP32-S3 with GCC, this rarely causes observable bugs due to the coherent cache architecture, but it is technically undefined behavior per the C++ standard and will raise flags in static analysis tools.

### 3.7 Battery ADC Reading Misplaced

The battery voltage ADC read is buried deep inside `update_detector_ui()` in lvgl_port.c:950-967, nested inside a conditional block that updates the nT label. This means battery voltage is only read when the magnitude label is being updated. It should be extracted to a timer callback or its own periodic task with a configurable read interval (e.g., every 5 seconds).

---

## 4. The Ugly - Architectural Risks and Anti-Patterns

### 4.1 The Sensor Task is a 340-Line Superfunction

`task_sensor_read` (Magnetic_Field_Scanner.ino:245-583) is a single function spanning **340 lines** that handles:

1. Deferred cycle count hardware updates (lines 254-314)
2. I2C sensor reads (lines 324-326)
3. Stuck-sensor watchdog (lines 328-365)
4. Zero-value rejection (lines 367-370)
5. Slew-rate EMI filtering (lines 336-354)
6. Absolute magnitude filtering (lines 372-376)
7. Hard/soft iron calibration application (lines 388-404)
8. Manual tare logic (lines 410-420)
9. Auto-tare logic (lines 422-438)
10. Gradient computation (lines 442-447)
11. nT conversion (line 450)
12. IMU read and rotation alignment (lines 452-463)
13. Gravity vector projection (lines 465-475)
14. Ferrous pin detection (lines 477-481)
15. Horizontal planar decomposition (lines 483-498)
16. UI data struct population (lines 500-510)
17. Audio frequency calculation (lines 519-524)
18. CSV data logging (line 526)
19. On-wand calibration orchestration (lines 528-563)
20. Queue publish (line 570)

This violates the Single Responsibility Principle in the extreme. It is nearly impossible to unit test any of these subsystems in isolation, and a bug in any one stage requires mentally parsing all 340 lines to trace data flow.

### 4.2 Web Server HTML Generation is Inline C++ Strings

The entire Web UI - including the root page, upload forms, directory listings, and the screenshot viewer - is generated by concatenating `String` objects inside C++ functions (wifi_logger.cpp:287-343, 399-422). This is:

1. **Unmaintainable** - Any HTML/CSS change requires modifying escaped C strings.
2. **Memory-hostile** - Each `+=` on an Arduino `String` triggers a `realloc()`, fragmenting the heap during serving.
3. **Non-reviewable** - The HTML is invisible to linters, formatters, and syntax highlighters.

For a Hackaday/GitHub audience, consider moving HTML templates to `PROGMEM` const char arrays or serving static files from the filesystem.

### 4.3 Mixed Naming Conventions

The codebase exhibits at least four distinct naming conventions:

| Convention | Examples | Origin |
|:---|:---|:---|
| `snake_case` | `task_sensor_read`, `cal_config`, `log_data` | Project custom code |
| `camelCase` | `readSensor`, `initRM3100`, `handleRoot` | Arduino/Java style |
| `PascalCase` | `MagData`, `TouchInputReadCallback` | Struct/class names |
| `mfs_` prefix | `mfs_lvgl_lock`, `mfs_button_pwr_task` | Namespacing attempt |

The `mfs_` prefix is a good instinct for namespacing in C, but it is applied inconsistently. Functions like `readSensor`, `initRM3100`, and `audio_init` should also carry the prefix for consistency.

### 4.4 The 0x7FFFFFFF Sentinel Pattern

The manual tare trigger uses a magic sentinel value:

`calibration_offset.x = 0x7FFFFFFF; // Triggers manual tare in sensor task`

This overloads a data field as a control signal, creating a fragile implicit contract between the UI and sensor tasks. If any future code path accidentally sets `calibration_offset.x` to `INT32_MAX`, it will silently trigger a tare operation. A dedicated `volatile bool tare_requested = false;` flag would be safer and self-documenting.

### 4.5 No Error Propagation Model

Most functions return `void` and handle errors by printing to Serial. In field deployment without a Serial connection, these errors are invisible. Consider implementing a simple error state that the UI can display (e.g., a red banner on the status bar).

---

## 5. Coding Consistency Scorecard

| Criterion | Rating | Notes |
|:---|:---|:---|
| **Commenting** | 4/5 | Most non-trivial blocks have inline comments. Some are excellent. Missing function-level docstrings. |
| **Naming** | 2/5 | Mixed conventions across files. No consistent namespace prefix. |
| **File Organization** | 2/5 | Monolithic god-files. Misnamed modules (wifi_logger does calibration). |
| **Error Handling** | 2/5 | Serial.println only. No user-visible error reporting. |
| **Thread Safety** | 3/5 | Mutexes used for LVGL and logging. Missing volatile/atomics on shared state. |
| **Magic Numbers** | 2/5 | Numerous unexplained literals in the sensor and audio tasks. |
| **Documentation** | 4/5 | Strong README, changelog, and user manual. Missing API-level docs in headers. |
| **Build Reproducibility** | 4/5 | Clear board settings documented. Missing platformio.ini or CI config. |
| **Version Control** | 5/5 | Strict semver discipline. Every change bumps. Exemplary changelog. |

---

## 6. Possible Improvements and Features

### 6.1 Improving Detection Range

- **3rd RM3100 Sensor (MID):** The hardware is already reserved (GPIO 5, address 0x22). Adding a third sensor at an asymmetric spacing enables **second-order gradiometry**, which can mathematically solve for the depth and distance of a magnetic anomaly rather than just its presence. This is the single highest-impact hardware upgrade.
- **Higher Cycle Counts:** The RM3100 supports cycle counts up to 800, which increases sensitivity at the cost of sample rate. Adding a user-selectable "High Sensitivity" mode (CC=800, ~9Hz) for slow, deliberate sweeps would improve detection of deeply buried targets.
- **Sensor Averaging:** Implementing configurable N-sample averaging (e.g., 4x or 8x) at lower update rates would reduce noise floor by sqrt(N), improving detection of weak anomalies at the cost of temporal resolution.

### 6.2 Improving Accuracy

- **Temperature Compensation:** The RM3100 has a known temperature coefficient. Adding a thermistor or using the QMI8658's internal temperature sensor to apply a temperature correction curve would reduce drift during long field sessions.
- **Cross-Axis Leakage Compensation:** The current soft-iron matrix handles ellipsoidal distortion, but does not model frequency-dependent eddy current effects from the battery and LCD. A runtime "delta calibration" that subtracts the gradient measured at rest from all subsequent readings could improve absolute accuracy.
- **Kalman Filter:** Replacing the simple EMA auto-tare with a proper Kalman filter (prediction + correction) would provide mathematically optimal drift tracking while preserving sharp anomaly edges.

### 6.3 Improving Usability

- **Data Visualization on Device:** Adding a scrolling time-series strip chart to a 4th tile would let field operators see the last 30 seconds of gradient magnitude history, making it easier to distinguish real anomalies from motion artifacts.
- **Audio Tone Profiles:** Offering selectable audio profiles (e.g., "Utility Locator" with aggressive threshold, "Archaeological Survey" with sensitive threshold) would make the tool accessible to different use cases without requiring the user to understand the gain curve math.
- **OTA Firmware Updates:** Implementing ESP32 OTA (Over-The-Air) updates via the existing Wi-Fi infrastructure would allow firmware updates without physical USB access - critical for deployed field instruments.
- **Screenshot Auto-Download:** When a screenshot is taken via the BOOT button, the web portal could auto-refresh or push a notification to connected browsers, eliminating the need to manually navigate to the file list.
- **QR Code on Screen:** Displaying a QR code of the device's IP address on the System and Hardware page would allow instant phone connection without manually typing the IP.

### 6.4 Improving Maintainability for Open Source

- **Add a platformio.ini:** This would allow contributors to build the project with a single command (`pio run`) without manually configuring Arduino IDE board settings.
- **Add a CONTRIBUTING.md:** Document the naming conventions, version bumping rules, and code review expectations.
- **Add a LICENSE clarification:** The project has a `LICENSE` file, but the README does not mention the license type. Add a badge and a one-line summary.
- **Automated Linting:** Adding a `.clang-format` configuration and a pre-commit hook would enforce consistent formatting across contributions.

---

## 7. Summary and Recommendation

The Magnetic Field Scanner is a genuinely impressive piece of embedded engineering. The on-device calibration pipeline alone - with ellipsoid fitting, eigenvalue decomposition, and Kabsch alignment running natively on an ESP32 - sets it apart from the vast majority of open-source sensor projects. The multi-layer signal filtering demonstrates real understanding of sensor physics, and the overall system integration (sensors, IMU, display, audio, Wi-Fi, dual filesystems) is ambitious and functional.

For Hackaday and GitHub publication, the **substance is strong** but the **presentation needs polish**. The highest-impact changes before publication would be:

1. **Clean up wifi_logger.cpp** - Remove duplicate includes and the duplicate `save_calibration()` call.
2. **Extract magic numbers** to named constants in `user_config.h`.
3. **Add a CONTRIBUTING.md** with coding standards.
4. **Add function-level docstrings** to the public API functions in headers.

These are cosmetic but significant for first impressions. The core architecture and physics are publication-ready as-is.

---

## 8. Driver Layer Review (src/ Subdirectory)

The `src/` directory contains a mix of project-authored Board Support Package (BSP) drivers and vendored Espressif SDK code. This section reviews each.

### 8.1 Classification: Project Code vs. Vendor Code

| Folder | Type | Lines | Origin |
|:---|:---|:---|:---|
| `i2c_bsp/` | **Project** | 161+29 | Custom I2C bus init, read/write wrappers, IMU driver |
| `PCF85063/` | **Project** | 80+24 | RTC wrapper around `SensorPCF85063.hpp` library |
| `QMI8658/` | **Project** | 93+21 | IMU wrapper around `SensorQMI8658.hpp` library |
| `sdcard/` | **Project** | 115+8 | SD card SDMMC driver (appears to be legacy/unused) |
| `lcd_bl_bsp/` | **Project** | 47+30 | LCD backlight LEDC PWM driver |
| `matrix_math/` | **Project** | 323 | Calibration math (reviewed in Section 2.1) |
| `tca9554/` | Vendor | ~300 | Espressif IO Expander SDK component |
| `touch/` | Vendor | ~434 | Espressif LCD Touch SDK component |
| `codec_board/` | Vendor | ~700 | Espressif Audio Codec Board abstraction |
| `esp_codec_dev/` | Vendor | ~2000 | Espressif Codec Device library (ES8311/ES7210) |

> **Note:** Vendor code (`tca9554`, `touch`, `codec_board`, `esp_codec_dev`) is Espressif-authored and MIT-licensed. It should not be modified and is excluded from the code quality review.

### 8.2 i2c_bsp — I2C Bus Abstraction (Project Code)

**Files:** `i2c_bsp.c` (161 lines), `i2c_bsp.h` (29 lines)

This is the lowest-level hardware abstraction in the project. It initializes both I2C buses (Bus 0 for sensors, Bus 1 for touch) and provides read/write wrappers used by every peripheral.

**Findings:**

- **Heap allocation in the hot path (Critical):** `i2c_write_buff()` calls `malloc()` and `free()` on every write when a register address is provided (lines 68-75). This function is called at 400Hz from the sensor task. While ESP32 malloc is fast, this creates 400 allocation/free cycles per second, contributing to heap fragmentation during long field sessions. This should use a stack-allocated buffer instead:
  ```c
  uint8_t pbuf[len+1];  // VLA on stack, max ~10 bytes for RM3100
  ```

- **Hardcoded bus handle assumption:** `i2c_write_buff()` and `i2c_read_buff()` always call `i2c_master_bus_wait_all_done()` on `user_i2c_port0_handle`, even though `i2c_master_touch_write_read()` uses `user_i2c_port1_handle`. If a device on Bus 1 ever calls `i2c_write_buff()`, it will wait on the wrong bus.

- **IMU code lives in the wrong file:** `imu_init()` and `imu_read()` are defined in `i2c_bsp.c` but they are IMU-specific logic. They should live in `QMI8658/qmi_bsp.cpp`. This was likely done to keep IMU access in pure C (avoiding the C++ `SensorQMI8658.hpp` dependency), which is a pragmatic choice but violates module boundaries.

- **IMU init overrides QMI library:** The `imu_init()` function in `i2c_bsp.c` writes raw register values (0x03=4g/500Hz, 0x04=512dps/500Hz) directly via I2C. However, `qmi_bsp.cpp` also initializes the IMU using the `SensorQMI8658` C++ library with *different* settings (4g/1000Hz, 64dps/897Hz). Both are called during setup. The raw-register version in `i2c_bsp.c` wins because it runs later. This dual-init is confusing and means `qmi_bsp.cpp` is effectively dead code at runtime (see 8.3).

- **Magic numbers in IMU config:** Register addresses `0x02`, `0x03`, `0x04`, `0x08` and config values `0x60`, `0x13`, `0x53`, `0x03` are unexplained. The inline comments help but named constants would be better.

### 8.3 QMI8658 — IMU BSP Wrapper (Likely Dead Code)

**Files:** `qmi_bsp.cpp` (93 lines), `qmi_bsp.h` (21 lines)

**Findings:**

- **Likely unused at runtime:** The main sketch calls `imu_init()` (from `i2c_bsp.c`) and `imu_read()` (also from `i2c_bsp.c`), not `qmi_init()` or `i2c_qmi_get()`. This entire module appears to be leftover from the original Waveshare demo code. It initializes the IMU via the `Wire` library (Arduino software I2C) rather than the ESP-IDF hardware I2C bus, which would conflict with the project's hardware I2C architecture.

- **Header guard collision:** `qmi_bsp.h` uses `#ifndef RTC_BSP_H` — this is a copy-paste error from `rtc_bsp.h`. It should be `#ifndef QMI_BSP_H`. This hasn't caused a bug only because no file includes both headers simultaneously.

- **Struct typo:** The struct is named `QmiDate_t` (should be `QmiData_t`).

- **Recommendation:** Either delete this module entirely (since `i2c_bsp.c` handles the IMU) or migrate `imu_init()`/`imu_read()` into this module and remove the raw-register IMU code from `i2c_bsp.c`.

### 8.4 PCF85063 — RTC BSP Wrapper (Clean)

**Files:** `rtc_bsp.cpp` (80 lines), `rtc_bsp.h` (24 lines)

**Findings:**

- **Good pattern:** Uses a custom I2C callback (`rtc_i2c_custom_cb`) to bridge the `SensorPCF85063` library to the project's hardware I2C bus. This is a clean adapter pattern.

- **Redundant extern:** Line 13 declares `extern i2c_master_dev_handle_t rtc_dev_handle;` but this is already declared in `i2c_bsp.h` which is included on line 11.

- **Stale test code:** `i2c_rtc_loop_task()` is a debug function with a `printf` format string that has 7 format specifiers but passes 7 arguments (the `getWeek()` value won't print because there's no `%d` for it). This function appears unused but should be removed or fixed for publication.

- **Missing `extern "C"` guards** in the header. The functions are called from both C and C++ translation units.

### 8.5 sdcard_bsp — SD Card SDMMC Driver (Legacy/Unused)

**Files:** `sdcard_bsp.cpp` (115 lines), `sdcard_bsp.h` (8 lines)

**Findings:**

- **Appears entirely unused:** The main sketch initializes the SD card using the Arduino `SD.begin()` API (via SPI), not the SDMMC driver in this module. The pin definitions in this file (`GPIO 39, 40, 41`) overlap with the SPI SD card pins but use the SDMMC protocol, suggesting this was an early experiment that was abandoned in favor of SPI mode.

- **Chinese comments still present:** Lines 26-28, 36-37 contain Chinese comments from the original Waveshare demo code.

- **Dangerous function:** `s_mfs_write_file()` uses `fprintf(f, data)` where `data` is user-provided. If `data` contains `%` characters, this is a format string vulnerability. Should be `fputs(data, f)` or `fprintf(f, "%s", data)`.

- **Recommendation:** Delete this module entirely. It is dead code that will confuse contributors.

### 8.6 lcd_bl_pwm_bsp — Backlight PWM Driver (Legacy/Partially Used)

**Files:** `lcd_bl_pwm_bsp.c` (47 lines), `lcd_bl_pwm_bsp.h` (30 lines)

**Findings:**

- **Partially superseded:** The main sketch defines `init_backlight_pwm()` and `set_backlight_pwm()` using the Arduino `ledcAttach()`/`ledcWrite()` API. This BSP module uses the ESP-IDF LEDC API directly. Both target the same GPIO (pin 42). The Arduino version is what actually runs at boot. This module's `lcd_bl_pwm_bsp_init()` does not appear to be called.

- **Naming inconsistency:** The function `setUpduty()` uses camelCase while the rest of the file uses snake_case.

- **Unused PWM mode defines:** The 11 `LCD_PWM_MODE_*` defines in the header are never referenced anywhere in the codebase.

- **Recommendation:** Either adopt this module as the canonical backlight driver (and remove the Arduino `ledcAttach` code from the .ino) or delete it entirely.

### 8.7 Driver Layer Summary

| Module | Status | Action Recommended |
|:---|:---|:---|
| `i2c_bsp` | **Active, has issues** | Replace `malloc` with stack buffer; extract IMU code |
| `PCF85063/rtc_bsp` | **Active, clean** | Fix redundant extern; remove debug task |
| `QMI8658/qmi_bsp` | **Dead code** | Delete or adopt as canonical IMU module |
| `sdcard/sdcard_bsp` | **Dead code** | Delete entirely |
| `lcd_bl_bsp` | **Dead code** | Delete or adopt as canonical backlight driver |
| `tca9554/` | Vendor | No action needed |
| `touch/` | Vendor | No action needed |
| `codec_board/` | Vendor | No action needed |
| `esp_codec_dev/` | Vendor | No action needed |

The most impactful cleanup for open-source presentation would be **deleting the three dead-code modules** (`qmi_bsp`, `sdcard_bsp`, `lcd_bl_bsp`). They add confusion without contributing functionality, and their presence suggests to reviewers that the codebase has unmaintained corners. The `i2c_bsp` malloc-in-hot-path issue should also be fixed as it affects long-term runtime stability.

---

*This review was conducted by analyzing the complete source tree of the Magnetic Field Scanner project at firmware version v3.0.40.*
