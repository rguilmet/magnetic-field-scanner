# Session Handoff & Context

## Current Phase: Phase 10 - Unified nanoTesla (nT) Architecture Planning (v5.0.0)
We successfully completed testing of the **v4.0.0** RTOS ISR architecture. However, during testing, the user discovered two critical physical/mathematical flaws in the old raw-count architecture:

1. **The nT Consistency Bug:** The original firmware processes all magnetic data in raw integer counts and relies on dynamic scaling when the user changes Cycle Counts. Because the RM3100 hardware's Zero-Field Offset (ZFO) is non-linear, and because `calibrate_wand.py` didn't save the Cycle Count it was recorded at, changing the Cycle Count in the UI caused the physical `nT` output to drift (or wildly swing) instead of remaining perfectly constant for a static magnetic field.
2. **The ISR Lockup / Lapping Bug:** The user discovered that the random "dead wand" lockups requiring a power cycle are caused by the SD card SPI flush blocking for 20ms. The sensor runs at 6.7ms and laps the blocked ESP32 twice, leaving the `DRDY` pin permanently `HIGH`. Because the firmware waits for a `RISING` edge interrupt, the ISR never fires again, and the watchdog's blind I2C rewrite fails to pull the `DRDY` pin back to `LOW`.

## Where We Left Off
We have completely diagnosed the physical mechanisms behind these bugs and formulated a comprehensive, mathematically sound plan to rewrite the architecture to natively use physical `nT`. This will decouple the calibration matrix from the hardware gain.

The detailed, approved execution plan is saved in the Antigravity workspace artifact `implementation_plan.md`. 
**The `v4.0.0` code has been fully committed to Git, leaving a perfectly clean working directory.**

## Next Steps for the Next Agent
1. **Execute the Plan:** The user is ready to pull the trigger on `v5.0.0`. Follow the `implementation_plan.md` artifact to refactor `SensorFusion.cpp`, `Magnetic_Field_Scanner.ino`, `lvgl_port.c`, and all Python scripts in `/scripts` to the new `nT` architecture.
2. **Update Documentation:** Per the user's specific request, once the code is refactored, you MUST update `project_overview.md` and `user_manual.md` to reflect the `v5.0.0` architecture, and commit those documentation changes to Git.

### 1. Battery Power Keep-Alive Latch (SYS_EN)
When powering on the ESP32 via the physical battery button, the PMIC requires the ESP32 to assert the `SYS_EN` pin HIGH via the TCA9554 IO Expander to latch power. (Already implemented in firmware, leaving this note as a reminder not to move I2C init below Serial waits).
