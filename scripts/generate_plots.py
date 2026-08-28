#!/usr/bin/env python3
"""
Magnetic Field Scanner - 3D Plot Generator
Version: v1.0.0

Specialized utility for visualizing 3D ellipsoid point clouds to verify the 
mathematical perfection of the sphere-fitting algorithms from raw calibration data.
"""

import argparse
import pandas as pd
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d import Axes3D
import numpy as np
import os
import sys

def generate_plot(input_file, output_file, show_plot):
    # Windows path bulletproofing: strip accidental terminal quotes and normalize slashes
    input_file = os.path.normpath(input_file.strip('\'"'))
    
    if not os.path.exists(input_file):
        print(f"Error: Could not find '{input_file}'.")
        sys.exit(1)

    print(f"Generating 3D point cloud visualization for {input_file}...")
    df = pd.read_csv(input_file)
    
    # Support both old and new CSV header formats for backwards compatibility
    if 'refX_raw' in df.columns:
        x_r, y_r, z_r = df['refX_raw'], df['refY_raw'], df['refZ_raw']
        x_t, y_t, z_t = df['tipX_raw'], df['tipY_raw'], df['tipZ_raw']
    else:
        x_r, y_r, z_r = df['Ref_X_Raw'], df['Ref_Y_Raw'], df['Ref_Z_Raw']
        x_t, y_t, z_t = df['Tip_X_Raw'], df['Tip_Y_Raw'], df['Tip_Z_Raw']

    fig = plt.figure(figsize=(14, 6))

    # Reference Sensor Subplot
    ax1 = fig.add_subplot(121, projection='3d')
    ax1.scatter(x_r, y_r, z_r, c='b', marker='.', alpha=0.5)
    ax1.set_title("Reference Sensor (Raw Data)")
    ax1.set_xlabel("X")
    ax1.set_ylabel("Y")
    ax1.set_zlabel("Z")
    
    # Ensure equal aspect ratio visually for Ref
    max_range = np.array([x_r.max()-x_r.min(), y_r.max()-y_r.min(), z_r.max()-z_r.min()]).max() / 2.0
    mid_x = (x_r.max()+x_r.min()) * 0.5
    mid_y = (y_r.max()+y_r.min()) * 0.5
    mid_z = (z_r.max()+z_r.min()) * 0.5
    ax1.set_xlim(mid_x - max_range, mid_x + max_range)
    ax1.set_ylim(mid_y - max_range, mid_y + max_range)
    ax1.set_zlim(mid_z - max_range, mid_z + max_range)

    # Tip Sensor Subplot
    ax2 = fig.add_subplot(122, projection='3d')
    ax2.scatter(x_t, y_t, z_t, c='r', marker='.', alpha=0.5)
    ax2.set_title("Tip Sensor (Raw Data)")
    ax2.set_xlabel("X")
    ax2.set_ylabel("Y")
    ax2.set_zlabel("Z")
    
    # Ensure equal aspect ratio visually for Tip
    max_range = np.array([x_t.max()-x_t.min(), y_t.max()-y_t.min(), z_t.max()-z_t.min()]).max() / 2.0
    mid_x = (x_t.max()+x_t.min()) * 0.5
    mid_y = (y_t.max()+y_t.min()) * 0.5
    mid_z = (z_t.max()+z_t.min()) * 0.5
    ax2.set_xlim(mid_x - max_range, mid_x + max_range)
    ax2.set_ylim(mid_y - max_range, mid_y + max_range)
    ax2.set_zlim(mid_z - max_range, mid_z + max_range)

    plt.tight_layout()
    
    if output_file:
        plt.savefig(output_file, dpi=150)
        print(f"Success! Plot saved to {output_file}")
        
    if show_plot:
        plt.show()

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Wand 3D Point Cloud Plot Generator (v3.0.32)")
    parser.add_argument("-i", "--input", required=True, help="Input calibration.csv file")
    parser.add_argument("-o", "--output", default="calibration_plot.png", help="Output image file (default: calibration_plot.png)")
    parser.add_argument("--show", action="store_true", help="Display the plot interactively in a window")
    
    args = parser.parse_args()
    generate_plot(args.input, args.output, args.show)
