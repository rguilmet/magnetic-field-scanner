# Characterization Log Collection Checklist

To fully characterize the hardware envelope across multiple Cycle Counts (CC), you must perform a "Sweep" for tests 2 through 6. All logs must be captured in **RAW mode**.

**WARNING:** The order of these tests is strictly hierarchical. Do NOT perform the Saturation or AHRS tests until the others are complete.

## 1. Calibration Performance (Capture ONLY at 400 CC)

*Note: Because calibration matrices are applied to normalized `nT` values, a single matrix works universally across all Cycle Counts. It must be performed at 400 CC to ensure a high-resolution 50Hz capture of the tumbling sphere.*

- [ ] `cal_1.csv` (Figure-8 calibration sequence)
- [ ] `cal_2.csv`
- [ ] `cal_3.csv`
- [ ] `cal_4.csv`
- [ ] `cal_5.csv`

## 2. Noise Floor (Capture at 200, 400, 800, 1600, 3200 CC)

Set the wand on a non-magnetic surface. Do not touch it. Let it log perfectly still for 10 seconds.
- [ ] `log_noise_200.csv`
- [ ] `log_noise_400.csv`
- [ ] `log_noise_800.csv`
- [ ] `log_noise_1600.csv`
- [ ] `log_noise_3200.csv`

## 3. Combined Precision & Isolation (Capture at 200, 400, 3200 CC)

Target: An 8d nail starting 24" away. Lock the wand physically on a table. 
Protocol: Slide block in 6" increments (1s pause per step). At tip (0"), pause for 3s. Reverse direction in 6" increments (1s pause). Repeat 10x per log.
- [ ] `log_repeat_200.csv`
- [ ] `log_repeat_400.csv`
- [ ] `log_repeat_3200.csv`

## 4. Saturation Limit (Capture at 200, 400, 3200 CC)

*Warning: This test uses a massive magnet and risks permanently magnetizing the wand's soft iron components.*
Target: Strong magnet staged 10' away. Wand fixed on ground.
Protocol: Start at 4' mark. Slide in 6" increments (1s pause per step). At tip (or when UI clips), pause for 3s. Reverse direction in 6" increments (1s pause).
- [ ] `log_saturation_200.csv`
- [ ] `log_saturation_400.csv`
- [ ] `log_saturation_3200.csv`

## 5. Open Air Rebar Test (Capture at 200, 400, 3200 CC)

Target: 36" rebar starting 8' away. Wand fixed on ground.
Protocol: Walk target in 1' increments (1s pause per step). At tip (0'), pause for 3s. Reverse direction in 1' increments. (1 pass per log).
- [ ] `log_rebar_200.csv`
- [ ] `log_rebar_400.csv`
- [ ] `log_rebar_3200.csv`

## 6. AHRS Stability (Capture at 200, 400 CC)

*Warning: Violent swinging can twist the sensor board, invalidating the Kabsch matrix.*
Point the wand North, aggressively pitch it up and down 45 degrees, and roll it left and right 45 degrees for 10 seconds.
- [ ] `log_ahrs_200.csv`
- [ ] `log_ahrs_400.csv`

---
*Once all logs are collected, process them simultaneously using:*
```bash
python scripts/characterize_system.py \
  --noise "log_noise_200.csv" "log_noise_400.csv" "log_noise_3200.csv" \
  --repeatability "log_repeat_200.csv" "log_repeat_400.csv" "log_repeat_3200.csv" \
  --saturation "log_saturation_200.csv" "log_saturation_400.csv" "log_saturation_3200.csv" \
  --rebar "log_rebar_200.csv" "log_rebar_400.csv" "log_rebar_3200.csv" \
  --ahrs "log_ahrs_200.csv" "log_ahrs_400.csv" \
  --calibration "cal_1.csv" "cal_2.csv" "cal_3.csv" "cal_4.csv" "cal_5.csv"
```
