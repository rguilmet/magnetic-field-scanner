# Phase 5 Implementation Plan: Quaternions & Geiger Mode

This plan covers the implementation of Quaternion Sensor Fusion (AHRS), the new Geiger Counter Audio Mode (GCM), and updates to the data logging architecture.

## User Review Required
**Thermal Drift Discussion:**
You made a brilliant observation regarding the temperature offset. The QMI8658 is trapped inside the Waveshare display enclosure, which gets warmed by the ESP32 CPU, the LCD backlight, and the battery charging circuit. Meanwhile, the RM3100 sensors are 24 inches down a tube, subject to the ambient air and ground temperatures. 
Because these two thermal zones are completely decoupled, attempting to compensate the RM3100 using the IMU's temperature would likely inject **more error** into the system. 
*Recommendation:* We abandon IMU-based thermal compensation for the RM3100. The existing "Auto-Tare" filter is actually the mathematically superior way to eat away slow thermal drift without needing local temperature probes.

## Proposed Changes

### user_config.h
* **Version Bump:** Update FIRMWARE_VERSION to 3.2.0 (Minor bump due to new features).
* **Settings:** Add definitions for the Geiger Mode (e.g., click duration).

### src/imu_bsp/
* **[NEW] MadgwickAHRS.c & MadgwickAHRS.h**: We will drop in the industry-standard, lightweight Madgwick filter algorithm. It operates entirely on math without heavy dependencies and is perfect for the ESP32.

### Magnetic_Field_Scanner.ino
* **[MODIFY] SensorTask:** Feed the raw Accel/Gyro data into MadgwickAHRSupdateIMU() at 400Hz. Store the resulting q0, q1, q2, q3 values globally.
* **[MODIFY] AudioTask:** Add a case 3: for Geiger Counter Mode (GCM). Instead of outputting a continuous pitch, it will emit a short, sharp audio burst ("click"), wait for a delay period, and repeat. 
  * **Logarithmic Scaling:** Just like the continuous pitch mapping, the delay between clicks will use a logarithmic scale log10(magnitude) to handle the massive dynamic range of the RM3100. Small anomalies will cause distinct clicks (e.g., 200ms apart), while massive anomalies will blend into a rapid buzz (e.g., 10ms apart).
* **[MODIFY] Logging Call:** Pass the 4 new quaternion floats into the log_data() function.

### src/lvgl_port/lvgl_port.c
* **[MODIFY] Waveform Button Callback:** Expand the logic to cycle through 0 (SQR), 1 (TRI), 2 (SIN), and 3 (GCM). Update the LVGL button label text to display "Wave: GCM" when selected.

### src/mfs_api.h
* **[MODIFY] log_data Prototype:** Expand the function signature to accept loat qw, float qx, float qy, float qz.

### src/data_logger/data_logger.cpp
* **[MODIFY] CSV Headers:** Add ,QW,QX,QY,QZ to the header row generated in start_logging().
* **[MODIFY] CSV Row Generation:** Inject the quaternion values into the snprintf data row inside log_data().

## Verification Plan
1. Compile the firmware.
2. Verify the LVGL button cycles to "GCM".
3. Test audio output to confirm discrete Geiger clicks that speed up when moving near metal.
4. Take a short scan and check the SD card CSV to ensure QW, QX, QY, and QZ columns exist and are populated with numbers between -1.0 and 1.0.

