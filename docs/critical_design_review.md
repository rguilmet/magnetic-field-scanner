# Critical Design Review (Current State)

**Document Version:** `v1.0.0`
**Date:** August 27, 2026 @ 07:34 EST
**Firmware Target:** `v3.0.32`
**Python Ecosystem Target:** `v1.0.0`
**Reviewer:** Architect / Lead Firmware Engineer

## 1. Executive Summary
The Magnetic Field Scanner is currently in a highly functional "Late-Stage Prototype" phase. The core physics, math, and primary UI are successfully demonstrating the intended product value (real-time magnetic gradiometry for utility locating). However, pushing this device to a "Professional Grade" commercial standard requires addressing specific technical debt in the firmware architecture and physical layer. 

Below is an objective architectural breakdown of the Good, the Bad, the Ugly, and the roadmap for professional refinement.

---

## 2. The Good (Strengths & Successes)

* **On-Device DSP & Matrix Math:** The custom `matrix_math.h` library is a massive engineering win. Executing a 9x9 Jacobi eigenvalue solver, 3D least-squares ellipsoid fitting, and Kabsch rotational alignment natively on an ESP32—using only safe stack memory and floating-point math—is a major achievement. It removes the need for PC-tethered calibration.
* **Firmware Mitigation of Hardware Physics:** The implementation of the **Slew-Rate Filter** (rejecting >800 count jumps per 2.5ms) and the **Low-Pass Auto-Tare** (slowly eating thermal/geological drift) are incredibly elegant firmware solutions. They perfectly shield the user experience from the harsh physical realities of EMI and temperature drift.
* **Dual-Core Task Isolation:** Pinning the LVGL UI to Core 1 while keeping the highly time-sensitive I2C sensor bit-banging on Core 0 ensures that UI screen-draws never cause missed sensor frames.
* **Audio UX:** Mapping the raw gradient magnitude dynamically to the PWM audio frequency provides zero-latency, eyes-free feedback, which is exactly what field operators need.

---

## 3. The Bad (Technical Debt & Limitations)

* **Monolithic Sensor Task:** The `task_sensor_read` function violates the single-responsibility principle. It is currently responsible for reading hardware I2C, running slew-rate filters, applying 3D calibration math, calculating total magnitude, *and* pushing data to UI queues. This makes the loop execution time variable.
* **Synchronous String Parsing:** The calibration file parser (`wifi_logger.cpp`) relies heavily on Arduino `String` objects and synchronous line-by-line SD card reading. This causes massive heap fragmentation and takes >5 seconds, requiring us to manually "pet" the FreeRTOS watchdog (`vTaskDelay`) to prevent crash reboots.
* **Inefficient Data Logging:** Writing raw ASCII CSV text to the SD card at high frequencies is extremely CPU and I/O intensive. It consumes unnecessary SD card write cycles and blocks the SPI bus.

---

## 4. The Ugly (Risks & Hacks)

* **Mechanical Reliance on Software (The 9.4° Bend):** The current PLA sensor carrier inside the wand has a physical bend/flex of roughly 9.4 to 9.8 degrees. We are relying *entirely* on the Kabsch software matrix to mathematically "un-bend" the wand. While mathematically sound, a professional device should never rely on software to fix gross mechanical tolerances.
* **I2C Over Long Distances:** I2C (Inter-Integrated Circuit) was designed for chips sitting inches apart on the same PCB. Running an unshielded I2C bus over 3 feet of wire next to a high-speed SPI display is a fundamental hardware risk. We have successfully stabilized it using stiff 4.7K pull-ups, 100kHz clock limits, and Slew-Rate filtering, but it remains electrically vulnerable to crosstalk and ambient RF noise.

---

## 5. Path to Professional Grade (Next Steps)

To elevate this device from a functional prototype to a commercial, professional-grade tool, the following architectural shifts are recommended:

### A. Firmware Architecture Refinements
1. **Decouple the Data Pipeline:** Split `task_sensor_read` into two tasks connected by a RingBuffer:
   * `task_i2c_driver`: Pure hardware interaction. Reads the registers at exactly 400Hz and pushes raw structs to a buffer.
   * `task_dsp`: Reads the buffer, applies calibration math, Auto-Tare, and Slew-Rate filtering.
2. **Binary Logging:** Replace CSV SD logging with a packed binary format (or Protobuf). A raw `struct` dump saves massive CPU overhead, prevents string allocation, and drastically increases SD write speeds.
3. **Eliminate `String`:** Purge all use of Arduino `String` objects across the codebase in favor of static `char` buffers and `snprintf` to prevent heap fragmentation during long field sessions.

### B. Hardware & Mechanical Refinements
1. **Mechanical Carrier Redesign:** Replace the 1/4" PLA/fiberglass internal carrier with an extruded aluminum channel or a custom rigid PCB backplane to ensure the Tip and Ref sensors are mechanically co-linear within <1.0 degree. 
2. **Differential Signaling (Long Term):** If the 100kHz I2C bus continues to show any dropped frames in noisy industrial environments (e.g., near power substations), the sensor bus should be upgraded to use I2C-to-Differential transceivers (like the PCA9615) to run over twisted-pair CAT5 cable down the wand.
3. **GPS Integration:** Proceed with the planned GPS integration on `IO43/IO44`. Mapping underground utility hits directly to geospatial coordinates is the primary differentiator for a high-end commercial device.

### C. Companion Software (Python Ecosystem)
1. **CLI Standardization:** All offline processing scripts (`calibrate_wand.py`, `generate_plots.py`, and `analyze_log.py`) must be upgraded to commercial CLI standards using `argparse` to provide robust `--help` documentation and standardized flag handling.
2. **Metadata Headers & SemVer:** Inject strict versioning (e.g., `v1.0.0`) and detailed docstring headers at the top of every script to match the rigor of the C++ codebase, ensuring traceability between field logs and the exact algorithm versions used to process them.
3. **Cross-Platform Path Sanitization:** Ensure all CLI file inputs (`argparse`) explicitly strip literal terminal quotes and normalize slashes (e.g., `os.path.normpath(args.input.strip('\'"'))`). This mitigates a known Windows PowerShell quirk where drag-and-dropped files inject literal quotes into the script, causing fatal `FileNotFound` errors.
