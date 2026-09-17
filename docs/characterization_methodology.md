# System Characterization Methodology

This document outlines the strict empirical methodology required to benchmark and characterize the performance limits of the Magnetic Field Scanner. 

The resulting data from these physical tests is processed by `scripts/characterize_system.py` to generate the official `characterization.md` datasheet.


## Characterization Jig & Staging
<p align="center">
  <img src="images/Characterization%20Panaroma%20(20260911_092047).jpg" width="100%" />
</p>
<p align="center">
  <img src="images/Characterization%20Jig%20with%20MFS%20Secured%20(20260911_092003).jpg" width="48%" />
  <img src="images/Characterization%20Jig%20Closer%20(20260911_092211).jpg" width="48%" />
</p>
<p align="center">
  <img src="images/Characterization%20Staging%20Area%20(20260911_092239).jpg" width="48%" />
  <img src="images/Characterization%20Blocks%20(10d%20Nail,%20and%208d%20Nail%20with%20Magnet%20(20260911_092223).jpg" width="48%" />
</p>
<p align="center">
  <img src="images/Characterization%200.5in%20x%2036in%20rebar%20(20260911_092205).jpg" width="48%" />
</p>

## Testing Environment & Configuration
To ensure statistically valid and reproducible results across the entire architectural range, the wand must be benchmarked across multiple Cycle Counts.
- **Cycle Count Sweeps:** The system dynamically normalizes all readings to nanoTeslas (nT) regardless of the Cycle Count (CC). However, changing the CC fundamentally alters the hardware's update speed, physical saturation limit, and RMS noise floor. Therefore, benchmark sets should be collected at `CC=200` (Fast/Noisy/High-Saturation), `CC=400` (Baseline), and `CC=3200` (Slow/Quiet/Low-Saturation) to fully characterize the hardware envelope.
- **Calibration Context:** Because the `v5.x.x` architecture normalizes all raw sensor counts to `nT` *before* the calibration matrix is applied, a single calibration profile is mathematically universal across all Cycle Counts. Calibration should exclusively be performed at `CC=400`. (Performing calibration at `CC=3200` is not recommended because the 5Hz update rate is too slow to capture a smooth 3D rotational sphere, while `CC=400` at ~50Hz is perfect).
- **Mode:** The wand UI must be set to **RAW Mode** (TARE and AUTO-TARE disabled). This exposes the naked noise floor and raw mathematical stability of the hardware, unimpeded by software UI filters or zeroing logic.
- **Environment:** All baseline tests must be conducted in a magnetically quiet area, free of moving ferrous objects or active EMI sources. Buffer zones for targets should be at least 40 feet away when not actively tested.

---

## The 6-Stage Benchmark Hierarchy

The order of testing is strictly hierarchical to physically protect the sensor calibration. Tests that risk permanently magnetizing the wand's soft iron (hysteresis) or mechanically twisting the sensor board inside the tube (torsion) are placed at the absolute end.

### 1. Calibration Consistency (`cal_1.csv` to `cal_5.csv`)
**Purpose:** Proves the mathematical repeatability of the figure-8 calibration routine by ensuring the Kabsch algorithm converges on statistically identical hard/soft iron matrices across multiple runs.
**Execution:**
1. Stand in a magnetically quiet area.
2. Trigger the on-wand calibration routine at `CC=400`.
3. Perform the standard 3D figure-8 tumbling sequence until completion.
4. Save the log as `cal_1.csv`.
5. Repeat this exact process 4 more times to collect 5 separate calibration logs.

### 2. The Noise Floor Log (`log_noise.csv`)
**Purpose:** Establishes the RMS background noise of the system. This dictates the absolute smallest localized anomaly the wand can reliably detect above the Earth's background field.
**Execution:**
1. Place the wand on a non-magnetic surface (e.g., a wooden table or the ground).
2. Do not touch or move the wand.
3. Record 10 seconds of data for `CC=200`, `400`, `800`, `1600`, and `3200`.

