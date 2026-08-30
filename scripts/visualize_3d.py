import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import matplotlib.animation as animation
import argparse
import sys
import os

__filename__ = os.path.basename(__file__)
__version__ = "v1.0.1"

print(f"=== {__filename__} {__version__} ===")

parser = argparse.ArgumentParser(description='3D Wand Visualizer')
parser.add_argument('csv_file', help='Path to the log CSV file')
parser.add_argument('--speed', type=int, default=1, help='Playback speed multiplier')
parser.add_argument('--trail', type=float, default=5.0, help='Length of the trail in seconds (default: 5.0)')
args = parser.parse_args()

try:
    df = pd.read_csv(args.csv_file)
except Exception as e:
    print(f"Error loading {args.csv_file}: {e}")
    sys.exit(1)

df['time_sec'] = (df['time_ms'] - df['time_ms'].iloc[0]) / 1000.0
qw, qx, qy, qz = df['QW'], df['QX'], df['QY'], df['QZ']

# To find where the Wand Tip (+X) points in the Earth frame, we rotate (1,0,0) by the quaternion q (Sensor relative to Earth).
vec_x = 1.0 - 2.0 * (qy**2 + qz**2)
vec_y = 2.0 * (qx*qy + qw*qz)  
vec_z = 2.0 * (qx*qz - qw*qy)  

# Map NED (North, East, Down) to Matplotlib
# Earth North (+X) -> Plot Depth (+Y)
# Earth East (+Y) -> Plot Horizontal (+X)
# Earth Down (+Z) -> Plot Vertical (+Z, but inverted)
plot_x = vec_y
plot_y = vec_x
plot_z = vec_z  # Pure NED Z

fig = plt.figure(figsize=(10, 8))
ax = fig.add_subplot(111, projection='3d')
fig.canvas.manager.set_window_title('Magnetic Field Scanner - 3D Playback')

# Invert Z axis so negative (Sky) is at the top, positive (Dirt) is at the bottom
ax.invert_zaxis()

ax.set_xlim([-1.5, 1.5])
ax.set_ylim([-1.5, 1.5])
ax.set_zlim([-1.5, 1.5])
ax.set_xlabel('West / East')
ax.set_ylabel('South / North')
ax.set_zlabel('Down / Up')
ax.set_title(f'Wand 3D Playback')

xx, yy = np.meshgrid(np.linspace(-1.5, 1.5, 10), np.linspace(-1.5, 1.5, 10))
zz = np.zeros_like(xx) - 0.5
ax.plot_surface(xx, yy, zz, alpha=0.2, color='green')

ax.quiver(0, 0, -0.5, 0, 1.0, 0, color='red', arrow_length_ratio=0.2, linewidth=2)
ax.text(0, 1.2, -0.5, "NORTH", color='red', fontweight='bold')

trail_line, = ax.plot([], [], [], color='cyan', linewidth=2, alpha=0.6)
wand_line, = ax.plot([0, plot_x.iloc[0]], [0, plot_y.iloc[0]], [0, plot_z.iloc[0]], color='blue', linewidth=5)
time_text = ax.text2D(0.05, 0.95, '', transform=ax.transAxes, fontsize=12)

step = max(1, len(df) // (30 * int(df['time_sec'].iloc[-1]))) * args.speed

def update(frame):
    idx = frame * step
    if idx >= len(df): idx = len(df) - 1
    
    current_time = df['time_sec'].iloc[idx]
    start_time = max(0, current_time - args.trail)
    
    # Find the dataframe slice for the trail
    trail_mask = (df['time_sec'] >= start_time) & (df['time_sec'] <= current_time)
    trail_x = plot_x[trail_mask]
    trail_y = plot_y[trail_mask]
    trail_z = plot_z[trail_mask]
    
    trail_line.set_data(trail_x, trail_y)
    trail_line.set_3d_properties(trail_z)
    
    wand_line.set_data([0, plot_x.iloc[idx]], [0, plot_y.iloc[idx]])
    wand_line.set_3d_properties([0, plot_z.iloc[idx]])
    time_text.set_text(f"Time: {current_time:.1f}s")
    
    return wand_line, trail_line, time_text

ani = animation.FuncAnimation(fig, update, frames=len(df)//step, interval=33, blit=False)
plt.show()