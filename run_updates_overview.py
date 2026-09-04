import re

with open("docs/project_overview.md", "r", encoding="utf-8") as f:
    doc = f.read()

doc = doc.replace("DRDY_MID", "DRDY_NEAR")
doc = doc.replace("RM3100 Mid (Future): `0x22` (DRDY: `GPIO 5`)", "RM3100 Near (Future): `0x22` (DRDY: `GPIO 5`)")
doc = doc.replace("1. **Third Sensor Integration (Mid):** \n   * A third RM3100 sensor will be mounted in the middle of the wand.",
                  "1. **Third Sensor Integration (Near):** \n   * A third RM3100 sensor will be mounted at 8 inches from the TIP.")

# Fix Auto-Tare section
doc = doc.replace("jumps by less than 50 counts", "jumps by less than 150 nT")

# Completely replace Cycle Count Scaling section
old_scaling = """### E. Cycle Count Scaling
* **The Physics:** When the user changes the Cycle Count (CC) on the fly, the RM3100's raw readings scale linearly. (e.g., CC=200 gives a radius of ~3,800, CC=400 gives ~7,600, CC=800 gives ~15,400).
* **The Code:** The `cal_config.tip_hard` and `ref_hard` matrices (Center Offsets) MUST be scaled linearly in code when CC changes. The Soft-Iron matrices (W) DO NOT scale, as they represent dimensional shape ratios."""

new_scaling = """### E. Universal Cycle Count Scaling (v5.x.x)
* **The Physics:** When the user changes the Cycle Count (CC) on the fly, the RM3100's raw output scales non-linearly due to Zero-Field Offsets (ZFO).
* **The Code (Pre-Normalization):** To eliminate drift and scaling errors, the `v5.0.0+` architecture intercepts raw LSB counts and immediately normalizes them to physical nanoTeslas (`nT`) using the exact `CC` gain scalar *before* any calibration is applied.
* **Universal Matrices:** Because the Kabsch calibration algorithm now operates entirely in the physical `nT` domain, the resulting Hard and Soft Iron matrices are mathematically dimensionless. You can calibrate the wand at 400 CC, and perfectly seamlessly jump to 3200 CC without re-calibrating or scaling the matrices!"""

doc = doc.replace(old_scaling, new_scaling)

with open("docs/project_overview.md", "w", encoding="utf-8") as f:
    f.write(doc)