### 3. Combined Precision & Isolation Log (`log_repeat.csv`)
**Purpose:** Proves Gradiometer Isolation (a massive anomaly at the Tip does not distort the Earth's background field at the Reference sensor) and Measurement Precision (repeatability across a physical $1/r^3$ staircase).

*Note on Single-Axis Testing: Because the wand runs the Kabsch 9-parameter Ellipsoid Fit algorithm (which mathematically forces all three physical coils to conform to a geometrically perfect, identical sphere), it is only necessary to perform this physical precision test on a single primary axis (e.g., the forward-pointing Z or Y axis). The calibration math guarantees that if one axis is perfectly isolated and precise, the other two share that exact same normalized geometric behavior.*

**Execution:**
1. Lock the wand physically on a table so it cannot move.
2. Target: A 10d nail in a wooden block, starting 2 feet (24") away, axially aligned to the wand.
3. Start logging.
4. Slide the block toward the tip in **6-inch increments**, pausing for **1 second** at each step (24", 18", 12", 6").
5. Stop at the tip (0") and pause for **3 seconds**.
6. Reverse direction in 6-inch increments (6", 12", 18", 24"), pausing for 1 second at each step.
7. Repeat this entire pass 10 times in a single log.
8. Repeat for `CC=200`, `400`, and `3200`.

### 4. The Saturation Limit Log (`log_saturation.csv`)
**Purpose:** Empirically proves the maximum magnetic field strength the RM3100 sensors can ingest before hardware clipping blinds the gradiometer, while capturing the steep $1/r^3$ dipole curve of a rare-earth magnet.
**Warning:** This test uses a massive magnet and risks permanently magnetizing the wand's soft iron components (hysteresis).
**Execution:**
1. Wand is fixed on the ground. Magnet is staged at ~10 feet.
2. Start logging. Walk to the 10' mark and pick up the magnet.
3. Approach the 4-foot (48") mark.
4. Slide the magnet toward the tip in **6-inch increments**, pausing for **1 second** at each step.
5. Stop at the tip (or as close as you can get before the UI values visually flatline/clip) and pause for **3 seconds**.
6. Reverse direction in 6-inch increments back to 4 feet, pausing for 1 second at each step.
7. Return the magnet to the 10-foot staging area and stop logging.
8. Repeat for `CC=200`, `400`, and `3200`.

### 5. Open Air Rebar Test (`log_rebar.csv`)
**Purpose:** Captures the magnetic dipole signature of a massive ferrous target (36" rebar) to automatically calculate the maximum Absolute Detection Range via an inverse-cube ($1/r^3$) curve fit.
**Execution:**
1. Wand is fixed on the ground alongside a non-metallic tape measure. Rebar is staged at ~10 feet.
2. Start the log at the wand handle, then walk out to the 10' mark.
3. Pick up the rebar, holding it perpendicular to the ground.
4. Approach the 8-foot mark and begin the staircase: 1-foot increments (8', 7', 6', 5', 4', 3', 2', 1'), pausing for **1 second** per step.
5. Pause at the TIP (0') for **3 seconds**.
6. Reverse direction in 1-foot increments back to 8 feet, pausing for 1 second per step.
7. Stop logging. 
8. Repeat for `CC=200`, `400`, and `3200`.

### 6. The AHRS Stability Log (`log_ahrs.csv`)
**Purpose:** Proves that the Kabsch algebraic transformation properly isolates the physical orientation of the dual sensors.
**Warning:** Violent swinging can physically twist the sensor board inside the tube, permanently invalidating the Kabsch matrix. This test must be performed last.
**Execution:**
1. Hold the wand in the air and point it directly North.
2. Start logging.
3. Aggressively pitch the wand up and down by 45 degrees, and roll it left and right by 45 degrees for 10 seconds.
4. Repeat for `CC=200` and `400`.

### 6. The 45-Degree Stress Test (AHRS Validation)
**Purpose:** An extreme empirical stress test of the Madgwick AHRS filter and the Kabsch calibration matrix. By locking the wand at a 45-degree pitch and roll while sweeping over a known anomaly, we force massive gravitational and magnetic cross-axis distortion. If the software can isolate the anomaly from the massive background shifts, the math is mathematically bulletproof.
**Execution:**
1. Secure the wand in a rigid jig fixed at a 45-degree pitch and 45-degree roll.
2. Slide the 36-inch 0.5" rebar beneath the tip in 6-inch increments, mimicking the standard target test.
3. Repeat for `CC=400`, `800`, `1600`, and `3200`.

---

## Generating the Datasheet

Once all benchmark logs are collected in the workspace, run the following Python command to ingest the data, execute the statistical analysis, and generate the final datasheet:

```bash
python scripts/characterize_system.py \
  --noise "log_noise_200.csv" "log_noise_400.csv" "log_noise_3200.csv" \
  --repeatability "log_repeat_200.csv" "log_repeat_400.csv" "log_repeat_3200.csv" \
  --saturation "log_saturation_200.csv" "log_saturation_400.csv" "log_saturation_3200.csv" \
  --rebar "log_rebar_200.csv" "log_rebar_400.csv" "log_rebar_3200.csv" \
  --ahrs "log_ahrs_200.csv" "log_ahrs_400.csv" "log_ahrs_3200.csv" \
  --calibration "cal_1.csv" "cal_2.csv" "cal_3.csv" "cal_4.csv" "cal_5.csv"
```
The resulting `characterization.md` will be placed in the `docs/` folder.

### Understanding Saturation Testing
When characterizing the wand's dynamic range, it is crucial to understand that there are two distinct physical phenomena being tested:
1. **The Rebar Test (Digital Clipping):** By using a large piece of steel (like a 36-inch rebar) which concentrates the Earth's field, we can subject the sensor to a massive ~500 µT anomaly. This tests the **Digital Clipping Limit** of the internal 24-bit integer counter without physically destroying the magnetic properties of the sensor core.
2. **The Neodymium Magnet Test (Physical Core Saturation):** By bringing a rare-earth magnet directly to the tip, we subject the sensor to an overwhelming Gauss-level magnetic field. This forcibly pushes the RM3100's inductor core past its B-H curve limit, causing the inductance to collapse. This tests the **Physical Core Blind State**, where the sensor fails to oscillate and outputs a `0` count (resulting in the Gradiometer displaying the Earth's background field). 

Both tests are strictly required to map the complete failure envelope of the wand across different Cycle Counts.
