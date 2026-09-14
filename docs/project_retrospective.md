# Magnetic Field Scanner: Phase 11 Retrospective (v5.1.x)

## Overview
This retrospective covers the collaborative process between the user and the AI agent during the v5.1.x firmware era, which focused on pushing the absolute physical limits of the RM3100 sensors, conquering I2C synchronization, and mapping physical magnetic decay laws to the Audio UI.

## The Good
* **The `cbrtf()` Audio Revelation:** The user identified that the standard logarithmic audio mapping felt "too squashed" as they approached targets. The AI realized that because a buried property pin acts as a magnetic dipole, its field decays according to the inverse-cube law (1/r^3). By implementing a mathematically pure `cbrtf()` (cube root) on the `nT` value, the audio pitch perfectly linearizes the physical distance (1/r) of the user walking toward the anomaly. This is a masterclass in fusing fundamental physics with UI design.
* **Synchronized `POLL` Architecture:** We successfully eradicated the internal silicon timer drift of the RM3100s. By abandoning Continuous Measurement Mode (CMM) and moving to a broadcasted `POLL` architecture, the Tip and Reference sensors are now mathematically forced to sample the physical world at the exact same microsecond, eliminating false Gradiometer noise and preventing I2C collisions.
* **Saturation Physics Discovered:** Through rigorous physical characterization (bringing an 800-pound pull magnet directly to the tip), we discovered that the RM3100 experiences two completely distinct failure modes: Physical Core Collapse (at ~533 µT, where the inductors physically saturate and fail to trigger interrupts) and Digital Integer Overflow (which limits the high-gain CC=3200 mode to ~88 µT). This deeply anchors our understanding of the sensor's dynamic range.
* **The Destructive Null Dip:** By carefully analyzing the 800 CC rebar logs, the user and AI discovered a ~3 µT inverted dip on the Reference Sensor. We deduced that the reference sensor sits in the negative return path of the large linear dipole (rebar). A massive win for our understanding of near-field gradiometry physics.

## The Bad
* **The CMM Register Lockout:** In the v5.1.1 transition to the new POLL architecture, the AI overlooked a critical hardware state-machine rule: writing to the RM3100's configuration registers (like Cycle Count) silently fails if the sensor is still actively running in Continuous Measurement Mode from a previous boot. This caused the Tip sensor to output gibberish physics on hot-flashes until we explicitly forced the sensor into IDLE mode before configuration.

## The Ugly
* **The Hardcoded Audio Bypass:** The user carefully configured `audio_max_freq` in `settings.json` only to find the UI completely ignoring it and capping out at 3000Hz. The AI discovered that an old piece of legacy code was hardcoding the audio limits directly inside the `.ino` file, bypassing the `settings_manager` entirely. A painful reminder to never leave magic numbers scattered in the firmware.
* **The 480ms Core Collapse Timeout:** When the RM3100 physical core collapses at >533 µT, the hardware permanently locks up and stops sending `DRDY` interrupts. We realized that relying on a manual sweep of the magnet was futile because the 160ms/480ms FreeRTOS timeout resets completely ruined the spatial mapping of the log. 

## Takeaways for Future Sessions
1. **Physics Dictates UI:** Whether it is applying a cube root to audio mapping to counteract 1/r^3 decay, or applying a Kabsch algorithm to counteract an 9.8-degree physical carrier bend, the software is only as good as its adherence to the physical universe.
2. **Always Idle Sensors Before Configuration:** Never trust the power-on-reset state of a sensor on a hot-flash. Explicitly force IDLE states before writing to control registers.
3. **Use Jigs for Characterization:** Humans cannot smoothly move a magnet at 1mm increments while the firmware is actively fighting 480ms hardware timeouts. Future saturation testing must use a threaded-rod jig for repeatable, non-temporal 1/r mapping.
