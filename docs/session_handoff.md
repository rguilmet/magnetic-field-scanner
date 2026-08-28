# Magnetic Field Scanner — Session Handoff Document

**Date:** August 28, 2026  
**Current Firmware Version:** `v3.0.40`  
**Project Root:** `C:\Users\rguilmet\Documents\Arduino\Magnetic_Field_Scanner`  

---

## 1. Project Summary

The Magnetic Field Scanner (MFS) is an ESP32-S3-based handheld magnetic gradiometer for underground utility locating. It uses dual RM3100 geomagnetic sensors in a differential (spatial gradient) configuration to detect ferrous objects buried underground. The device features a full LVGL touchscreen UI, real-time audio feedback, on-device calibration with Kabsch rotational alignment, Wi-Fi web portal, and dual-filesystem (SD + FFat) data logging.

**Hardware:** Waveshare ESP32-S3-Touch-LCD-3.49 v3, 2x RM3100 (TIP @ 0x23, REF @ 0x21), QMI8658 IMU, PCF85063 RTC, ES8311 audio codec, TCA9554 I/O expander, SD card via SPI.

**Target Audience:** Hackaday and GitHub open-source publication.

---

## 2. Key Files and Architecture

| File | Lines | Role |
|:---|:---|:---|
| `Magnetic_Field_Scanner.ino` | 864 | Main sketch: FreeRTOS tasks, sensor I2C, audio, calibration orchestration |
| `user_config.h` | 88 | All pin definitions, constants, firmware version |
| `src/wifi_logger/wifi_logger.h` | 64 | Public API: structs (`CalibrationConfig`, `SystemSettings`), function declarations |
| `src/wifi_logger/wifi_logger.cpp` | 855 | Settings/calibration persistence, HTTP web server, data logging, timestamps, screenshots |
| `src/lvgl_port/lvgl_port.h` | 52 | UI API: `UIData` struct, UI function declarations |
| `src/lvgl_port/lvgl_port.c` | 1034 | LCD/touch driver init, LVGL UI creation, all widget callbacks, UI update logic |
| `src/matrix_math/matrix_math.h` | 323 | Header-only: 9x9 solver, Jacobi eigen, ellipsoid fitting, Kabsch alignment |
| `scripts/analyze_log.py` | 72 | Python: parses field logs, generates matplotlib dashboards |
| `scripts/calibrate_wand.py` | ~120 | Python: offline calibration processing |
| `scripts/generate_plots.py` | ~90 | Python: batch plot generation |
| `CHANGELOG.md` | ~100 | Granular per-version changelog (v3.0.0 through v3.0.40) |
| `docs/opus_critical_design_review.md` | ~230 | **THE PRIMARY INPUT FOR NEXT STEPS** |

### FreeRTOS Tasks
- **SensorTask** (Core 0, Priority 2): 400Hz I2C polling, DSP pipeline, calibration math, logging
- **DisplayTask** (Core 1, Priority 1): LVGL UI updates from `xQueueOverwrite`
- **AudioTask** (Core 1, Priority 1): Continuous I2S audio synthesis with EMA smoothing

---

## 3. Critical Rules (MUST Follow)

1. **Semver Versioning:** Every file change MUST bump `FIRMWARE_VERSION` in `user_config.h` and add a `CHANGELOG.md` entry. Never reuse a version number.
2. **File Naming Convention:** Dynamic files use: `[log|calibration|screenshot]_YYYY-MM-DD_HH-MM-SS[+/-]<offset>.[csv|bmp|json]`. Internal CSV rows retain millisecond precision.
3. **Arduino IDE Board Settings:** ESP32-S3 Dev Module, OPI PSRAM, 16MB Flash, `app3M_fat9M_16MB` partition. Changing the board selection wipes these critical settings.
4. **Linker Cache Bug:** Deep changes to `lv_conf.h` or macro-level configs require manual deletion of `%LocalAppData%\arduino\sketches\<hash>` to force clean rebuild.

---

## 4. Opus Critical Design Review — Prioritized Findings

The full review is at `docs/opus_critical_design_review.md`. Below are the actionable findings, prioritized for an implementation plan:

### Priority 1 — Bugs (Fix Immediately)
- [ ] **Duplicate `save_calibration()` call** in `wifi_logger.cpp` lines 715-716. The function is called twice in succession. Remove the duplicate.
- [ ] **Header guard collision** in `qmi_bsp.h` — uses `#ifndef RTC_BSP_H` instead of `#ifndef QMI_BSP_H` (copy-paste from rtc_bsp.h).
- [ ] **Heap allocation in I2C hot path** — `i2c_write_buff()` in `i2c_bsp.c` calls `malloc()`/`free()` at 400Hz. Replace with stack-allocated buffer.

