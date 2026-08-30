# Magnetic Field Scanner - Future Ideas & Roadmap

This document captures brainstormed ideas for increasing the usability, capability, and professional feel of the MFS wand for Phase 5 and beyond.

## 1. The Screen & UI Experience
* **Scrolling "Seismograph" Strip Chart:** Add a 4th LVGL tile containing an lv_chart. As the user walks over a buried pipe, it will draw the "bell curve" of the magnetic anomaly in real-time. This visually pinpoints the exact center of the pipe much better than reading a rapidly changing integer.
* **Target Depth Estimation:** With the addition of the 3rd (NEAR) sensor at 8 inches, implement a depth-inversion algorithm to calculate the physical depth of the target and display a live "Estimated Depth: XX inches" readout.

## 2. Connectivity & Web Portal
* **Over-The-Air (OTA) Updates:** Add an HTTP endpoint to web_server.cpp utilizing Update.h to allow uploading .bin compiled firmware directly from a phone or laptop browser, avoiding USB tethering.
* **QR Code Pairing:** Have the LVGL screen generate and display a QR code containing the wand's IP address. Users can scan it with their phone camera to instantly open the Web Portal without typing numbers.
* **Live WebSocket Streaming:** Implement WebSockets to stream the 400Hz telemetry live to a connected browser. This enables viewing high-resolution charts on a laptop in real-time during a scan.

## 3. Advanced Physics & Math
* **Kalman Filter for Auto-Tare:** Replace the simple Exponential Moving Average (EMA) auto-tare logic with a 1D Kalman filter to better isolate slow temperature drift from the slow approach of a deep magnetic target.
* **GPS Mapping:** Integrate a standard NMEA GPS module (using the freed IO43 and IO44 pins) to attach Lat/Lon coordinates to every row in the CSV log. This allows for post-processing the CSV into a visual heat-map overlay on Google Earth or QGIS.

## 4. Deep Search Mode (Software Oversampling)
**Concept:** Fight the inverse-square law to double the detection depth of the wand without requiring a hardware spin.
* **Physics:** Because magnetic fields decay rapidly, doubling the detection range requires reducing the noise floor by a factor of 4.
* **Implementation:** Apply a Rolling Software Average (Oversampling) to the raw hardware values before calculating the gradient. 
* **Benefit over Hardware CC:** Increasing the hardware Cycle Count (e.g., to CC=6400) would reduce the noise, but it would drop the UI refresh rate to 3 FPS (massive stutter). A software rolling average over 16 samples keeps the FreeRTOS loop and UI running at a buttery-smooth 50 FPS, it just introduces a slight phase delay (~300ms smear) requiring the user to sweep slower.
* **Proposed UI:** Instead of cluttering the Cycle Count button with 11 different mathematical combinations (which causes decision paralysis), split the UI into two logical toggles:
  1. **Hardware Speed:** [FAST: 200] -> [NORMAL: 400] -> [MAX: 800]
  2. **Deep Search (Averaging):** [OFF: 1x] -> [DEEP: 8x] -> [ULTRA: 16x]

## 5. Hardware Power Isolation (LDO Separation)
**Concept:** The ESP32-S3 and the LCD backlight create significant electrical noise on the 3.3V power rail, which ripples into the RM3100 analog measurement.
* **Implementation:** On the next PCB revision, split the analog and digital power planes. Provide a dedicated, ultra-low-noise LDO voltage regulator exclusively for the RM3100 I2C bus.
* **Benefit:** Shaves an estimated 10-20% off the noise floor with zero impact on UI speed or latency.

