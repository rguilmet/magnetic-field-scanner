# Magnetic Field Scanner (MFS) - User Manual

**Document Version:** `v1.0.2`
**Last Updated:** August 27, 2026 @ 12:46 EST
**Firmware Target:** `v3.0.36`
**Python Ecosystem Target:** `v1.0.0`

Welcome to the User Manual for the Magnetic Field Scanner (MFS). This guide will walk you through the user interface, calibration process, data logging capabilities, and web server connectivity.

---

## 1. User Interface Overview

The MFS features a touchscreen interface driven by LVGL. The interface is designed for rapid field use and zero-latency feedback.

### Main Dashboard
![Main Dashboard](images/Magnetic%20Field%20Scanner%20-%20Main%20Screen.png)

* **Live Telemetry:** Displays real-time magnetic field strength in NanoTeslas (nT) for both the TIP and REF sensors.
* **Spatial Gradient:** Shows the delta (difference) between the TIP and REF sensors, isolating localized magnetic anomalies from Earth's background field.
* **Battery & Storage:** Status icons in the header show battery voltage and whether the SD Card / FFat is actively mounted.

### Calibration & Tracking (Tare Operations)
![Calibration & Tracking](images/Magnetic%20Field%20Scanner%20-%20Calibration%26Tracking.png)

The Wand employs a dual-strategy for zeroing out environmental magnetic interference:
* **Manual Tare:** Memorizes the current environmental gradient shadow (e.g., standing near a car) and subtracts it from all future readings. Tap the Tare button on the screen when standing in a magnetically "clean" area before beginning your sweep.
* **Auto-Tare:** Implements an invisible low-pass filter (multiplier `0.005`) that slowly pulls the baseline back to zero over time to combat temperature drift. Crucially, it **only engages if the gradient jumps by less than 50 counts**. This eats away slow drift while completely ignoring the sharp spikes of a buried utility pipe!

### System & Hardware (Settings Menu)
![System & Hardware](images/Magnetic%20Field%20Scanner%20-%20System%26Hardware.png)

* **Audio Toggle:** Mutes or enables the variable-pitch audio feedback. The PWM audio frequency dynamically scales with the spatial gradient magnitude, allowing eyes-free locating.
* **Cycle Count (CC):** Allows you to adjust the internal RM3100 Cycle Count (default `400`). Note: The firmware will automatically scale your calibration offsets if you change this in the field.
* **Logging Toggle:** Starts or stops CSV data logging to the active filesystem.
* **Wi-Fi Settings:** Displays the current IP address for Web Server access.

---

## 2. Web Server & Log Management

The MFS hosts a local web server allowing you to wirelessly manage logs and configuration files without removing the SD card.

1. Ensure the MFS is connected to your local Wi-Fi network.
2. Note the IP address displayed on the MFS screen (e.g., `http://192.168.1.xxx`).
3. Open a web browser on your PC or smartphone and navigate to that IP.

### Web Interface Features
*(Please place a screenshot of the web UI here)*
![Web Interface](docs/images/Magnetic%20Field%20Scanner%20-%20web_interface.png)

* **Dual-Drive Visibility:** The page distinctly lists files stored on the removable **SD Card** versus the internal **FFat** flash memory.
* **Download Logs:** Click any `.csv` log file to download it directly.
* **Configuration Management:** You can download `settings.json` and `calibration.json`, modify them on your PC, and use the Upload button to send them back to the device.

---

## 3. Data Output Format

When logging is active (polling at 400Hz), the MFS generates a `.csv` file (`_log.csv`). To visualize this file, use the companion Python script `analyze_log.py`. The columns are formatted exactly as follows:

| Column(s) | Description |
| :--- | :--- |
| `time_ms` | CPU uptime in milliseconds |
| `timestamp` | Human-readable date and time |
| `version` | Firmware version (e.g., v3.0.32) |
| `voltage` | Battery voltage level |
| `cc` | RM3100 Cycle Count (e.g., 400) |
| `refX_raw` -> `tipZ_raw` | Uncalibrated raw axes for Reference and Tip sensors |
| `refX_cal` -> `tipZ_cal` | Hard/Soft iron corrected and Kabsch-aligned axes |
| `calOffsetX` -> `calOffsetZ` | The active Manual Tare offsets currently being applied |
| `gradX, gradY, gradZ` | Spatial gradient vectors (Tip - Ref) |
| `mag` | Total spatial gradient magnitude (The primary detection metric) |
| `nT` | NanoTesla conversion of magnitude |
| `accX, accY, accZ` | IMU Accelerometer data (Gs) |
| `gyrX, gyrY, gyrZ` | IMU Gyroscope data (Degrees per Second) - Maps your physical sweep |
| `freq` | The PWM Audio frequency being output to the speaker (Hz) |
| `is_muted` | Boolean flag indicating if the UI speaker is muted |

---

## 4. Hardware Calibration

Magnetometers are highly sensitive to local ferromagnetic interference (such as the LCD shield, battery, and screws). To achieve precision gradiometry, the device must perfectly align the magnetic spheres of the Tip and Reference sensors.

**On-Device DSP Calibration:**
Unlike earlier prototypes, **v3.0.32 does not require a PC for calibration.** The ESP32-S3 performs all mathematics locally. With calibrate_wand.py as a backup to create calibration.json file.
1. Tap the "Calibrate" button in the UI.
2. Tumbel the wand smoothly in a "Figure-8" pattern through all 3D axes (Pitch, Roll, Yaw) for approximately 30-45 seconds.
3. The device will run a native 9x9 Jacobi Eigenvalue solver to extract the Hard Iron center offsets and Soft Iron 3x3 scaling matrices.
4. It then applies a **Kabsch Rotational Alignment** to mathematically "un-bend" the ~9.4-degree physical flex of the wand's internal fiberglass and board seating on the carrier.
5. The device will automatically save this to `calibration.json` and apply it to all real-time readings.
