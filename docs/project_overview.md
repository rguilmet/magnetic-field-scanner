# Magnetic Field Scanner - Project Overview

**Document Version:** `v1.1.0`
**Last Updated:** August 28, 2026
**Firmware Target:** `v3.4.0`
**Python Ecosystem Target:** `v1.1.0`

This document serves as the master source of truth for the Magnetic Field Scanner project. It details the mechanical assembly, electrical wiring, pinouts, firmware architecture, critical pitfalls to avoid in future development, and the roadmap for upcoming features.

---

## 1. Project Identity, Versioning & Documentation Rules
* **CRITICAL RULE:** All projects shall have a main Software version identifier as `v[Major].[Minor].[Patch]` and follow industry standard semver rules and practices for bumping the version at EVERY file iteration.
* **Why:** We do not reuse version numbers for each iteration. This allows us to distinctly identify in the AI context the files we are working on. The version string is defined as `FIRMWARE_VERSION` in `user_config.h`.

**Documentation Strategy (The "Definition of Done"):**
To prevent documentation rot and excessive maintenance overhead, documentation updates are strictly tethered to the SemVer lifecycle:
* **Patch Releases (e.g., `v3.1.1` -> `v3.0.33`):** Bug fixes and math tweaks. Do *not* update the architectural docs (`project_overview.md`, `user_manual.md`). Log these changes exclusively in the root `CHANGELOG.md`.
* **Minor Releases (e.g., `v3.0.x` -> `v3.1.0`):** Adding new functional features (e.g., GPS integration). The feature is not considered complete until both the `CHANGELOG.md` and the relevant `docs/` Markdown files are updated to reflect the new capabilities.
* **Major Releases (e.g., `v3.x` -> `v4.0.0`):** Massive architectural overhauls (e.g., migrating from I2C to CAN bus). Triggers a comprehensive review and rewrite of all files in the `docs/` directory.

---

## 2. Hardware Architecture & Mechanical Setup
* **The Wand:** Constructed from a rigid 1" fiberglass outer rod with a 3/4" ID.
* **Main Controller (The Handle):** Waveshare ESP32-S3-Touch-LCD (4.3 / 3.49 v2) with an integrated screen, battery ADC, IMU (QMI8658, addr `0x6B`), and RTC (PCF85063, addr `0x51`). This unit sits at the top of the wand acting as the primary handle and UI display. A custom wiring harness runs down from this unit into the main fiberglass tube.
* **The Sensor Array:** The wand acts as a **Magnetic Gradiometer**. It currently utilizes two PNI RM3100 magnetometer breakout boards ("Tip" and "Reference").
  * The boards are mounted to a PLA mechanical carrier that slides directly into the 3/4" ID of the fiberglass tube.
  * **Carrier Rigidity:** To ensure absolute rigidity between the Tip and Reference sensors, the PLA board carriers are physically joined to each other by a solid 1/4" internal fiberglass rod.
  * **Sensor Orientation:** The boards were recently flipped 180 degrees (upside down) in the assembly. 
  * **Physical Misalignment:** There is a permanent, measured physical flex/bend in the carrier assembly of roughly `~9.4` to `~9.8` degrees. The firmware mathematically eliminates this using the Kabsch alignment algorithm.

### Mechanical Diagram
(Assuming visual diagram exists elsewhere)

### Sensor Coordinate Systems & Axis Alignment (Phase 7 Discovery)
Because the BMI270 IMU and the dual RM3100 Magnetometers are mounted to the same rigid Wand structure, they must speak the exact same 3D spatial language for sensor fusion (Madgwick 9-DOF) to work. A live logging "Alignment Dance" revealed the exact physical orientation of the chips on the Veroboard. 

**The IMU (BMI270) Coordinate System (Right-Handed):**
* +X Axis: Points **Forward** (Towards the sensor tip)
* +Y Axis: Points **Left**
* +Z Axis: Points **Up** (Out of the LCD Screen)

**The Magnetometer (RM3100) Coordinate System (Left-Handed):**
Due to the physical mounting of the breakout boards inside the wand, the RM3100 acts as a Left-Handed coordinate system relative to the IMU.
* +X Axis: Points **Left**
* +Y Axis: Points **Forward**
* +Z Axis: Points **Down** (Into the ground)

