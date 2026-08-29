#!/usr/bin/env python3
"""
Magnetic Field Scanner - Calibration Tool
Version: v1.1.0

Ingests raw calibration.csv datasets, computes the 3x3 Soft-Iron matrices (W) 
and Hard-Iron center offsets (V), applies Kabsch rotational alignment, and 
outputs a drop-in calibration.json file.
"""

import numpy as np
import pandas as pd
import json
import argparse
import os
import time
import matplotlib.pyplot as plt

def get_calibration_matrices(x, y, z):
    """
    Fits an ellipsoid to 3D spatial data using the Least-Squares method.
    Returns the Hard-Iron Offset (Center), Soft-Iron Matrix, and the calculated radii.
    """
    # Normalize coordinates to improve numerical stability during the fit
    x_mean, y_mean, z_mean = np.mean(x), np.mean(y), np.mean(z)
    xn, yn, zn = x - x_mean, y - y_mean, z - z_mean
    
    # Ellipsoid algebraic equation: Ax^2 + By^2 + Cz^2 + 2Dxy + 2Exz + 2Fyz + 2Gx + 2Hy + 2Iz = 1
    D = np.array([xn*xn, yn*yn, zn*zn, 2*xn*yn, 2*xn*zn, 2*yn*zn, 2*xn, 2*yn, 2*zn]).T
    v, _, _, _ = np.linalg.lstsq(D, np.ones_like(xn), rcond=None)
    
    # Form the 4x4 algebraic matrix
    A = np.array([[v[0], v[3], v[4], v[6]],
                  [v[3], v[1], v[5], v[7]],
                  [v[4], v[5], v[2], v[8]],
                  [v[6], v[7], v[8], -1]])
    
    # Solve for the center of the ellipsoid (Hard-Iron Offset)
    center_n = np.linalg.solve(-A[:3, :3], v[6:9])
    
    # Translate the matrix to the center to extract the soft-iron matrix
    T = np.eye(4)
    T[3, :3] = center_n
    R = T.dot(A).dot(T.T)
    
    evals, evecs = np.linalg.eigh(R[:3, :3] / -R[3, 3])
    radii = 1.0 / np.sqrt(np.abs(evals))
    target_radius = np.mean(radii)
    
    # The soft-iron matrix rotates, scales the axes to a perfect sphere, and rotates back
    soft_iron = evecs.dot(np.diag(target_radius / radii)).dot(evecs.T)
    
    # Denormalize center to map back to raw RM3100 values
    center = center_n + np.array([x_mean, y_mean, z_mean])
    
    return center, soft_iron, radii, target_radius

def kabsch_alignment(P, Q):
    """
    Calculates the optimal rotation matrix to perfectly align 3D point cloud P onto Q.
    """
    H = P.T @ Q
    U, S, Vt = np.linalg.svd(H)
    R = Vt.T @ U.T
    
    # Ensure a proper rotation (prevent mathematical reflections)
    if np.linalg.det(R) < 0:
        Vt[2,:] *= -1
        R = Vt.T @ U.T
    return R

def assess_coverage(sensor_name, x, y, z, ideal_radius):
    """
    Assesses how much of the spherical bounding box the user successfully covered.
    """
    ideal_diameter = ideal_radius * 2
    x_cov = (np.max(x) - np.min(x)) / ideal_diameter * 100
    y_cov = (np.max(y) - np.min(y)) / ideal_diameter * 100
    z_cov = (np.max(z) - np.min(z)) / ideal_diameter * 100
    
    print(f"\n--- {sensor_name} Coverage Assessment ---")
    print(f"Ideal Magnetic Radius: {ideal_radius:.0f} counts")
    print(f"X-Axis Coverage: {x_cov:.1f}%")
    print(f"Y-Axis Coverage: {y_cov:.1f}%")
    print(f"Z-Axis Coverage: {z_cov:.1f}%")
    
    if min(x_cov, y_cov, z_cov) < 40.0:
        print("Warning: One of your axes has less than 40% coverage.")
        print("The algorithm can extrapolate it, but try tumbling the wand more next time!")

def plot_calibration(ref_raw, tip_raw, ref_cal, tip_cal):
    """
    Generates a 3D matplotlib plot showing the Before and After calibration states.
    """
    fig = plt.figure(figsize=(12, 6))
    
    ax1 = fig.add_subplot(121, projection='3d')
    ax1.scatter(ref_raw[:,0], ref_raw[:,1], ref_raw[:,2], s=2, c='r', alpha=0.5, label='Reference (Raw)')
    ax1.scatter(tip_raw[:,0], tip_raw[:,1], tip_raw[:,2], s=2, c='b', alpha=0.5, label='Tip (Raw)')
    ax1.set_title("Before Calibration\n(Showing Hard-Iron Offsets)")
    ax1.legend()
    
    ax2 = fig.add_subplot(122, projection='3d')
    ax2.scatter(ref_cal[:,0], ref_cal[:,1], ref_cal[:,2], s=2, c='r', alpha=0.5, label='Reference (Calibrated)')
    ax2.scatter(tip_cal[:,0], tip_cal[:,1], tip_cal[:,2], s=2, c='b', alpha=0.5, label='Tip (Calibrated)')
    ax2.set_title("After Calibration & Kabsch Alignment\n(Spheres perfectly overlap)")
    ax2.legend()
    
    plt.tight_layout()
    plt.show()

