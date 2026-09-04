#!/usr/bin/env python3
"""
Magnetic Field Scanner - Calibration Analyzer
Generates 3D plots of raw vs calibrated magnetometer data,
plus IMU and Euler angle time-series.
"""

import argparse
import pandas as pd
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d import Axes3D
import os
import sys

__filename__ = os.path.basename(__file__)
__version__ = "v1.0.1"

print(f"=== {__filename__} {__version__} ===")

def main():
    parser = argparse.ArgumentParser(description="Calibration Plot Generator")
    parser.add_argument("-i", "--input", required=True, help="Input calibration CSV file")
    parser.add_argument("-o", "--outdir", default="plots", help="Output directory for generated plots")
    args = parser.parse_args()

    input_file = os.path.normpath(args.input.strip('\'"'))
    out_dir = os.path.normpath(args.outdir.strip('\'"'))

    if not os.path.exists(input_file):
        print(f"Error: Could not find '{input_file}'.")
        sys.exit(1)

    os.makedirs(out_dir, exist_ok=True)
    print(f"Analyzing {input_file}...")
    df = pd.read_csv(input_file)

    # 1. 3D Scatter of Raw Magnetometer Data
    fig = plt.figure(figsize=(10, 8))
    ax = fig.add_subplot(111, projection='3d')
    if 'refX_raw' in df.columns:
        ax.scatter(df['refX_raw'], df['refY_raw'], df['refZ_raw'], label='Reference Raw', alpha=0.5, s=2)
        ax.scatter(df['tipX_raw'], df['tipY_raw'], df['tipZ_raw'], label='Tip Raw', alpha=0.5, s=2)
    ax.set_title('Raw Magnetometer Data (Ellipsoids)')
    ax.set_xlabel('X')
    ax.set_ylabel('Y')
    ax.set_zlabel('Z')
    ax.legend()
    raw_path = os.path.join(out_dir, 'mag_raw_3d.png')
    plt.savefig(raw_path, bbox_inches='tight')
    plt.close()
    print(f"Saved: {raw_path}")

    # 2. 3D Scatter of Calibrated Magnetometer Data
    fig = plt.figure(figsize=(10, 8))
    ax = fig.add_subplot(111, projection='3d')
    if 'refX_cal' in df.columns:
        ax.scatter(df['refX_cal'], df['refY_cal'], df['refZ_cal'], label='Reference Calibrated', alpha=0.5, s=2)
        ax.scatter(df['tipX_cal'], df['tipY_cal'], df['tipZ_cal'], label='Tip Calibrated', alpha=0.5, s=2)
    ax.set_title('Calibrated Magnetometer Data (Spheres)')
    ax.set_xlabel('X')
    ax.set_ylabel('Y')
    ax.set_zlabel('Z')
    ax.legend()
    cal_path = os.path.join(out_dir, 'mag_cal_3d.png')
    plt.savefig(cal_path, bbox_inches='tight')
    plt.close()
    print(f"Saved: {cal_path}")

    # 3. IMU Data Over Time
    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(12, 8), sharex=True)
    time_sec = (df['time_ms'] - df['time_ms'].iloc[0]) / 1000.0 if 'time_ms' in df.columns else df.index
    ax1.plot(time_sec, df['accX'], label='Acc X')
    ax1.plot(time_sec, df['accY'], label='Acc Y')
    ax1.plot(time_sec, df['accZ'], label='Acc Z')
    ax1.set_ylabel('Acceleration (g)')
    ax1.legend()
    ax1.set_title('Accelerometer over Time')

    ax2.plot(time_sec, df['gyrX'], label='Gyr X')
    ax2.plot(time_sec, df['gyrY'], label='Gyr Y')
    ax2.plot(time_sec, df['gyrZ'], label='Gyr Z')
    ax2.set_ylabel('Gyro (dps)')
    ax2.set_xlabel('Time (s)')
    ax2.legend()
    imu_path = os.path.join(out_dir, 'imu_time.png')
    plt.savefig(imu_path, bbox_inches='tight')
    plt.close()
    print(f"Saved: {imu_path}")

    # 4. Euler Angles
    if 'Azimuth' in df.columns and 'Elevation' in df.columns:
        plt.figure(figsize=(12, 4))
        plt.plot(time_sec, df['Azimuth'], label='Azimuth')
        plt.plot(time_sec, df['Elevation'], label='Elevation')
        plt.xlabel('Time (s)')
        plt.ylabel('Angle (deg)')
        plt.title('Euler Angles Over Time')
        plt.legend()
        euler_path = os.path.join(out_dir, 'euler_time.png')
        plt.savefig(euler_path, bbox_inches='tight')
        plt.close()
        print(f"Saved: {euler_path}")
        
    print("Analysis complete!")

if __name__ == '__main__':
    main()