**The Translation Key:**
To safely feed the Magnetometer data into the Right-Handed 9-DOF Madgwick algorithm, the software applies the following mathematical mapping to the RM3100 (
efX_cal, refY_cal, refZ_cal) values:
`c
float mag_x =  (float)ref.y;  // Maps RM3100 Forward to IMU Forward
float mag_y = -(float)ref.x;  // Maps RM3100 Right to IMU Left
float mag_z = -(float)ref.z;  // Maps RM3100 Down to IMU Up
`
This permanently locks the Quaternions to True Magnetic North for accurate post-analysis mapping.
*(Please place your mechanical drawing in a folder named `docs` in the project root and name it `mechanical_drawing.png` or update this link to point to it).*
![Mechanical Drawing of Wand and Waveshare](docs/mechanical_drawing.png)

---

## 3. Wiring & Pinout Assignments

### CRITICAL HARDWARE NOTE: I2C Pull-Up Resistors
* **The RM3100 breakout boards DO NOT have built-in I2C pull-up resistors.**
* **The Fix:** We permanently installed **4.7K pull-up resistors** in the cable harness directly at the Waveshare connection end (SDA/SCL to 3.3V).
* **The Result:** Combined with the Waveshare's internal pull-ups, this provides a "stiff" ~2K to ~3K ohm equivalent resistance, which is perfect for fighting the heavy capacitance of the 3-foot cable run down the wand.

### CAT6 Wire Harness & Color Coding
The internal harness from the Waveshare board to the sensor array utilizes a standard CAT6 twisted-pair cable. It perfectly allocates all 8 wires to support three RM3100 sensors while maximizing electrical integrity over the 48" run.

For the full visual diagram, reference the spreadsheet: docs/electrical/Cable_Color_Code.xlsx.

| Component / Function | ESP32 Pin | CAT6 Color | Twisted Pair Strategy (Signal Integrity) |
| :--- | :--- | :--- | :--- |
| **3.3V Power** | 3V3 | Orange | **Power & Sync:** Twisted with DRDY_MID. DC power acts as an AC ground, shielding the digital pulse. |
| **DRDY_MID** | IO05 | Orange/White | *(Future 3rd Sensor)* |
| **SDA (Data)** | IO47 | Green | **I2C Data Shield:** Twisted directly with GND to prevent capacitive crosstalk against SCL. |
| **GND** | GND | Green/White | |
| **SCL (Clock)** | IO48 | Blue | **I2C Clock Shield:** Twisted directly with GND to prevent signal degradation. |
| **GND** | GND | Blue/White | |
| **DRDY_TIP** | IO01 | Brown | **Digital Sync:** Low-frequency (400Hz) digital interrupts paired together. |
| **DRDY_REF** | IO02 | Brown/White | |

### I2C Architecture & Device Addresses
The wand utilizes two completely separate hardware I2C buses to isolate the sensitive long-wire sensors from the noisy high-speed screen components.

#### 1. I2C0 (External Sensor Bus)
* **Purpose:** Drives the long wire harness running down the 3-foot fiberglass tube to the RM3100 sensors.
* **Speed:** Hardcoded to **100kHz** to fight cable capacitance.
* **Pins:** SCL: `48`, SDA: `47`
* **Devices on Bus:**
  * **RM3100 Tip:** `0x23` (DRDY: `GPIO 1`)
  * **RM3100 Ref:** `0x21` (DRDY: `GPIO 2`)
  * **RM3100 Mid (Future):** `0x22` (DRDY: `GPIO 5`) *(Reserved for planned 3rd sensor)*
* **CRITICAL CONFLICT HISTORY (Address `0x20`):** Initially, an RM3100 was set to address `0x20`. This caused a total system failure because the Waveshare board has an undocumented internal I/O Expander hardcoded to `0x20`. **Never use `0x20` on the sensor bus**, even though the buses are logically separated; we mapped the RM3100s to `0x21`, `0x22`, and `0x23` to safely avoid any internal hardware routing conflicts.

#### 2. I2C1 (Internal Waveshare Bus)
* **Purpose:** Local communication for the Waveshare board's integrated SMD components.
* **Pins:** SCL: `18`, SDA: `17`
* **Devices on Bus:**
  * **Capacitive Touch Controller:** `0x3B`
  * **RTC (PCF85063):** `0x51`
  * **IMU (QMI8658):** `0x6B`
  * **I/O Expander:** `0x20` *(The source of the conflict mentioned above)*

