# Magnetic Field Scanner - Bill of Materials (BOM)

## Core Electronics

| Part Description | Quantity | Link / Vendor | Notes |
| :--- | :---: | :--- | :--- |
| **Waveshare ESP32-S3-Touch-LCD-3.49 (v3 / PCBA 1.1)** | 1 | [Link Placeholder] | **CRITICAL INTEGRATED PLATFORM:** This is not just a display. It contains the ESP32-S3 MCU, QMI8658 IMU, TCA9554 I/O Expander, and Audio Codec physically built into the board. **Do not buy the IMU or Expander separately.** |
| **PNI RM3100 Magneto-Inductive Sensor Breakout** | 2 | [Link Placeholder] | One for the `TIP` (0") and one for the `REF` (24"). An optional 3rd sensor can be placed at the `NEAR` position (8"). |
| **Micro-SD Card (SPI)** | 1 | [Link Placeholder] | Used for high-speed logging. |

## Peripheral Components

| Part Description | Quantity | Link / Vendor | Notes |
| :--- | :---: | :--- | :--- |
| **2W 8 Ohm 1" Speaker** | 1 | [Link Placeholder] | Driven by the Waveshare's integrated audio codec via I2S. |
| **4.7K Ohm Resistor** | 2 | [Link Placeholder] | I2C pull-up resistors (if not utilizing the internal pull-ups). |
| **CAT6 Ethernet Cabling** | [Qty Placeholder] | [Link Placeholder] | Used for highly-shielded data and power transmission to the remote RM3100 sensors. Separate and different from the I2C bus wiring. |

## Mechanical & Jig Components

| Part Description | Quantity | Link / Vendor | Notes |
| :--- | :---: | :--- | :--- |
| **Fiberglass Tube (1" OD, 3/4" ID, 46.5" Long)** | [Qty Placeholder] | [Link Placeholder] | Acts as the rigid non-magnetic shaft of the wand. |
| **Fiberglass Rod (1/4" OD, 48" Long)** | [Qty Placeholder] | [Link Placeholder] | Internal structural reinforcement or wire-routing guide. |
| **3D Printed Parts (.stl)** | [Qty Placeholder] | Local 3D Printer | `Board_Holder_REF&NEAR_Part1_(1.75).stl`, `Board_Holder_REF&NEAR_Part2_(0.75).stl`, etc. found in `docs/mechanical/stl`. |

## Characterization Setup (Optional)

| Part Description | Quantity | Link / Vendor | Notes |
| :--- | :---: | :--- | :--- |
| **5/8" x 36" Steel Rebar** | 1 | Home Depot / Local | Used as a standard magnetic monopole target for open-air distance tests. |
| **Plywood Sheet & Wooden Guides** | 1 | Home Depot / Local | Used to build a magnetically-inert sliding jig for precision measurement testing. |