### Priority 2 — Code Hygiene (Pre-Publication Polish)
- [ ] **Duplicate includes** in `wifi_logger.cpp` — `FFat.h`, `WiFi.h`, `WebServer.h`, `esp_heap_caps.h`, `dirent.h`, `sys/stat.h` each included 2-3x.
- [ ] **Duplicate `extern` declaration** — `toggle_mute` declared twice in `lvgl_port.c` (lines 362 and 395).
- [ ] **Extract magic numbers** to named `#define` constants in `user_config.h` (slew threshold 800, auto-tare threshold 50.0f, nT conversion factor 0.38f, audio squelch 20.0f, EMA coefficients, etc.).
- [ ] **Add `volatile`** to cross-task shared variables: `is_scanning`, `is_muted`, `current_audio_gain`, `auto_tare_enabled`.
- [ ] **Delete dead-code driver modules:** `QMI8658/qmi_bsp` (superseded by `i2c_bsp.c`), `sdcard/sdcard_bsp` (superseded by Arduino `SD.begin()`), `lcd_bl_bsp` (superseded by Arduino `ledcAttach()`).
- [ ] **Remove stale debug code:** `i2c_rtc_loop_task()` in `rtc_bsp.cpp`, redundant `extern` in `rtc_bsp.cpp` line 13.
- [ ] **Fix format string vulnerability** in `sdcard_bsp.cpp` `s_mfs_write_file()` (if not deleting the module).

### Priority 3 — Architectural Refactoring
- [ ] **Decompose `wifi_logger.cpp`** into purpose-named modules: `settings_manager`, `web_server`, `data_logger`, `screenshot`.
- [ ] **Replace `0x7FFFFFFF` sentinel** for tare triggering with a dedicated `volatile bool tare_requested` flag.
- [ ] **Move battery ADC reading** out of `update_detector_ui()` in `lvgl_port.c` into a timer or periodic task.
- [ ] **Consolidate scattered `extern` declarations** into a single `mfs_api.h` header.
- [ ] **Extract IMU code** from `i2c_bsp.c` into its own module (or consolidate with `qmi_bsp`).

### Priority 4 — Open Source Readiness
- [ ] **Add `CONTRIBUTING.md`** with naming conventions, version bumping rules, and code review expectations.
- [ ] **Add function-level docstrings** to public API functions in headers.
- [ ] **Add license badge** to README.md.
- [ ] **Consider `platformio.ini`** for single-command builds.

### Priority 5 — Feature Improvements (Post-Publication)
- [ ] 3rd RM3100 sensor (MID) integration for second-order gradiometry
- [ ] Temperature compensation for RM3100 drift
- [ ] Kalman filter to replace EMA auto-tare
- [ ] Scrolling time-series strip chart (4th tile)
- [ ] OTA firmware updates via Wi-Fi
- [ ] QR code of IP address on System & Hardware page
- [ ] GPS integration on IO43/IO44

---

## 5. What Was Accomplished in This Session

- v3.0.33 → v3.0.40 firmware iterations
- Fixed `force_audio_tone` pipeline (pitch-bend bug, microsecond timing bug)
- Implemented on-device LVGL screenshots (BMP via BOOT button long-press)
- Fixed LVGL snapshot bug (was only capturing Tile 1 regardless of active page)
- Standardized file naming conventions across all generated files
- Re-architected BMP web delivery to bypass Chrome HTTP download warnings (Canvas-based PNG conversion)
- Refined Web UI screenshot viewer (button placement, title removal)
- Updated `user_manual.md` with real device screenshots and section name alignment
- Renamed entire project from `Magnetic_Field_Detector_Wand` to `Magnetic_Field_Scanner`
- Web portal title updated to "Magnetic Field Scanner Portal"
- Created comprehensive Opus Critical Design Review (`docs/opus_critical_design_review.md`)

---

## 6. Instructions for Next Session

1. Open the project from `C:\Users\rguilmet\Documents\Arduino\Magnetic_Field_Scanner` to ensure the workspace path is correct.
2. Read `docs/opus_critical_design_review.md` for full context.
3. Build an implementation plan against the prioritized findings in Section 4 above.
4. Start with Priority 1 (the duplicate `save_calibration` bug) and Priority 2 (code hygiene), as these are the highest-impact, lowest-risk changes before Hackaday publication.
