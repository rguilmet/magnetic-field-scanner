# Magnetic Field Scanner (MFS) Wand

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)
[![Firmware](https://img.shields.io/badge/Firmware-v4.0.0-green.svg)]()
[![Platform](https://img.shields.io/badge/Platform-ESP32--S3-orange.svg)]()
[![Build](https://img.shields.io/badge/Build-Arduino%20|%20PlatformIO-lightgrey.svg)]()


The Magnetic Field Scanner (MFS) Wand is a high-precision spatial magnetic field mapping instrument. Powered by an ESP32-S3, it features dual RM3100 geomagnetic sensors, a 6-axis IMU, an RTOS-driven architecture, and a full LVGL-based touchscreen user interface.

## Key Features
* **Zero-Latency ISR Architecture**: Sensor polling is driven by FreeRTOS Event Groups triggered directly by the RM3100 `DRDY` hardware interrupts.
* **Aggressive TMRC Hardware Tuning**: Capable of reading at blistering 150Hz speeds (200 Cycle Count), with a robust 75Hz default (400 Cycle Count).
* **Extreme Depth Settings**: Supports Cycle Counts up to 3200 (running at 9Hz) for maximum physical depth penetration.
* **Dynamic Madgwick Integration**: Time-dilation issues at slower speeds are solved via dynamic `dt` tracking in the Sensor Fusion loop.
* **SD Logging Decimation**: Intelligently decimates 150Hz readings to 75Hz during SD Card writes to prevent SD-bus latency from dropping sensor frames.

## Hardware Configuration

### Core Components
* **Base Platform:** Waveshare ESP32-S3-Touch-LCD-3.49 v3 (PCBA v1.1 silkscreen). This highly integrated device provides the MCU, display, audio, and power management core.
* **MCU:** ESP32-S3 (16MB Flash, OPI PSRAM) embedded on the Waveshare board.
* **Magnetometers:** 2x RM3100 (TIP and REF sensors), with hardware reserved for a 3rd (MID).
* **IMU:** QMI8658 (6-axis Accelerometer & Gyroscope) offset mechanically by 60° relative to the wand axis.
* **Display:** 172x640 QSPI LCD with capacitive touch.
* **Audio Codec:** ES8311 / ES7210 via I2S for audio feedback.
* **Storage:** External SPI SD Card + Internal FFat (Flash).
* **RTC:** PCF85063 for precise timestamping.
* **I/O Expander:** TCA9554 to offload static control pins (Backlight, Resets) and free up high-speed GPIO.

---

## Pin Allocations & Rationale

To support this many peripherals on a single ESP32-S3, strict pin management and an I2C I/O expander are utilized. 

| Feature | GPIO / Pin | Protocol / Type | Rationale |
| :--- | :--- | :--- | :--- |
| **I2C Bus** | SDA: 47, SCL: 48 | I2C | Shared bus for RM3100s, IMU, RTC, and TCA9554 IO Expander. |
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

## Software Architecture

The software is built on the Arduino ESP32 Core but heavily utilizes ESP-IDF native features and FreeRTOS for professional-grade isolation and performance.

### 1. FreeRTOS Task Isolation
The architecture strictly separates deterministic sensor polling from UI rendering:
* **Sensor Polling Task:** Pinned to Core 0 (or Core 1 depending on load). Runs a tight loop polling the RM3100 sensors synced to the hardware DRDY pins (Capped at 50Hz via software). 
* **LVGL UI Task:** Pinned to the opposing core. Handles the display rendering, touch inputs, and UI updates without interrupting the I2C sensor bus.

### 2. Dual-Drive Filesystem & Smart Fallback
The scanner features a robust storage abstraction (`get_active_fs()`) that dynamically routes file I/O to the SD Card if present, or falls back to Internal FFat if the card is missing or corrupted. 
* JSON configuration files (`settings.json`, `calibration.json`) will intelligently load from FFat if missing on a newly inserted SD card, and then automatically migrate.

### 3. Integrated Web Server
The device hosts a captive-portal-capable Web Server allowing the user to seamlessly download logs, upload calibration matrices, and format drives. It explicitly serves HTTP headers (`Content-Disposition`, `Content-Length`) for flawless browser compatibility across local networks.

### 4. Calibration & Spatial Geometry (SensorFusion)
All intensive 3D mathematics have been encapsulated into a stateful `SensorFusion` C++ class to maintain strict Single Responsibility Principle (SRP) and keep the I2C polling loop clean.
* **Hard/Soft Iron Correction:** The system applies 3x3 rotational/scaling matrices and 3D offset vectors to the raw magnetic data to correct for local distortions caused by the battery and LCD.
* **IMU Mechanical Offset (`imu_rotation_deg`):** A fixed `60.0` degree rotation is mathematically applied to the IMU to account for the physical bend between the wand handle and the sensor shaft, allowing accurate radar projection mapping.
* **Dynamic Auto-Zero:** Eliminates Gyroscope thermal drift by silently recalculating FOC bias during periods of extreme stillness.

---

## Real-World Capabilities & Detection Depth

The Magnetic Field Scanner is designed as a spatial gradiometer. Because it relies on an empirical RMS noise floor of **±0.33 µT**, it is highly sensitive to buried ferromagnetic objects.

### Use Case: Locating Property Pins (5/8" Rebar)
A standard 5/8" steel property pin driven vertically into the earth acts as a magnetic monopole, concentrating the Earth's magnetic flux at its tip. 

**Estimated Detection Depth:**
- A vertical 5/8" property pin generates a surface anomaly of roughly `500 µT` at 1 inch.
- Because the wand is a gradiometer with a long handle (the sensors are spaced far apart), the near-field signal decays according to the inverse-square law (`1/r²`).
- To be reliably detected above the wand's `0.33 µT` noise floor, the signal needs a Signal-to-Noise Ratio (SNR) of at least 3 (approx. `1.0 µT`).
- **Calculation:** `1.0 µT = 500 µT * (1 / r²)` → `r = ~22 inches`.

**Conclusion:**
In a magnetically quiet environment (RAW mode), you can expect to reliably detect a standard vertical property pin buried under **1.5 to 2 feet (18 to 24 inches)** of dirt or concrete.

### Practical Tips for Pin Locating
- **Sweep Low:** Keep the tip sensor as close to the ground as possible. Every inch of air gap costs you an inch of dirt penetration.
- **Use Audio:** Rely on the dynamic FOC audio feedback. The human ear is incredibly adept at picking up the slow frequency rise of a deep anomaly long before the UI screen indicates a massive spike.
- **Vertical vs Horizontal:** Property pins driven vertically are *much* easier to detect than pipes lying horizontally, because the vertical rod concentrates the Earth's magnetic field directly into a concentrated point (a monopole) at the surface.

---

## Future Expansion Roadmap

The hardware architecture has been deliberately designed to accommodate future capabilities without requiring a PCB spin:

### 1. 3D Depth Sensing (3rd RM3100)
A dedicated interrupt pin (GPIO 5) and I2C address (0x22) have been reserved for a 3rd RM3100 sensor (MID). 
* **Purpose:** Enables 2nd-order magnetic gradiometry, allowing the wand to calculate the exact depth of a magnetic anomaly (e.g., a buried pipe) rather than just its presence.
* **Placement Strategy:** For near-surface target detection (where the field gradient changes rapidly), it is highly recommended to place this 3rd sensor **closer to the TIP** rather than exactly in the middle. Logarithmic spacing (biasing sensors toward the target interface) provides much higher sensitivity to shallow depth variations, while the REF sensor at the top acts as the far-field environmental baseline.

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
