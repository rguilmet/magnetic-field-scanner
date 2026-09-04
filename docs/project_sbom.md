# Software Bill of Materials (SBOM)

This document outlines the core firmware dependencies, third-party libraries, and toolchains required to build the Magnetic Field Scanner `v5.x.x`. 

## Core Toolchain & Framework
| Component | Version / Target | Description |
| :--- | :--- | :--- |
| **Platform Framework** | Arduino ESP32 Core `v3.x.x` | Built upon ESP-IDF v5.1. Provides FreeRTOS and base hardware abstractions. |
| **Hardware Target** | ESP32-S3 Dev Module | 16MB Flash, OPI PSRAM, QIO 80MHz, Hardware CDC + JTAG. |

## Third-Party Libraries

| Library Name | Version | License | Description / Purpose |
| :--- | :--- | :--- | :--- |
| **LVGL (Light and Versatile Graphics Library)** | `v8.3.x` | MIT | Drives the core GUI, arc gauges, dynamic color mapping, and capacitive touch handling. Must be configured via `lv_conf.h`. |
| **ArduinoJson** | `v7.0+` | MIT | Used for parsing and serializing `settings.json` and `calibration.json` to/from the SD Card and FFat filesystems. |
| **Madgwick AHRS** | `v1.2.0` (or embedded) | GPL | Sensor fusion algorithm that combines Accelerometer and Gyroscope data into reliable Euler angles (Pitch/Roll/Yaw) to drive the radar projection map. |
| **TCA9554** | *Custom/Embedded* | N/A | I2C I/O Expander driver (Address `0x20`) used to control LCD backlight, display reset, and PMIC `SYS_EN` latch. |
| **ES8311 / ES7210** | *Custom/Embedded* | N/A | I2S Audio Codec drivers utilized by the Waveshare board for dynamic frequency audio feedback. |
| **RM3100 Driver** | *Custom/Embedded* | N/A | Heavily modified to operate exclusively in `POLL` mode across the I2C bus (`0x21`, `0x22`, `0x23`) with raw integer-to-`nT` domain shifting. |

## Auxiliary Python Scripts
*Found in the `/scripts` directory for system characterization and calibration analysis.*

| Package Name | Version | Purpose |
| :--- | :--- | :--- |
| **Python** | `>= 3.9` | Base execution environment. |
| **pandas** | `latest` | High-performance CSV ingestion and DataFrame manipulation for log analysis. |
| **numpy** | `latest` | Matrix mathematics and Least-Squares Kabsch ellipsoid fitting. |
| **matplotlib** | `latest` | Generation of 2D/3D plots and calibration visualizations. |
| **scipy** | `latest` | Advanced statistical metrics (variance, standard deviation) for hardware limit modeling. |
