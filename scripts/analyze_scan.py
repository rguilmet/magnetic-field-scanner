#!/usr/bin/env python3
"""
Magnetic Field Scanner - Post-Scan Analysis Report Generator
Version: v1.0.0

Ingests log or calibration CSV files, performs automated math/noise analysis,
and generates a formatted Markdown report with embedded plots.
"""

import numpy as np
import pandas as pd
import argparse
import os
import matplotlib.pyplot as plt
from datetime import datetime

__filename__ = os.path.basename(__file__)
__version__ = "v1.0.0"

def get_calibration_matrices(x, y, z):
    x_mean, y_mean, z_mean = np.mean(x), np.mean(y), np.mean(z)
    xn, yn, zn = x - x_mean, y - y_mean, z - z_mean
    D = np.array([xn*xn, yn*yn, zn*zn, 2*xn*yn, 2*xn*zn, 2*yn*zn, 2*xn, 2*yn, 2*zn]).T
    v, _, _, _ = np.linalg.lstsq(D, np.ones_like(xn), rcond=None)
    A = np.array([[v[0], v[3], v[4], v[6]],
                  [v[3], v[1], v[5], v[7]],
                  [v[4], v[5], v[2], v[8]],
                  [v[6], v[7], v[8], -1]])
    center_n = np.linalg.solve(-A[:3, :3], v[6:9])
    T = np.eye(4)
    T[3, :3] = center_n
    R = T.dot(A).dot(T.T)
    evals, evecs = np.linalg.eigh(R[:3, :3] / -R[3, 3])
    radii = 1.0 / np.sqrt(np.abs(evals))
    target_radius = np.mean(radii)
    soft_iron = evecs.dot(np.diag(target_radius / radii)).dot(evecs.T)
    center = center_n + np.array([x_mean, y_mean, z_mean])
    return center, soft_iron, target_radius

def kabsch_alignment(P, Q):
    H = P.T @ Q
    U, S, Vt = np.linalg.svd(H)
    R = Vt.T @ U.T
    if np.linalg.det(R) < 0:
        Vt[2,:] *= -1
        R = Vt.T @ U.T
    return R

def plot_3d(ref_raw, tip_raw, ref_cal, tip_cal, out_path):
    fig = plt.figure(figsize=(12, 6))
    ax1 = fig.add_subplot(121, projection='3d')
    ax1.scatter(ref_raw[:,0], ref_raw[:,1], ref_raw[:,2], s=2, c='r', alpha=0.5, label='Ref (Raw)')
    ax1.scatter(tip_raw[:,0], tip_raw[:,1], tip_raw[:,2], s=2, c='b', alpha=0.5, label='Tip (Raw)')
    ax1.set_title("Raw Data")
    ax1.legend()
    
    ax2 = fig.add_subplot(122, projection='3d')
    ax2.scatter(ref_cal[:,0], ref_cal[:,1], ref_cal[:,2], s=2, c='r', alpha=0.5, label='Ref (Cal)')
    ax2.scatter(tip_cal[:,0], tip_cal[:,1], tip_cal[:,2], s=2, c='b', alpha=0.5, label='Tip (Cal)')
    ax2.set_title("Calibrated & Aligned")
    ax2.legend()
    
    plt.tight_layout()
    plt.savefig(out_path)
    plt.close()

def plot_timeseries(df, out_path_prefix):
    plots = []
    
    if 'mag' in df.columns:
        plt.figure(figsize=(10, 4))
        plt.plot(df['time_ms'], df['mag'], color='purple')
        plt.title('Magnetic Magnitude over Time')
        plt.xlabel('Time (ms)')
        plt.ylabel('Magnitude (uT)')
        plt.grid(True, alpha=0.3)
        plt.tight_layout()
        p1 = f"{out_path_prefix}_mag.png"
        plt.savefig(p1)
        plt.close()
        plots.append(os.path.basename(p1))
        
    if 'gradX' in df.columns:
        plt.figure(figsize=(10, 4))
        plt.plot(df['time_ms'], df['gradX'], label='gradX')
        plt.plot(df['time_ms'], df['gradY'], label='gradY')
        plt.plot(df['time_ms'], df['gradZ'], label='gradZ')
        plt.title('Spatial Gradient over Time')
        plt.xlabel('Time (ms)')
        plt.ylabel('Gradient')
        plt.legend()
        plt.grid(True, alpha=0.3)
        plt.tight_layout()
        p2 = f"{out_path_prefix}_grad.png"
        plt.savefig(p2)
        plt.close()
        plots.append(os.path.basename(p2))

    if 'Azimuth' in df.columns:
        plt.figure(figsize=(10, 4))
        plt.plot(df['time_ms'], df['Azimuth'], label='Azimuth', color='green')
        plt.plot(df['time_ms'], df['Elevation'], label='Elevation', color='blue')
        plt.title('Attitude (Euler Angles) over Time')
        plt.xlabel('Time (ms)')
        plt.ylabel('Degrees')
        plt.legend()
        plt.grid(True, alpha=0.3)
        plt.tight_layout()
        p3 = f"{out_path_prefix}_euler.png"
        plt.savefig(p3)
        plt.close()
        plots.append(os.path.basename(p3))
        
    return plots

