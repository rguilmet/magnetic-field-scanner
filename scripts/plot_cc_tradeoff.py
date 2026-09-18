import matplotlib.pyplot as plt

cc = [200, 400, 800, 1600, 3200]
# From our script, we know the exact values:
# Note: we ignore the slight bottleneck on CC=200 since CC=400 is faster
# Actually, let's just plot the empirical data exactly as it was logged
noise_nt = [194.35, 187.50, 130.29, 69.41, 30.22]
sample_rate_hz = [33.82, 43.52, 27.66, 16.26, 9.23]

fig, ax1 = plt.subplots(figsize=(10, 6))

color1 = 'tab:red'
ax1.set_xlabel('Cycle Count (CC)', fontsize=12, fontweight='bold')
ax1.set_ylabel('RMS Noise Floor (nT)', color=color1, fontsize=12, fontweight='bold')
ax1.plot(cc, noise_nt, color=color1, marker='o', linewidth=3, markersize=8, label='Noise (Lower is Better)')
ax1.tick_params(axis='y', labelcolor=color1)
ax1.set_xticks(cc)

ax2 = ax1.twinx()
color2 = 'tab:blue'
ax2.set_ylabel('Sample Rate (Hz)', color=color2, fontsize=12, fontweight='bold')
ax2.plot(cc, sample_rate_hz, color=color2, marker='s', linewidth=3, markersize=8, linestyle='--', label='Bandwidth (Higher is Faster)')
ax2.tick_params(axis='y', labelcolor=color2)

plt.title('The "Diminishing Returns" Curve: Bandwidth vs. Resolution', fontsize=14, fontweight='bold')

# Annotate the "Sweet Spots"
ax1.annotate('Sweet Spot for Manual Sweeping', 
             xy=(1600, 69.41), xytext=(1200, 120),
             arrowprops=dict(facecolor='black', shrink=0.05, width=1.5, headwidth=8),
             fontsize=10, fontweight='bold', bbox=dict(boxstyle="round,pad=0.3", fc="yellow", ec="black", lw=1, alpha=0.8))

ax1.annotate('Deep Pipe Detection', 
             xy=(3200, 30.22), xytext=(2400, 80),
             arrowprops=dict(facecolor='black', shrink=0.05, width=1.5, headwidth=8),
             fontsize=10, fontweight='bold', bbox=dict(boxstyle="round,pad=0.3", fc="yellow", ec="black", lw=1, alpha=0.8))

fig.tight_layout()
plt.grid(True, alpha=0.3)
plt.savefig(r'docs\reports\cc_tradeoff_curve.png', dpi=150)
print("Saved plot to docs/reports/cc_tradeoff_curve.png")
