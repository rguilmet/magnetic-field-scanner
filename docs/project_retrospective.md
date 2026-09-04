# Magnetic Field Scanner: Phase 9 & 10 Retrospective

## Overview
This retrospective covers the collaborative process between the user and the AI agent during the transition from the legacy software-polling architecture to the v4.0.0 Zero-Latency ISR architecture, and the subsequent planning for the v5.0.0 Unified `nT` Architecture. 

## The Good
* **Exceptional Hardware Diagnosis:** The collaboration truly shined when diagnosing physical hardware limitations. The user's brilliant deduction that the SD Card SPI flush (20ms) was causing the RM3100 (running at 6.7ms) to "lap" the ESP32 and permanently lock the `DRDY` pin `HIGH` was a masterclass in embedded systems debugging.
* **Architectural Leap:** We successfully identified that the legacy 50Hz software polling loop was fundamentally bottlenecking the system. Moving to FreeRTOS Event Groups and hardware interrupts unlocked 150Hz performance and zero-CPU idle states.
* **Physical First Principles:** Recognizing that "raw counts" were a flawed basis for the mathematical calibration matrix (due to Cycle Count scaling and non-linear Zero-Field Offsets) led to the robust, elegant v5.0.0 plan to convert everything to pure physical `nT` natively at the I2C bus level.
* **Collaborative Ping-Pong:** The iterative back-and-forth allowed us to refine features rapidly, such as deciding to log the IMU temperature to track RM3100 thermal oscillator drift.

## The Bad
* **Time-Dilation Blind Spot:** In the initial firmware design, we failed to account for the fact that changing hardware Cycle Counts inherently changes the sensor's frequency. This caused the Madgwick filter to suffer from massive time-dilation distortion (drifting pitch/roll) until we finally implemented a dynamic `dt` tracker using `micros()`.
* **Cable Capacitance Oversight:** The agent initially proposed cranking the I2C bus to 400KHz to buy more margin against the `DRDY` lapping issue, temporarily forgetting the physics of the 60-inch CAT6 cable. The user correctly caught that the RC time constant of a 5-foot cable would cause severe data corruption at 400KHz, leading us to safely settle on 200KHz.

## The Ugly
* **The Silent Bricking:** The way the FreeRTOS `RISING` edge interrupt interacted with the RM3100's Continuous Measurement Mode was brutal. Because the watchdog blindly reset the I2C registers without performing a dummy read to force `DRDY` `LOW`, the wand would infinitely print "Sensor lockup detected!" and require a hard power cycle. Untangling this required tracing the exact state machine of the silicon die.
* **Git Discipline:** The agent completed the massive v4.0.0 ISR refactor but failed to immediately commit the changes to Git, temporarily violating the project's strict version control rules. It took a manual prompt from the user to realize the working directory was dirty before ending the session.

## Takeaways for Future Sessions
1. **Always Verify Physical Hardware Constraints:** Whether it's cable capacitance, SD card SPI blocking times, or thermal drift, software cannot be written in a vacuum.
2. **Commit Early, Commit Often:** The AI agents must strictly adhere to the rule of checkpointing logical chunks of work immediately to prevent orphaned code.
3. **Trust the User's Intuition:** When the user suspects a timing overlap or a hardware lapping issue, they are usually right. Follow their lead into the datasheet.
