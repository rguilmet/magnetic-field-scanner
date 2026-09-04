import re

with open("docs/hyper-context-anchor.md", "r", encoding="utf-8") as f:
    doc = f.read()

old_rule = "* **NEVER** write a loop that assumes the RM3100 `DRDY` pin acts exclusively as a RISING edge. If the ESP32 is blocked by an SD card flush, the sensor will lap the CPU, leaving `DRDY` permanently `HIGH`. Always perform a manual `digitalRead()` poll or a dummy `REG_RESULTS` read to break the lockup."
new_rule = "* **NEVER** place the RM3100 in Continuous Measurement Mode (CMM). If the ESP32 is blocked by an SD card flush or UI rendering, the sensor will lap the CPU, causing an I2C phase collision and a permanent `DRDY` lockup. Always operate exclusively in POLL mode via `REG_POLL` to guarantee deterministic phase synchronization."

doc = doc.replace(old_rule, new_rule)

with open("docs/hyper-context-anchor.md", "w", encoding="utf-8") as f:
    f.write(doc)
