# Critical Design Review (CDR)
**Architecture Version:** `v5.1.x`
**Perspective:** Principal Software Engineer / Systems Architect

## 1. Executive Summary
The Magnetic Field Scanner `v5.1.x` represents a fundamental paradigm shift from legacy polling loops to a deterministic, RTOS-driven sensor fusion engine. This document serves as the formal architectural record of the mathematical and hardware-level breakthroughs that allow the ESP32-S3 to achieve true nanotesla-level spatial gradiometry.

## 2. The I2C Phase Collision & The POLL Architecture
In legacy versions (`v4.x.x`), the RM3100 sensors were placed in Continuous Measurement Mode (CMM). The ESP32 would wait for a hardware `DRDY` (Data Ready) interrupt and blindly fetch the results.

### The Problem: Sensor Lapping
Because the ESP32 handles dynamic SD Card writes and complex LVGL UI rendering, the I2C polling loop was occasionally subjected to latency spikes (e.g., waiting for an SD card block flush). While the ESP32 was blocked, the RM3100 sensors continued measuring autonomously. If a sensor finished a *second* measurement before the ESP32 fetched the *first*, the sensor would hold the `DRDY` pin permanently `HIGH` and refuse to clock out data, resulting in a permanent bus lockup.

### The Solution: Deterministic POLL Mode
In `v5.1.x`, we completely stripped the sensors of their autonomy. 
The RM3100 is now operated exclusively in **POLL Mode**. 
1. The ESP32 broadcasts a simultaneous `REG_POLL` command to all sensors across the I2C bus.
2. The Sensor Task drops into a 0% CPU `vTaskDelay` via FreeRTOS Event Groups, waiting for the physical `DRDY` GPIO interrupts to assert.
3. Because the measurement is explicitly triggered by the master, it is impossible for the sensor to "lap" the CPU. The phase-lock between the TIP and REF sensors is mathematically guaranteed, ensuring the spatial gradient calculation is comparing the exact same slice of time.

## 3. Universal Calibration & The `nT` Pre-Normalization
Magnetic sensors natively output raw integer counts (LSBs). In legacy versions, the Kabsch calibration algorithm was applied directly to these raw counts.

### The Problem: Cycle Count Scaling
The RM3100's hardware gain is governed by its Cycle Count (CC) register. A reading of `1000` at `CC=200` represents a vastly different magnetic field than `1000` at `CC=800`. Furthermore, the sensor exhibits non-linear Zero-Field Offsets (ZFO). Attempting to scale the Hard-Iron calibration matrix in software to account for CC changes resulted in massive mathematical drift and UI pinning.

### The Solution: Domain Shifting
In `v5.x.x`, the architecture intercepts the raw LSBs and immediately converts them into physical **nanoTeslas (nT)** *before* any calibration is applied. 
- By shifting the data into a physical domain, the resulting Kabsch Hard-Iron and Soft-Iron matrices become **mathematically dimensionless**.
- **The Victory:** You can now generate a highly accurate calibration ellipsoid at `CC=400` (for high-speed UI rendering), and then drop the wand into a deep-penetration `CC=3200` scan without changing the calibration matrix at all. The calibration is truly universal.

## 4. Architectural Scalability: The 3rd Sensor (NEAR)
The wand is physically designed to support a 3rd RM3100 sensor, placed 8 inches from the TIP (the `NEAR` sensor). 

Because the `v5.1.x` architecture uses synchronized `POLL` triggering and FreeRTOS Event Groups, adding the 3rd sensor requires zero fundamental architectural changes. The ESP32 simply requires the 3rd `DRDY` interrupt bit to set in the Event Group before unblocking the I2C read cycle. This guarantees that 1st-order gradients (TIP-REF) and 2nd-order gradients (TIP-NEAR-REF) are perfectly phase-aligned without introducing any software bottlenecking.

---

## Appendix A: Mathematical Target Modeling

### 5/8" Rebar Monopole Calculation
When testing the wand's detection capabilities, a standard 5/8" x 36" steel property pin is used. To understand the physics of detection, we model the pin as a highly permeable ferromagnetic cylinder immersed in the Earth's background field.

**Variables:**
* Earth's background field ($B_{earth}$): ~50,000 nT (vertical component dominates at mid-latitudes).
* Relative Permeability of Steel ($\mu_r$): ~100 to 1000.
* Cylinder dimensions: $r = 0.3125$ inches, $L = 36$ inches.

Because the rebar is highly permeable and vertically aligned, it acts as a flux concentrator. The Earth's magnetic field lines "funnel" into the steel, exiting at the top tip, creating a localized magnetic monopole.

The induced magnetic moment ($m$) of a slender rod in a uniform field is roughly proportional to its volume and the external field:
$m \approx \frac{\mu_r - 1}{N} \cdot V \cdot B_{earth}$
(Where $N$ is the demagnetization factor, heavily dependent on the length/diameter ratio).

At the surface (e.g., 1 inch above the tip), this flux concentration easily reaches a localized magnitude of **~50,000 to 500,000 nT** depending on the specific alloy's permeability. 

However, because the sensor is a spatial gradiometer taking the difference between two points, the detected anomaly magnitude decays rapidly. While a true dipole decays at $1/r^3$, the near-field gradient of a monopole concentrates heavily at the source. To achieve a Signal-to-Noise Ratio (SNR) of 3:1 against the wand's empirical $\pm0.33 \mu T$ noise floor, the physical gradient must be $\approx 1.0 \mu T$. Based on inverse-cube spatial decay, this threshold is crossed at approximately **18 to 24 inches** of depth.
