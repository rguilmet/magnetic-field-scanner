# System Characterization Methodology

This document outlines the strict empirical methodology required to benchmark and characterize the performance limits of the Magnetic Field Scanner. 

The resulting data from these physical tests is processed by `scripts/characterize_system.py` to generate the official `CHARACTERIZATION.md` datasheet.

## Testing Environment & Configuration
To ensure statistically valid and reproducible results across the entire architectural range, the wand must be benchmarked across multiple Cycle Counts.
- **Cycle Count Sweeps:** The system dynamically normalizes all readings to nanoTeslas (nT) regardless of the Cycle Count (CC). However, changing the CC fundamentally alters the hardware's update speed, physical saturation limit, and RMS noise floor. Therefore, benchmark sets should be collected at `CC=200` (Fast/Noisy/High-Saturation), `CC=400` (Baseline), and `CC=3200` (Slow/Quiet/Low-Saturation) to fully characterize the hardware envelope.
- **Calibration Context:** Because the `v5.x.x` architecture normalizes all raw sensor counts to `nT` *before* the calibration matrix is applied, a single calibration profile is mathematically universal across all Cycle Counts. Calibration should exclusively be performed at `CC=400`. (Performing calibration at `CC=3200` is not recommended because the 5Hz update rate is too slow to capture a smooth 3D rotational sphere, while `CC=400` at ~50Hz is perfect).
- **Mode:** The wand UI must be set to **RAW Mode** (TARE and AUTO-TARE disabled). This exposes the naked noise floor and raw mathematical stability of the hardware, unimpeded by software UI filters or zeroing logic.
- **Environment:** All baseline tests must be conducted in a magnetically quiet area, free of moving ferrous objects or active EMI sources.

---

## The 5 Benchmark Logs

To characterize the system, a user must capture 5 distinct `.csv` log files, each isolating a specific performance vector.

### 1. The Noise Floor Log (`log_noise.csv`)
**Purpose:** Establishes the RMS background noise of the system. This dictates the absolute smallest localized anomaly the wand can reliably detect above the Earth's background field.
**Execution:**
1. Place the wand on a non-magnetic surface (e.g., a wooden table or the ground).
2. Do not touch or move the wand.
3. Start logging, record for 10 seconds, and stop.

### 2. The Gradiometer Isolation Log (`log_target.csv`)
**Purpose:** Proves that the dual-sensor spatial gradiometer setup successfully rejects the Earth's baseline field while highlighting massive local anomalies.
**Execution:**
1. Place the wand on a non-magnetic surface.
2. Start logging.
3. Pass a small ferrous object (like a screwdriver or small magnet) close to the **Tip Sensor** exclusively, keeping it away from the Reference Sensor in the handle.
4. Remove the object and stop logging.

### 3. The AHRS Stability Log (`log_ahrs.csv`)
**Purpose:** Proves that the Kabsch algebraic transformation properly isolates the physical orientation of the dual sensors, preventing the compass heading (Azimuth) from drifting or rolling when the wand is pitched.
**Execution:**
1. Hold the wand in the air and point it directly North.
2. Start logging.
3. Aggressively pitch the wand up and down by 45 degrees, and roll it left and right by 45 degrees for 10 seconds.
4. Stop logging.

### 4. The Saturation Limit Log (`log_saturation.csv`)
**Purpose:** Empirically proves the maximum magnetic field strength the RM3100 sensors can ingest before hardware clipping blinds the gradiometer.
**Execution:**
1. Place the wand on a non-magnetic surface.
2. Start logging.
3. Very slowly bring a strong magnet (e.g., neodymium) closer to the Tip Sensor until the live values on the screen flatline or clip.
4. Remove the magnet and stop logging.

### 5. The Measurement Repeatability Log (`log_repeat.csv`)
**Purpose:** Proves instrument repeatability—that the wand reports the exact same field strength when exposed to the exact same physical anomaly multiple times.
**Execution:**
1. Lock the wand physically on a table so it cannot move or vibrate.
2. Place a target (screwdriver or magnet) on a sliding track or against a hard physical guide.
3. Start logging.
4. Slide the target up to the wand until it hits a physical stop block.
5. Hold it there for 1 second.
6. Slide the target away.
7. Repeat this physical motion exactly 10 times in the same log file.
8. Stop logging.

*(Note: Human hands are too shaky for this test. The inverse-cube law of magnetic fields means a 1mm human error in placement will cause massive signal variance. A physical sliding stop-block is strictly required).*

---

## Generating the Datasheet

Once all 5 benchmark logs are collected in the workspace, run the following Python command to ingest the data, execute the statistical analysis (RMS, Standard Deviation, Peak Variance), and generate the final datasheet:

```bash
python scripts/characterize_system.py \
  --noise "log_noise.csv" \
  --target "log_target.csv" \
  --ahrs "log_ahrs.csv" \
  --repeatability "log_repeat.csv" \
  --saturation "log_saturation.csv"
```
The resulting `CHARACTERIZATION.md` will be placed in the `docs/` folder.
