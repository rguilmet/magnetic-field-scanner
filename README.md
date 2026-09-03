# Magnetic Field Scanner (ESP32-S3 + Dual RM3100)

A professional-grade, open-source spatial magnetic gradiometer designed to detect buried ferromagnetic anomalies (property pins, pipes, UXO) by isolating highly localized magnetic gradients from the Earth's background magnetic field.

Powered by an ESP32-S3 and utilizing dual PNI RM3100 magneto-inductive sensors, this device achieves nanotesla-level sensitivity, executing 9-parameter ellipsoidal matrix calibrations and real-time Madgwick AHRS sensor fusion to provide an intuitive, HUD-style radar interface on a high-speed LCD.

---

## Hardware Specifications

* **MCU:** Espressif ESP32-S3 (Dual Core 240MHz, OPI PSRAM).
* **Magnetic Sensors:** Dual PNI RM3100 (TIP and REF) in a spatial gradiometer configuration.
* **IMU:** 6-DoF or 9-DoF IMU (for AHRS attitude tracking and radar projection).
* **Display:** 172x640 QSPI/8080 LCD driven by LVGL.
* **Storage:** External SPI SD Card + Internal FFat (Flash).
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
| **RM3100 DRDY** | TIP: 3, REF: 2, MID: 5 | Interrupt | Direct hardware interrupts for DRDY synchronization. |
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

## Software Architecture (v5.x.x)

The software is built on the Arduino ESP32 Core but heavily utilizes ESP-IDF native features and FreeRTOS for professional-grade isolation and performance.

### 1. Synchronized POLL Architecture & FreeRTOS
The `v5.x.x` architecture completely eliminates I2C collisions and phase-drift by orchestrating synchronized measurements:
* **POLL Mode:** The ESP32 broadcasts a simultaneous `REG_POLL` command to both RM3100 sensors across the I2C bus.
* **Event Groups (Core 0):** The Sensor Task sleeps at 0% CPU until both hardware `DRDY` GPIO interrupts assert, guaranteeing absolute temporal synchronization between the Tip and Reference sensors before reading.
* **LVGL UI Task (Core 1):** Handles display rendering, capacitive touch input, and real-time radar / minimap animations without interrupting the rigid I2C sensor polling pipeline.

### 2. Universal Calibration Matrix
The system applies an advanced 9-parameter Least-Squares Ellipsoid Fit (via the Kabsch Algorithm) to correct for Hard/Soft Iron distortions caused by the battery and LCD:
* **Pre-Normalization:** The `v5.0.0+` architecture converts raw sensor LSBs into normalized nanoTeslas (nT) *before* the calibration matrix is applied.
* **Universal Application:** Because the matrix is mathematically dimensionless, a single calibration profile works universally across ALL Cycle Counts (12 to 3200). You calibrate once at 400 CC, and the matrix remains perfectly valid even if you switch the wand to 3200 CC.

### 3. Dynamic UI Scaling (TARE vs RAW)
The LVGL Gradiometer UI dynamically scales its physical range and color bands based on the active mode:
* **RAW Mode (0 - 25,000 nT):** Wide dynamic range to absorb the baseline physical misalignment of the sensors (typically ~4,900 nT) without pinning the needle in the red.
* **TARE Mode (0 - 5,000 nT):** When active, the software zeroes the baseline, tightening the visual arc (Green: 0-150, Yellow: 150-500, Red: >500) for extreme sensitivity to tiny localized anomalies.

### 4. Dual-Drive Filesystem & Integrated Web Server
* **Smart Fallback:** Dynamically routes file I/O to the high-speed SD Card, or falls back to Internal FFat if the card is missing or corrupted.
* **Captive Portal:** Hosts a Web Server allowing seamless download of `.csv` characterization logs and upload of `calibration.json` profiles.

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
A dedicated interrupt pin (GPIO 5) and I2C address (0x22) have been reserved for a 3rd RM3100 sensor (MID). 
* **Purpose:** Enables 2nd-order magnetic gradiometry, allowing the wand to calculate the exact depth of a magnetic anomaly (e.g., a buried pipe) rather than just its presence.

### 2. Spatial Mapping (GPS / RTK)
The static control pins for the LCD and IMU were aggressively offloaded to the TCA9554 I/O expander specifically to free up the hardware UART pins:
* **TX (GPIO 43) / RX (GPIO 44):** Reserved for a high-precision GPS or RTK module. This will allow the wand to fuse magnetic gradient data with precise geographic coordinates, generating professional magnetic survey heatmaps.

## How to Build & Deploy

### Prerequisites
* **Environment:** Arduino IDE v2+ or VSCode/Arduino.
* **Core:** ESP32 Board Package v3.3.x (ESP-IDF v5 based).

### Board Settings (ESP32-S3 Dev Module)
* **USB Mode:** Hardware CDC and JTAG
* **PSRAM:** OPI PSRAM
* **Flash Mode:** QIO 80MHz
* **Flash Size:** 16MB (128Mb)
* **Partition Scheme:** Custom or `app3M_fat9M_16MB`
* **Core Debug Level:** None (for performance)

### Compilation Note
All code files adhere to a strict Semantic Versioning (`vX.Y.Z`) rule enforced at every file iteration. Ensure you are building the latest `user_config.h` baseline.

## Open Source & Contributing
This project is open-sourced under the **GPLv3 License**. See [LICENSE](LICENSE) for details.
We welcome community contributions! Please review our [Contribution Guidelines](CONTRIBUTING.md) before submitting pull requests.
