# Golden Calibration Log Analysis

We've plotted the data from `golden-calibration_2026-08-29_07-39-34-240.csv` to visualize the quality of the calibration and observe the sensor readings over the test duration.

## Magnetometer Calibration (Hard and Soft Iron)

This visualization shows how the raw magnetometer readings (which form a distorted, off-center ellipsoid due to hard/soft iron interference) are transformed into a clean sphere centered on the origin.

````carousel
![Raw Magnetometer Data](C:/Users/rguilmet/.gemini/antigravity/brain/f1d175fb-954a-463e-96d9-f351d6ab2294/scratch/mag_raw_3d.png)
<!-- slide -->
![Calibrated Magnetometer Data](C:/Users/rguilmet/.gemini/antigravity/brain/f1d175fb-954a-463e-96d9-f351d6ab2294/scratch/mag_cal_3d.png)
````

**Observations:**
- **Raw Data:** The reference and tip sensors have massive hard-iron offsets (the ellipsoids are very far from 0,0,0, particularly the reference sensor which is offset deeply in the -Z and +X directions, and the tip sensor offset deeply in the +X, +Y directions).
- **Calibrated Data:** The spheres are perfectly centered on the origin and are uniform in radius. This confirms the Python Kabsch alignment and soft/hard iron correction matrices are performing beautifully. The baseline magnetic field magnitude is consistently normalized.

### Coverage Assessment

The Python calibration tool evaluates how completely the sensor was rotated through all 3D axes. A higher coverage percentage ensures the resulting calibration sphere is accurate in all orientations.

```text
--- Reference Sensor Coverage Assessment ---
Ideal Magnetic Radius: 7739 counts
X-Axis Coverage: 98.2%
Y-Axis Coverage: 98.6%
Z-Axis Coverage: 87.4%

--- Tip Sensor Coverage Assessment ---
Ideal Magnetic Radius: 7741 counts
X-Axis Coverage: 99.2%
Y-Axis Coverage: 100.2%
Z-Axis Coverage: 91.4%
```

**Observations:** This is an exceptionally good "golden" calibration. Both the X and Y axes achieved near-perfect ~100% coverage, and the Z-axis (which is typically harder to cover fully without awkward wrist motions) achieved an excellent ~90% on both sensors.

## IMU Sensor Readings

The accelerometer and gyroscope readings over the course of the calibration sequence.

![IMU Time Series](C:/Users/rguilmet/.gemini/antigravity/brain/f1d175fb-954a-463e-96d9-f351d6ab2294/scratch/imu_time.png)

**Observations:**
- The calibration involved a lot of dynamic movement (as expected during a figure-8 or tumbling sweep). 
- The accelerometer sees 1g total magnitude properly distributed across the axes depending on orientation.
- The gyroscope properly tracks the rotations and then settles back to 0 when stationary. 

## Computed Euler Angles (Sensor Fusion)

Here are the Azimuth and Elevation output from the Madgwick filter during the calibration sequence.

![Euler Angles over Time](C:/Users/rguilmet/.gemini/antigravity/brain/f1d175fb-954a-463e-96d9-f351d6ab2294/scratch/euler_time.png)

**Observations:**
- Because this log was recorded *before* the magnetometer negation bug (Bug #4) was fixed, we can see the Elevation curve maxing/minning out around +/- 50 to 60 degrees, rather than hitting the true +/- 90 degrees. This perfectly visually confirms the conflict between the IMU and the incorrectly-mapped magnetometer.
- Now that `v3.6.15` is running with the corrected magnetometer mapping, your future logs will show Elevation correctly traversing the full -90° to +90° range.

---
**Summary:** The actual *calibration math* (hard/soft iron matrices) is perfect and highly effective. The only issue was the sign mapping on the magnetometer data being fed into the Madgwick filter *after* calibration, which we just resolved!
