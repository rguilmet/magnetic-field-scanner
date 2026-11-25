# Magnetic Field Scanner (MFS) Wand

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)
[![Firmware](https://img.shields.io/badge/Firmware-v6.0.1-green.svg)]()
[![Platform](https://img.shields.io/badge/Platform-ESP32--S3-orange.svg)]()
[![Build](https://img.shields.io/badge/Build-Arduino%20|%20PlatformIO-lightgrey.svg)]()

A professional-grade, open-source spatial magnetic gradiometer designed to detect buried ferromagnetic anomalies (property pins, pipes, UXO) by isolating highly localized magnetic gradients from the Earth's background magnetic field.

Powered by an ESP32-S3 and utilizing dual PNI RM3100 magneto-inductive sensors, this device achieves nanotesla-level sensitivity, executing 9-parameter ellipsoidal matrix calibrations and real-time Madgwick AHRS sensor fusion to provide an intuitive, HUD-style radar interface on a high-speed LCD.

---


<p align="center">
  <img src="docs/images/Magnetic%20Field%20Scanner%20-%20Side%20View%20(20260914_125728).png" alt="Magnetic Field Scanner Hardware Side View" width="100%" />
</p>
<p align="center">
  <img src="docs/images/Magnetic%20Field%20Scanner%20-%20Display%20View%20(20260914_123335).png" alt="Magnetic Field Scanner LCD Display View" width="80%" />
</p>

| ![HUD Interface - RAW Mode](docs/images/Magnetic%20Field%20Scanner%20-%20Main%20Screen%20-%20RAW.-%20v5.1.13.png) | ![HUD Interface - AUTO Mode](docs/images/Magnetic%20Field%20Scanner%20-%20Main%20Screen%20-%20AUTO%20-%20v5.1.13.png) | ![System Hardware and Diagnostics Screen](docs/images/Magnetic%20Field%20Scanner%20-%20System%20%26%20Hardware%20-%20800%20CC%20-%20v5.1.13..png) |
|:---:|:---:|:---:|

## Hardware Configuration

| ![Internals Left (I2C Pull-ups)](docs/images/Magnetic%20Field%20Scanner%20-%20Inside%20Shot%20Left%20-%20Annoated%20(20260914_160351).png) | ![Internals Overview](docs/images/Magnetic%20Field%20Scanner%20-%20Inside%20Shot%20Overview%20-%20Angled%20(20260914_160644).png) | ![Internals Right](docs/images/Magnetic%20Field%20Scanner%20-%20Inside%20Shot%20Right%20(20260914_160351).png) |
|:---:|:---:|:---:|

| ![Exploded Side View](docs/images/Magnetic%20Field%20Scanner%20-%20Exploded%20Side%20View%20(20260914_163716).png) | ![TIP Sensor Front](docs/images/Magnetic%20Field%20Scanner%20-%20Exploded%20View%20-%20Closeup%20TIP%20FRONT%20(20260914_163735).png) | ![REF Sensor Front](docs/images/Magnetic%20Field%20Scanner%20-%20Exploded%20View%20-%20Closeup%20REF%20FRONT%20(20260914_163749).png) |
|:---:|:---:|:---:|

### Core Components
* **Base Platform:** Waveshare ESP32-S3-Touch-LCD-3.49 v3 (PCBA v1.1 silkscreen). This highly integrated device provides the MCU, display, audio, and power management core.
* **MCU:** ESP32-S3 (16MB Flash, OPI PSRAM) embedded on the Waveshare board.
* **Magnetometers:** 2x RM3100 (TIP (at 0") and REF (at 24") sensors), with hardware reserved for a 3rd (NEAR at 8").
* **IMU:** QMI8658 (6-axis Accelerometer & Gyroscope) offset mechanically by 60° relative to the wand axis.
* **Display:** 172x640 QSPI LCD with capacitive touch.
* **Audio Codec:** ES8311 / ES7210 via I2S for audio feedback.
* **Storage:** External SPI SD Card + Internal FFat (Flash).
* **Wiring:** See the [MFS Wiring Diagram](docs/electrical/wiring/MFS_Wiring_Diagram.png).
* **RTC:** PCF85063 for precise timestamping.
* **I/O Expander:** TCA9554 to offload static control pins (Backlight, Resets) and free up high-speed GPIO.

---

## Pin Allocations & Rationale

To support this many peripherals on a single ESP32-S3, strict pin management and an I2C I/O expander are utilized. 

| Feature | GPIO / Pin | Protocol / Type | Rationale |
| :--- | :--- | :--- | :--- |
| **I2C Bus** | SDA: 47, SCL: 48 | I2C | Shared bus for RM3100s, IMU, RTC, and TCA9554 IO Expander (capped at 200kHz for cable capacitance). |
| **I2S Audio** | MCLK: 7, BCLK: 15, WS: 46, DIN: 6, DOUT: 45 | I2S | Full duplex audio codec interface. |
| **SD Card** | CS: 38, MOSI: 39, MISO: 40, SCLK: 41 | SPI | Dedicated high-speed SPI bus for high-bandwidth data logging. |
| **LCD Display** | CS: 9, PCLK: 10, D0-D3: 11, 12, 13, 14, TE: 21 | QSPI / 8080 | High-speed bus dedicated to driving the 172x640 LVGL display. |
| **Battery ADC** | 4 | Analog (ADC1) | Dedicated for battery voltage monitoring. |
| **RM3100 DRDY** | TIP: 3, REF: 2, NEAR: 5 | Interrupt | Direct hardware interrupts for DRDY synchronization. |
| **Future GPS** | TX: 43, RX: 44 | UART | Reserved. Freed up by migrating static control pins to the IO Expander. |

### TCA9554 I/O Expander (Address 0x20)
Used to handle low-speed/static signals to conserve MCU pins:
* **P0:** Touch Interrupt
* **P1:** LCD Backlight Enable
* **P2 / P3:** IMU Interrupts 1 & 2
* **P4:** RTC Interrupt
* **P5:** LCD Reset
* **P6:** System Power Enable (Keep-alive)
* **P7:** Night/Sleep Mode

---

## Software Architecture (v6.x)

Built on top of [`Field_Instrument_Core`](https://github.com/rguilmet/field-instrument-core) (`>= v1.1.1`) and FreeRTOS under a unified dual-core & memory contract:

### 1. Synchronized POLL Architecture & Unified FreeRTOS Contract
The `v6.x` architecture delegates hardware BSP, I2C fault recovery, LVGL port/PSRAM isolation, RTC, web portal, 9-DOF Madgwick math, matrix calibration math, and lock-free BMP screenshots to `Field_Instrument_Core`:
* **Core `0` (`FIC_CORE_ACQ`):** Runs the Wi-Fi/TCP-IP stack and `SensorTask` (`FIC_PRIO_UI_FEED = 2`). `SensorTask` broadcasts simultaneous `REG_POLL` commands across `I2C0` (`200 kHz`) and sleeps at `0%` CPU until both hardware `DRDY` GPIO interrupts assert before reading `Tip` (`0x23`), `Ref` (`0x21`), and `QMI8658` (`0x6B`).
* **Core `1` (`FIC_CORE_UI`):** Strictly prioritizes real-time I2S `ES8311` audio (`AudioTask` at `FIC_PRIO_AUDIO = 4`) above `LVGL` rendering (`FIC_PRIO_LVGL = 3` with `16`-strip `<= 16 KB` DMA flushes and `I2C1` `0x3B` touch), UI feed / power tasks (`DisplayTask`, `mfs_button_pwr_task` at `FIC_PRIO_UI_FEED = 2`), and Priority-1 `loop()` background web portal polling + `5 s` debounced `save_settings()` persistence.
* **Resilient `I2C0`/`I2C1` & Internal `DRAM0` Isolation:** Enforces recursive bus mutexes, 18-clock GPIO SCL bus-clear, sentinel glitch detection, and RM3100 `I2C0` recovery hooks, while routing LVGL widget allocations `>= 64 B` to Octal PSRAM (`16%` static `DRAM0` / `~162 KB` free internal heap post-setup).

### 2. Deep Mathematics: Kabsch & Madgwick Algorithms
To achieve survey-grade spatial accuracy, the ESP32 executes ellipsoid matrix calibration and 9-axis sensor fusion using `Field_Instrument_Core`'s shared math primitives:
* **Kabsch Rotational Alignment (`fic_matrix_math.h`):** Runs a 9-parameter Least-Squares Ellipsoid Fit (Jacobi Eigenvalue solver + `kabsch_align_3x3_f32()`) to correct Hard and Soft Iron magnetic distortions caused by the battery and LCD.
* **Instance-Safe Madgwick 9-DOF (`fic_ahrs_9dof.h`):** `SensorFusion` encapsulates `Ahrs9DofState_t` (`ahrs_9dof_madgwick_9dof()`) to fuse Tip/Ref magnetometers with QMI8658 accelerometer and gyroscope data, projecting the gradient vector onto Earth's gravity vector.
* **Universal Domain Shifting:** Converts raw sensor LSBs into normalized physical nanoTeslas (`nT`) *before* applying Kabsch calibration matrices so a single profile scales across all Cycle Counts (`200` to `3200`).

### 3. Polargraphic Display & UI Scaling
The split LVGL `9.6.0` HUD (`mfs_ui_create.c` / `mfs_ui_update.c`) features a Log10 Polargraphic Arc showing vertical gradient polarity (`trueZ`): left (Blue) for negative vertical gradient ('halo' outside a pin), right (Red) for positive vertical gradient (centered over a pin).
* **RAW Mode:** Displays the absolute magnetic gradient while absorbing mechanical sensor misalignment offsets.
* **TARE Mode:** Manual baseline zeroing for maximum sensitivity around localized targets.
* **AUTO Mode:** Slowly adapts out thermal and geologic drift below threshold while preserving sharp dipole spikes.

### 4. Dual-Drive Filesystem & Shared Web Portal
* **Atomic Persistence (`fic_settings_json`):** `settings.json` and `calibration.json` use atomic `.tmp` + read-back length verify + `.bak` writes mirrored from SD primary (`SPI`) to internal `FFat` fallback.
* **Captive Portal (`fic_web_portal`):** Core 1 HTTP portal supporting sortable CSV logs, lock-free BMP screenshots (`fic_screenshot`), NTP RTC sync, and OTA calibration uploads with deadlock-free spinlock status buffering.

---

## Real-World Capabilities & Detection Depth

The Magnetic Field Scanner is designed as a spatial gradiometer. Because it relies on an empirical RMS noise floor of **±0.33 µT** (at 400 CC), it is highly sensitive to buried ferromagnetic objects.

### Use Case: Locating Property Pins (5/8" Rebar)
A standard 5/8" steel property pin driven vertically into the earth acts as a magnetic monopole, concentrating the Earth's magnetic flux at its tip. 

**Estimated Detection Depth:**
- A vertical 5/8" property pin generates a surface anomaly of roughly `500 µT` at 1 inch.
- Because the wand is a gradiometer with a long handle, the near-field gradient of a monopole decays according to the inverse-cube law (`1/r³`).
- To be reliably detected above the wand's noise floor, the signal needs a minimum Signal-to-Noise Ratio (SNR).
- In a magnetically quiet environment (RAW mode), you can expect to reliably detect a standard vertical property pin buried under **1.5 to 2 feet (18 to 24 inches)** of dirt or concrete.

### Practical Tips for Pin Locating
- **Sweep Low:** Keep the tip sensor as close to the ground as possible. Every inch of air gap costs you an inch of dirt penetration.
- **Use Audio:** Rely on the dynamic FOC audio feedback. The human ear is incredibly adept at picking up the slow frequency rise of a deep anomaly long before the UI screen indicates a massive spike.
- **Vertical vs Horizontal:** Property pins driven vertically are *much* easier to detect than pipes lying horizontally, because the vertical rod concentrates the Earth's magnetic field directly into a concentrated point (a monopole) at the surface.

---

## System Characterization

The wand features a rigid characterization methodology to empirically validate its hardware envelope across multiple Cycle Counts (CC=200, 400, 3200). 
To test your hardware, use the included Python script on your generated `.csv` logs:

```bash
python scripts/characterize_system.py   --noise "log_noise_200.csv" "log_noise_400.csv" "log_noise_3200.csv"   --target "log_target_200.csv" "log_target_400.csv" "log_target_3200.csv"   --ahrs "log_ahrs_200.csv" "log_ahrs_400.csv" "log_ahrs_3200.csv"   --repeatability "log_repeat_200.csv" "log_repeat_400.csv" "log_repeat_3200.csv"   --saturation "log_saturation_200.csv" "log_saturation_400.csv" "log_saturation_3200.csv"   --calibration "cal_1.csv" "cal_2.csv" "cal_3.csv" "cal_4.csv" "cal_5.csv"
```
See `docs/characterization_methodology.md` for full instructions.

---

## Future Expansion Roadmap

The hardware architecture has been deliberately designed to accommodate future capabilities without requiring a PCB spin:

### 1. 3D Depth Sensing (3rd RM3100)
A dedicated interrupt pin (GPIO 5) and I2C address (0x22) have been reserved for a 3rd RM3100 sensor (NEAR). 
* **Purpose:** Enables 2nd-order magnetic gradiometry, allowing the wand to calculate the exact depth of a magnetic anomaly (e.g., a buried pipe) rather than just its presence.

### 2. Spatial Mapping (GPS / RTK)
The static control pins for the LCD and IMU were aggressively offloaded to the TCA9554 I/O expander specifically to free up the hardware UART pins:
* **TX (GPIO 43) / RX (GPIO 44):** Reserved for a high-precision GPS or RTK module. This will allow the wand to fuse magnetic gradient data with precise geographic coordinates, generating professional magnetic survey heatmaps.

## How to Build & Deploy

### Prerequisites
* **Environment:** Arduino IDE v2+ or `arduino-cli`.
* **Core:** ESP32 Board Package v3.3.x (`esp32:esp32`, ESP-IDF v5 based).
* **Shared BSP Library:** [`Field_Instrument_Core`](https://github.com/rguilmet/field-instrument-core) `>= v1.1.0` installed under `Documents/Arduino/libraries/Field_Instrument_Core` (plus `lvgl v9.6.x`, `ArduinoJson v7.x`, and `SensorLib`).
* **Standalone Pre-Core Archive:** The final self-contained standalone firmware release prior to `Field_Instrument_Core` extraction is preserved at Git tag `v5.1.15`.

### Board Settings (ESP32-S3 Dev Module)
* **USB Mode:** Hardware CDC and JTAG (`USBMode=hwcdc,CDCOnBoot=cdc`)
* **PSRAM:** OPI PSRAM (`PSRAM=opi`)
* **Flash Mode:** QIO 80MHz
* **Flash Size:** 16MB (128Mb)
* **Partition Scheme:** Custom or `app3M_fat9M_16MB`
* **Core Debug Level:** None (for performance)

### Compilation Note
All code files adhere to strict Semantic Versioning (`vX.Y.Z`) rules enforced at every iteration. Ensure you are building the latest `user_config.h` baseline alongside `Field_Instrument_Core >= v1.1.0`.

## Open Source & Contributing
This project is open-sourced under the **GPLv3 License**. See [LICENSE](LICENSE) for details.
We welcome community contributions! Please review our [Contribution Guidelines](CONTRIBUTING.md) before submitting pull requests.