def main():
    print(f"=== {__filename__} {__version__} ===")
    parser = argparse.ArgumentParser(description="Wand Magnetic Report Generator")
    parser.add_argument("-i", "--input", type=str, required=True, help="Path to input CSV log or calibration file")
    args = parser.parse_args()
    
    input_file = os.path.normpath(args.input.strip('\'"'))
    if not os.path.exists(input_file):
        print(f"Error: File not found: {input_file}")
        return
        
    print(f"Loading {input_file}...")
    df = pd.read_csv(input_file)
    
    try:
        ref_x, ref_y, ref_z = df['refX_raw'].values, df['refY_raw'].values, df['refZ_raw'].values
        tip_x, tip_y, tip_z = df['tipX_raw'].values, df['tipY_raw'].values, df['tipZ_raw'].values
    except KeyError:
        print("Error: CSV missing required raw columns (refX_raw, tipX_raw, etc).")
        return
        
    LIMIT = 50000
    mask = (np.abs(ref_x) < LIMIT) & (np.abs(ref_y) < LIMIT) & (np.abs(ref_z) < LIMIT) & \
           (np.abs(tip_x) < LIMIT) & (np.abs(tip_y) < LIMIT) & (np.abs(tip_z) < LIMIT)
    
    ref_x, ref_y, ref_z = ref_x[mask], ref_y[mask], ref_z[mask]
    tip_x, tip_y, tip_z = tip_x[mask], tip_y[mask], tip_z[mask]
    
    base_name = os.path.splitext(os.path.basename(input_file))[0]
    report_path = os.path.join("reports", f"Report_{base_name}.md")
    img_dir = os.path.join("reports", "images")
    os.makedirs(img_dir, exist_ok=True)
    
    metrics = {
        "Data Points": len(ref_x),
        "Duration (s)": round((df['time_ms'].iloc[-1] - df['time_ms'].iloc[0]) / 1000.0, 2),
        "File Type": "Log" if 'calibration' not in input_file.lower() else "Calibration"
    }
    
    fit_success = False
    ref_cal = None
    tip_aligned = None
    ref_raw_stack = np.column_stack((ref_x, ref_y, ref_z))
    tip_raw_stack = np.column_stack((tip_x, tip_y, tip_z))
    
    try:
        ref_center, ref_W, ref_radius = get_calibration_matrices(ref_x, ref_y, ref_z)
        tip_center, tip_W, tip_radius = get_calibration_matrices(tip_x, tip_y, tip_z)
        
        ref_cal = (ref_raw_stack - ref_center) @ ref_W.T
        tip_cal = (tip_raw_stack - tip_center) @ tip_W.T
        
        R_align = kabsch_alignment(tip_cal, ref_cal)
        tip_aligned = tip_cal @ R_align.T
        
        ref_mags = np.linalg.norm(ref_cal, axis=1)
        tip_mags = np.linalg.norm(tip_aligned, axis=1)
        
        metrics["Ref Fit Variance"] = round(float(np.var(ref_mags)), 4)
        metrics["Tip Fit Variance"] = round(float(np.var(tip_mags)), 4)
        metrics["Kabsch Rotation (deg)"] = round(np.degrees(np.arccos(np.clip((np.trace(R_align) - 1.0) / 2.0, -1.0, 1.0))), 2)
        fit_success = True
    except Exception as e:
        metrics["Fit Error"] = str(e)
    
    if 'mag' in df.columns:
        metrics["Magnitude Variance (Noise)"] = round(float(df['mag'].var()), 4)
        metrics["Magnitude P2P (uT)"] = round(float(df['mag'].max() - df['mag'].min()), 4)
        
        if 'gradX' in df.columns:
            grad_mag = np.sqrt(df['gradX']**2 + df['gradY']**2 + df['gradZ']**2)
            metrics["Max Gradient Magnitude"] = round(float(grad_mag.max()), 4)
            
    print("Generating plots...")
    img_prefix = os.path.join(img_dir, base_name)
    plot_files = []
    
    if fit_success:
        p_3d = f"{img_prefix}_3d.png"
        plot_3d(ref_raw_stack, tip_raw_stack, ref_cal, tip_aligned, p_3d)
        plot_files.append(os.path.basename(p_3d))
        
    ts_plots = plot_timeseries(df, img_prefix)
    plot_files.extend(ts_plots)
    
    print(f"Writing report to {report_path}...")
    with open(report_path, "w") as f:
        f.write(f"# Magnetic Scan Analysis Report\n")
        f.write(f"**Generated:** {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n")
        f.write(f"**Source File:** `{os.path.basename(input_file)}`\n\n")
        
        f.write(f"## Metrics\n")
        f.write("| Metric | Value |\n")
        f.write("|--------|-------|\n")
        for k, v in metrics.items():
            f.write(f"| {k} | {v} |\n")
            
        f.write(f"\n## Visualizations\n")
        if not fit_success:
            f.write("> [!WARNING]\n> The algebraic ellipsoid solver failed to converge on this dataset. This usually happens if the scan does not form a complete 3D sphere (e.g. walking in a straight line without tumbling the wand).\n\n")
            
        for img in plot_files:
            f.write(f"![{img}](images/{img})\n\n")
            
    print("[SUCCESS] Report generation complete!")

if __name__ == '__main__':
    main()