### Other Master Pinouts (Defined in `user_config.h`)

| Component | Pin (GPIO) | Notes / Details |
| :--- | :--- | :--- |
| **SPI (SD Card)** | CS: `38`, MOSI: `39`, MISO: `40`, CLK: `41` | Used by `wifi_logger.cpp` to save `calibration.csv` and logs. |
| **GPS Module** | TX: `43`, RX: `44` | *Planned Future State.* |

---

## 4. Critical Engineering Decisions & Pitfalls to Avoid

### A. I2C Bus Speed (DO NOT EXCEED 100kHz)
* **The Mistake:** Attempting to run the hardware I2C bus at 400kHz ("Fast Mode").
* **The Consequence:** The physical capacitance of 3 feet of unshielded wire inside the wand caused mushy square waves. The ESP32 missed ACK bits, resulting in `0,0,0` readings, massive 140,000-count garbage spikes, and frequent "Sensor Lockup" reboots.
* **The Fix:** The I2C speed (`scl_speed_hz`) in `Magnetic_Field_Scanner.ino` is hardcoded to `100,000` (Standard Mode). This is electrically bulletproof for this cable length. 

### B. EMI "Meteors" & Slew Rate Filtering
* **The Problem:** Even at 100kHz, the long I2C wires act as antennas. Microscopic bit-flips (EMI glitches) occasionally jump the raw reading by 2,000 to 5,000 counts in a single frame. Because the gradiometer math expects near-zero differences, this causes the audio UI to emit a massive, terrifying shriek ("cosmic meteors").
* **The Fix:** We implemented a **Slew Rate Filter (Derivative Filter)** in `task_sensor_read`. If any single axis on either sensor jumps by more than **800 counts** in a single 2.5-millisecond frame, it is mathematically deemed physical EMI. The frame is silently dropped, averting the audio shriek. Do not use an absolute threshold (like `>50,000`) because total magnitude scales with the Cycle Count!

### C. Watchdog Timer (TWDT) Panics
* **The Problem:** Processing a 3,000-line `calibration.csv` file from the SD card using `String` operations in `wifi_logger.cpp` takes more than 5 seconds. The ESP32 FreeRTOS Task Watchdog Timer assumed the system was frozen and triggered a hard panic reboot right at 99% completion.
* **The Fix:** Ensure heavy processing loops (like file parsing) contain a `vTaskDelay(pdMS_TO_TICKS(10));` every 100 iterations to "pet the watchdog" and yield to the OS.

### D. Sensor Lockup Logic
* **The Problem:** Aggressively hard-resetting the RM3100 sensors `initRM3100()` on a single `0,0,0` read failure.
* **The Fix:** Occasional missed frames are normal. The watchdog now only triggers a hard sensor reset after **10 consecutive identical or failed frames**. Single glitch frames are silently ignored via `continue`.

### E. Cycle Count Scaling
* **The Physics:** When the user changes the Cycle Count (CC) on the fly, the RM3100's raw readings scale linearly. (e.g., CC=200 gives a radius of ~3,800, CC=400 gives ~7,600, CC=800 gives ~15,400).
* **The Code:** The `cal_config.tip_hard` and `ref_hard` matrices (Center Offsets) MUST be scaled linearly in code when CC changes. The Soft-Iron matrices (W) DO NOT scale, as they represent dimensional shape ratios.

### F. Auto-Tare vs Manual Tare
* **Manual Tare:** Memorizes the current environmental gradient shadow (e.g., standing near a car) and subtracts it from all future readings. Should only be performed in a magnetically "clean" area.
* **Auto-Tare:** Implements a low-pass filter (multiplier `0.005`) that slowly and invisibly pulls the baseline back to zero over time. Crucially, it **only engages if the gradient jumps by less than 50 counts**. This eats away slow temperature/geology drift while completely ignoring the sharp spikes of a buried utility pipe!

### G. Windows CLI Path Handling (Python)
* **The Problem:** When dragging and dropping files into Windows PowerShell or Command Prompt to pass arguments to Python scripts, Windows automatically wraps the filepath in literal double-quotes (e.g., `"C:\path\to\file.csv"`). Python's `argparse` absorbs these literal quotes as part of the string, causing `os.path.exists()` to fail and crash the script. 
* **The Fix:** All Python companion scripts in this project universally sanitize `argparse` inputs using `os.path.normpath(args.input.strip('\'"'))` before interacting with the file system. This strips accidental terminal quotes and normalizes slashes, bulletproofing the CLI against Windows pathing quirks.

