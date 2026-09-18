import numpy as np
import matplotlib.pyplot as plt

# Empirical parameters
B0_nT = 457230  # 457.23 uT max spike converted to nT (from CC=1600 data)
r0_inches = 2.0  # Assumed distance from sensor die to exterior of enclosure/rebar contact

distances_inches = np.linspace(r0_inches, 120, 500) # 2 inches to 10 feet
distances_feet = distances_inches / 12.0

# 1/r^2 Magnetic Monopole Falloff
# B(r) = B0 * (r0 / r)^2
B_r_nT = B0_nT * (r0_inches / distances_inches)**2

# 1/r^3 Magnetic Dipole Falloff (for comparison)
# B(r) = B0 * (r0 / r)^3
B_r_dipole_nT = B0_nT * (r0_inches / distances_inches)**3

fig, ax = plt.subplots(figsize=(10, 6))

ax.plot(distances_feet, B_r_nT, 'b-', linewidth=3, label='Vertical Pin (Monopole $1/r^2$)')
ax.plot(distances_feet, B_r_dipole_nT, 'g--', linewidth=2, label='Horizontal Pipe (Dipole $1/r^3$)')

# Threshold line (250 nT)
threshold_nT = 250
ax.axhline(y=threshold_nT, color='r', linestyle=':', linewidth=2, label='Detection Threshold (250 nT)')

ax.set_yscale('log')
ax.set_ylim(10, 1000000)
ax.set_xlim(0, 10)

ax.set_xlabel('Depth / Distance from Sensor (Feet)', fontsize=12, fontweight='bold')
ax.set_ylabel('Magnetic Field Disturbance (nT) [Log Scale]', fontsize=12, fontweight='bold')
ax.set_title('Theoretical Detection Range based on Empirical CC=1600 Spike (457 µT)', fontsize=14, fontweight='bold')

# Find intersection point for Monopole
monopole_max_depth = 2.0 / np.sqrt(threshold_nT / B0_nT) / 12.0 # in feet
ax.plot(monopole_max_depth, threshold_nT, 'bo', markersize=8)
ax.annotate(f'Max Depth: {monopole_max_depth:.1f} ft', 
            xy=(monopole_max_depth, threshold_nT), xytext=(monopole_max_depth+0.5, threshold_nT*2),
            arrowprops=dict(facecolor='black', shrink=0.05, width=1),
            fontsize=10, fontweight='bold')

# Find intersection point for Dipole
dipole_max_depth = 2.0 / (threshold_nT / B0_nT)**(1/3.0) / 12.0 # in feet
ax.plot(dipole_max_depth, threshold_nT, 'go', markersize=8)
ax.annotate(f'Max Depth: {dipole_max_depth:.1f} ft', 
            xy=(dipole_max_depth, threshold_nT), xytext=(dipole_max_depth+0.5, threshold_nT/3),
            arrowprops=dict(facecolor='black', shrink=0.05, width=1),
            fontsize=10, fontweight='bold')

ax.legend(loc='upper right')
ax.grid(True, which="both", ls="--", alpha=0.5)

plt.tight_layout()
plt.savefig(r'docs\reports\detection_depth_curve.png', dpi=150)
print("Saved detection depth plot")
