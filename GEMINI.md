# Software Versioning Rule

All projects shall have a main Software version identifier as v[Major].[Minor].[Patch] and follow industry standard semver rules and practices for bumping the version (appropriately) at EVERY file iteration. 

We do not reuse version numbers for each iteration. This allows us to distinctly identify in the AI context the files we are working on.

# Python Scripting Standard

All Python scripts in the project must include a filename and version string at the top of the file, and must output this information to the console when executed.

**Note on Versioning:** The version of each Python script is **independent** of the main firmware project version in `user_config.h`. Python scripts follow their own semver rules and evolve separately (they are NOT in lockstep with the firmware version).

Example:
```python
import os

__filename__ = os.path.basename(__file__)
__version__ = "v1.1.1"

if __name__ == "__main__":
    print(f"=== {__filename__} {__version__} ===")
```

# Source Control & Git Consistency

AI Agents must explicitly ensure that all logical chunks of work and state changes are cleanly committed to git before ending a session or moving on to a completely new feature branch. Do not leave uncommitted architectural changes drifting in the working directory. Use clear, descriptive commit messages starting with the version bump if applicable (e.g., `v3.6.15: Fix NED mapping`).