---

## 5. Current State of the Project
* **Core Functionality:** The Gradiometer math (`Tip - Ref`) is fully operational and extremely sensitive.
* **On-Device Calibration:** Functional. `matrix_math.h` performs a 9x9 Jacobi eigenvalue solver and 3D ellipsoid fit using only local stack memory. It applies Kabsch alignment to precisely orient the Tip sensor onto the Reference sensor's frame, nullifying the 9-degree mechanical flex.
* **Data Logging:** SD card saving is stable. Python companion scripts (`calibrate_wand.py` and `plot_pins.py`) can ingest `_log.csv` and `_calibration.csv` files to visualize the exact sweeping motions using IMU Gyroscope Z-axis tracking.
* **Audio & UI:** LVGL runs successfully on Core 1 while the bitbanging/I2C sensor tasks run on Core 0. (Note: `lv_conf.h` must remain in the root directory due to library constraints).

---

## 6. Companion Software Ecosystem (Python)
The repository includes a suite of offline Python processing scripts located in the `scripts/` directory. All official scripts adhere to SemVer versioning, contain industry-standard header documentation, and utilize `argparse` for robust command-line `--help` functionality.

* **`calibrate_wand.py`:** Ingests raw `calibration.csv` datasets, computes the 3x3 Soft-Iron matrices (W) and Hard-Iron center offsets (V), applies Kabsch rotational alignment, and outputs a drop-in `calibration.json` file.
* **`generate_plots.py`:** Specialized utility for visualizing 3D ellipsoid point clouds to verify the mathematical perfection of the sphere-fitting algorithms.
* **`analyze_log.py` (Formerly `plot_pins.py`):** Ingests live field operation logs (`_log.csv`) and generates a synchronized 3-pane matplotlib dashboard showing Gradient Magnitude, Audio Frequency response, and IMU Gyroscope Z-axis (Yaw) to visually correlate physical sweeping motions with underground utility strikes.

---

## 7. Planned Future State (Roadmap)
1. **Third Sensor Integration (Mid):** 
   * A third RM3100 sensor will be mounted in the middle of the wand.
   * I2C Address: `0x22`
   * DRDY Pin: `GPIO 5`
   * Purpose: To allow quadratic gradient tracking and depth-estimation of buried targets.
2. **GPS Module Integration:**
   * Serial unit to be added to pins `IO43` (TX) and `IO44` (RX).
   * Purpose: To embed geospatial coordinates into the SD log files, allowing post-processing software to map buried utilities directly onto a satellite view.


## 8. Firmware Architecture & Single Responsibility Principle
Following the Phase 3 structural refactor, the firmware heavily adheres to the **Single Responsibility Principle (SRP)**. Monolithic files have been rigorously decomposed to prevent cross-domain contamination, particularly between Core 0 (Sensor/SD) and Core 1 (UI/Web).

### src/ Directory Breakdown:
* **data_logger/**: Solely responsible for writing 400Hz CSV data to the SD card/FFat and handling file flushes.
* **settings_manager/**: Exclusively manages the saving, loading, and JSON serialization of SystemSettings and CalibrationConfig.
* **web_server/**: A pure UI/networking layer running on Core 1. It handles HTTP endpoints, file downloads, uploads, and OTA operations without directly touching hardware logic.
* **screenshot/**: Isolated LVGL screen capture logic (RGB565 to BMP conversion).
* **lvgl_port/**: Handles the physical drawing of the screen and UI layout.
* **matrix_math/**: A pure, header-only implementation of the 9x9 Gaussian elimination and 3D Kabsch alignment algorithms.
* **Hardware BSPs (i2c_bsp, imu_bsp, 
tc_bsp)**: Board Support Packages that isolate the direct register-level I2C commands from the main business logic.

**Core Separation & Thread Safety:**
* **Core 0 (Data/Hardware):** 	ask_sensor_read, 	ask_battery_monitor, 	ask_audio_alert. I2C reads and fast math.
* **Core 1 (UI/Network):** 	ask_display_update (LVGL), mfs_button_pwr_task, WiFi server.
* **Mutexes:** mfs_lvgl_lock() must be acquired before touching UI elements from Core 0. log_mux must be acquired before writing to logFile.


