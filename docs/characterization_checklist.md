# Characterization Log Collection Checklist

To fully characterize the hardware envelope across multiple Cycle Counts (CC), you must perform a "Sweep" for tests 1 through 5. This means capturing each test at three distinct Cycle Counts:
- **`CC=200`** (Fast/Noisy/High-Saturation)
- **`CC=400`** (Baseline)
- **`CC=3200`** (Slow/Quiet/Low-Saturation)

All logs must be captured in **RAW mode**.

## Core Performance Sweeps (Capture each at 200, 400, and 3200 CC)

- [ ] **1. Noise Floor (`log_noise_[cc].csv`)**
  Set the wand on a non-magnetic surface. Do not touch it. Let it log perfectly still for 10 seconds.
- [ ] **2. Gradiometer Isolation (`log_target_[cc].csv`)**
  Pass a small ferrous object (like a screwdriver or small magnet) close to the Tip Sensor, but keep it away from the Reference sensor in the handle. This proves the wand rejects the Earth's field while highlighting local anomalies.
- [ ] **3. AHRS Stability (`log_ahrs_[cc].csv`)**
  Point the wand North, and aggressively pitch it up and down 45 degrees, and roll it left and right 45 degrees for 10 seconds.
- [ ] **4. Saturation Limit (`log_saturation_[cc].csv`)**
  Slowly bring a strong magnet (e.g., neodymium) closer to the Tip Sensor until the live values flatline/clip.
- [ ] **5. Measurement Precision (`log_repeat_[cc].csv`)**
  Lock the wand physically on a table. Place a target (screwdriver/magnet) on a track or against a guide. 
  - **X-Axis:** Slide the target up to the wand (until it hits a physical stop block), hold for a second, slide it away. Repeat 10 times.
  - **Y-Axis:** Reposition the guide and repeat the slide/hold 10 times.
  - **Z-Axis:** Reposition the guide and repeat the slide/hold 10 times.

## Calibration Performance (Capture ONLY at 400 CC)

*Note: Because calibration matrices are applied to normalized `nT` values, a single matrix works universally across all Cycle Counts. It must be performed at 400 CC to ensure a high-resolution 50Hz capture of the tumbling sphere.*

- [ ] **6. Calibration Consistency (`cal_1.csv` through `cal_5.csv`)**
  Perform the standard figure-8 calibration sequence, save the calibration, and repeat this 5 separate times.

---
*Once all logs are collected, process them simultaneously using:*
```bash
python scripts/characterize_system.py \
  --noise "log_noise_200.csv" "log_noise_400.csv" "log_noise_3200.csv" \
  --target "log_target_200.csv" "log_target_400.csv" "log_target_3200.csv" \
  --ahrs "log_ahrs_200.csv" "log_ahrs_400.csv" "log_ahrs_3200.csv" \
  --repeatability "log_repeat_200.csv" "log_repeat_400.csv" "log_repeat_3200.csv" \
  --saturation "log_saturation_200.csv" "log_saturation_400.csv" "log_saturation_3200.csv" \
  --calibration "cal_1.csv" "cal_2.csv" "cal_3.csv" "cal_4.csv" "cal_5.csv"
```