def main():
    parser = argparse.ArgumentParser(description="Wand Magnetic Calibration Tool (v1.0.0)")
    parser.add_argument("-i", "--input", type=str, required=True, help="Path to input calibration.csv log file")
    parser.add_argument("-o", "--output", type=str, default="calibration.json", help="Path to output JSON file (default: calibration.json)")
    parser.add_argument("--plot", action="store_true", help="Display 3D point cloud visualization after processing")
    args = parser.parse_args()

    # Windows path bulletproofing: strip accidental terminal quotes and normalize slashes
    input_file = os.path.normpath(args.input.strip('\'"'))
    output_file = os.path.normpath(args.output.strip('\'"'))

    if not os.path.exists(input_file):
        print(f"Error: Could not find '{input_file}'. Please check the path.")
        return

    print(f"Loading data from {input_file}...")
    df = pd.read_csv(input_file)
    
    # 1. Extract Raw Data
    ref_x, ref_y, ref_z = df['refX_raw'].values, df['refY_raw'].values, df['refZ_raw'].values
    tip_x, tip_y, tip_z = df['tipX_raw'].values, df['tipY_raw'].values, df['tipZ_raw'].values
    
    # EMI / Glitch filter: drop any readings exceeding 50,000 counts
    LIMIT = 50000
    mask = (np.abs(ref_x) < LIMIT) & (np.abs(ref_y) < LIMIT) & (np.abs(ref_z) < LIMIT) & \
           (np.abs(tip_x) < LIMIT) & (np.abs(tip_y) < LIMIT) & (np.abs(tip_z) < LIMIT)
           
    if not mask.all():
        dropped = len(mask) - mask.sum()
        print(f"Filtered out {dropped} EMI glitch points (>50,000 counts).")
        ref_x, ref_y, ref_z = ref_x[mask], ref_y[mask], ref_z[mask]
        tip_x, tip_y, tip_z = tip_x[mask], tip_y[mask], tip_z[mask]
    
    # 2. Calculate Offsets and Soft-Iron Matrices
    ref_center, ref_W, ref_radii, ref_rad_mean = get_calibration_matrices(ref_x, ref_y, ref_z)
    tip_center, tip_W, tip_radii, tip_rad_mean = get_calibration_matrices(tip_x, tip_y, tip_z)
    
    # 3. Assess Coverage
    assess_coverage("Reference Sensor", ref_x, ref_y, ref_z, ref_rad_mean)
    assess_coverage("Tip Sensor", tip_x, tip_y, tip_z, tip_rad_mean)
    
    # 4. Apply Initial Soft/Hard Iron corrections
    ref_raw = np.column_stack((ref_x, ref_y, ref_z))
    tip_raw = np.column_stack((tip_x, tip_y, tip_z))
    
    ref_cal = (ref_raw - ref_center) @ ref_W.T
    tip_cal = (tip_raw - tip_center) @ tip_W.T
    
    # 5. Calculate Physical Alignment (Kabsch Algorithm)
    R_align = kabsch_alignment(ref_cal, tip_cal)
    ref_aligned = ref_cal @ R_align.T
    
    # Calculate angular misalignment in degrees
    trace = np.trace(R_align)
    angle_deg = np.degrees(np.arccos(np.clip((trace - 1.0) / 2.0, -1.0, 1.0)))
    print(f"\n--- Physical Misalignment Analysis ---")
    # 5b. Find Gyroscope Zero-Rate Offset (FOC)
    print("Searching for Gyroscope Zero-Rate offset (FOC)...")
    window_size = 50
    min_var = float('inf')
    best_idx = 0
    for i in range(len(df) - window_size):
        window = df['gyrZ'].iloc[i:i+window_size]
        var = window.var()
        if var < min_var:
            min_var = var
            best_idx = i
            
    if min_var > 5.0:
        print(f"  WARNING: Could not find a quiet stationary window (min variance {min_var:.4f} > 5.0).")
        print("  Skipping Gyro FOC extraction! To calibrate the gyroscope, you must leave the wand perfectly still for at least 1 second during the log.")
        foc_x = 0.0
        foc_y = 0.0
        foc_z = 0.0
        has_foc = False
    else:
        foc_x = df['gyrX'].iloc[best_idx:best_idx+window_size].mean()
        foc_y = df['gyrY'].iloc[best_idx:best_idx+window_size].mean()
        foc_z = df['gyrZ'].iloc[best_idx:best_idx+window_size].mean()
        print(f"  Found quiet window at row {best_idx} with variance {min_var:.4f}")
        print(f"  Gyro Offsets: X={foc_x:.3f}, Y={foc_y:.3f}, Z={foc_z:.3f} dps")
        has_foc = True

    print(f"Sensors are physically misaligned by: {angle_deg:.2f} degrees")
    print("Rotation Matrix (Kabsch):")
    print(np.round(R_align, 3))
    
    # 6. Generate final combined calibration config
    # We bake the physical rotation directly into the Reference soft-iron matrix
    ref_soft_final = R_align @ ref_W

    config = {
        "calibration_type": "PC Python Calibration",
        "calibration_date_ms": int(time.time() * 1000),
        "calibration_date": time.strftime("%Y-%m-%d %H:%M:%S"),
        "matrix_version": "2.0",
        "ref_offset": [round(x, 2) for x in ref_center],
        "ref_soft": [[round(x, 4) for x in row] for row in ref_soft_final],
        "tip_offset": [round(x, 2) for x in tip_center],
        "tip_soft": [[round(x, 4) for x in row] for row in tip_W],
        
        "imu_rotation_deg": 0.0
    }
    
    # 7. Save to JSON
    if has_foc:
        config["gyr_offset"] = [round(foc_x, 3), round(foc_y, 3), round(foc_z, 3)]
    with open(output_file, 'w') as f:
        json.dump(config, f, indent=4)
        
    print(f"\n[SUCCESS] Saved new mathematical offsets to '{output_file}'")
    
    # 8. Show Plot
    if args.plot:
        print("Opening 3D visualization. Close the window to exit...")
        plot_calibration(ref_raw, tip_raw, ref_aligned, tip_cal)

if __name__ == '__main__':
    main()

