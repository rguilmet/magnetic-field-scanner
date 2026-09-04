# Final Launch Checklist: Hackaday & Open Source Release

This checklist is a private roadmap for crossing the finish line and officially publishing the Magnetic Field Scanner to the Hackaday community.

## 1. Physical Jig Construction & Data Capture
- [ ] **Build the Jig:** Assemble the plywood, double-sided tape, wood rails, and the sliding blocks in the magnetically-sterile "reclaim driveway".
- [ ] **Capture CC Sweeps:** Run the formal characterization sweeps (Noise Floor, Repeatability, Golden Target saturation) at CC=200, CC=400, and CC=3200.
- [ ] **Run the Python Analyzer:** Execute `scripts/characterize_system.py` on the collected `.csv` logs to automatically generate the final `docs/characterization.md` datasheet.
- [ ] **The "Tape Measure" Test:** Purchase the 5/8" x 36" rebar. Lay a tape measure on the driveway and slowly slide the rebar toward the wand to empirically record the maximum open-air detection distance.
- [ ] **The "Real World" Test:** Sweep the known buried property pins in the yard to validate the physics in dirt/concrete.

## 2. Photography & Media (The "Hackaday Gold")
*Hackaday projects live and die by their visual documentation.*
- [ ] **The Hero Shot:** A high-quality, well-lit photo of the fully assembled, powered-on wand.
- [ ] **The "Mad Scientist" Shot:** Photos of the wooden characterization jig setup in the driveway. This proves extreme engineering rigor to the community.
- [ ] **The Tape Measure Shot:** A photo showing the wand detecting the 36" rebar at distance alongside a tape measure.
- [ ] **The Action Video:** A short video (or GIF) showing the LVGL UI arc dynamically changing as you sweep over a buried property pin. Include the audio!
- [ ] **The Auto-Tare Video:** A 15-second clip showing the Auto-Tare algorithm slowly "eating away" a static magnetic anomaly (like standing next to a car).

## 3. Repository Final Polish
- [ ] **BOM Quantities & Links:** Fill in the `[Qty Placeholder]` and `[Link Placeholder]` entries in `docs/project_bom.md` with the final counts and Amazon/DigiKey URLs.
- [ ] **Verify CAD Files:** Ensure the `.stl` files in `docs/mechanical/stl` exactly match your final physical build (especially the new 8" NEAR sensor mounts).
- [ ] **Clean Up SD Card:** Delete any lingering dummy/test logs from the wand's SD card before taking final screenshots of the Web Server.

## 4. Hackaday Project Page Construction
- [ ] **Create the Project Hub:** Initialize the project on Hackaday.io.
- [ ] **Link the GitHub:** Add the `rguilmet/magnetic-field-scanner` repository URL to the sidebar.
- [ ] **Write Project Logs:** (Optional but highly recommended) Write 2 or 3 short "Project Log" entries. Hackaday readers love the story of development. Suggested topics:
  - *Slaying the I2C Lockup Dragon:* How we abandoned CMM for deterministic `POLL` mode to guarantee sensor phase-sync.
  - *Mathematical Breakthroughs:* Moving the Kabsch calibration algorithm from raw integer counts into the physical `nT` domain to achieve a universal, dimensionless matrix.
  - *The Custom Jig:* Explaining how you characterized the hardware limits.
- [ ] **Publish!** 🚀
