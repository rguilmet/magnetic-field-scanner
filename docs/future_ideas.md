# Magnetic Field Scanner - Future Ideas & Roadmap

This document captures brainstormed ideas for increasing the usability, capability, and professional feel of the MFS wand for Phase 5 and beyond.

## 1. The Screen & UI Experience
* **Scrolling "Seismograph" Strip Chart:** Add a 4th LVGL tile containing an lv_chart. As the user walks over a buried pipe, it will draw the "bell curve" of the magnetic anomaly in real-time. This visually pinpoints the exact center of the pipe much better than reading a rapidly changing integer.
* **Target Depth Estimation:** With the addition of the 3rd (NEAR) sensor at 8 inches, implement a depth-inversion algorithm to calculate the physical depth of the target and display a live "Estimated Depth: XX inches" readout.
* **Audio "Geiger Counter" Mode:** Pitch-bending is highly sensitive, but continuous tones can cause fatigue. Add a toggle for a "Geiger counter" mode, where the proximity to the pipe is represented by the frequency of clicks.

## 2. Connectivity & Web Portal
* **Over-The-Air (OTA) Updates:** Add an HTTP endpoint to web_server.cpp utilizing Update.h to allow uploading .bin compiled firmware directly from a phone or laptop browser, avoiding USB tethering.
* **QR Code Pairing:** Have the LVGL screen generate and display a QR code containing the wand's IP address. Users can scan it with their phone camera to instantly open the Web Portal without typing numbers.
* **Live WebSocket Streaming:** Implement WebSockets to stream the 400Hz telemetry live to a connected browser. This enables viewing high-resolution charts on a laptop in real-time during a scan.

## 3. Advanced Physics & Math
* **Quaternion Sensor Fusion (AHRS):** Fuse the QMI8658 accelerometer and gyroscope data using a Madgwick or Mahony filter to generate a 3D Quaternion. Use this to instantly and computationally-cheaply de-rotate the RM3100 vectors back to a "flat Earth" reference grid, making the wand completely immune to hand-twisting and Gimbal Lock.
* **Thermal Drift Compensation:** Use the QMI8658's built-in thermometer to track environmental temperature changes. Apply a mathematical correction curve to the RM3100 readings to prevent the zero-baseline from drifting on hot days.
* **Kalman Filter for Auto-Tare:** Replace the simple Exponential Moving Average (EMA) auto-tare logic with a 1D Kalman filter to better isolate slow temperature drift from the slow approach of a deep magnetic target.
* **GPS Mapping:** Integrate a standard NMEA GPS module (using the freed IO43 and IO44 pins) to attach Lat/Lon coordinates to every row in the CSV log. This allows for post-processing the CSV into a visual heat-map overlay on Google Earth or QGIS.