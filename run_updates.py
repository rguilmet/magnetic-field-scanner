import re
import os

# 1. Update user_config.h
with open("user_config.h", "r") as f:
    cfg = f.read()
cfg = cfg.replace("MFS_PIN_RM3100_MID_DRDY", "MFS_PIN_RM3100_NEAR_DRDY")
cfg = re.sub(r'(#define\s+MFS_FIRMWARE_VERSION\s+)"v5\.1\.1"', r'\1"v5.1.2"', cfg)
with open("user_config.h", "w") as f:
    f.write(cfg)

# 2. Update Magnetic_Field_Scanner.ino
with open("Magnetic_Field_Scanner.ino", "r") as f:
    ino = f.read()
ino = ino.replace("ADDR_MID", "ADDR_NEAR")
ino = ino.replace("MFS_PIN_RM3100_MID_DRDY", "MFS_PIN_RM3100_NEAR_DRDY")
with open("Magnetic_Field_Scanner.ino", "w") as f:
    f.write(ino)

# 3. Update README.md
with open("README.md", "r", encoding="utf-8") as f:
    rmd = f.read()
rmd = rmd.replace("MID: 5", "NEAR: 5")
rmd = rmd.replace("3rd RM3100 (MID)", "3rd RM3100 (NEAR)")
rmd = rmd.replace("3rd RM3100 sensor (MID)", "3rd RM3100 sensor (NEAR)")
rmd = rmd.replace("rather than exactly in the middle.", "rather than exactly in the middle (i.e. placed at 8 inches from the TIP).")
with open("README.md", "w", encoding="utf-8") as f:
    f.write(rmd)
