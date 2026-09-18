import pandas as pd
import matplotlib.pyplot as plt
import os

def plot_case_study(flat_csv, angled_csv, output_file):
    df_flat = pd.read_csv(flat_csv)
    df_angled = pd.read_csv(angled_csv)
    
    # Center the time axis
    df_flat['t_norm'] = (df_flat['time_ms'] - df_flat['time_ms'].iloc[0]) / 1000.0
    df_angled['t_norm'] = (df_angled['time_ms'] - df_angled['time_ms'].iloc[0]) / 1000.0

    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(12, 10), sharex=False, sharey=True)
    plt.subplots_adjust(hspace=0.3)
    
    # ==========================================
    # PLOT 1: FLAT WAND (CC=3200 - BLIND)
    # ==========================================
    ax1.set_title("Flat Wand (0°, 0°) at CC=3200 - Core Blindness", fontsize=14, fontweight='bold')
    ax1.plot(df_flat['t_norm'], df_flat['tipZ_raw'], label='Tip Z-Axis (Saturated / Blind)', color='blue', linewidth=2)
    ax1.plot(df_flat['t_norm'], df_flat['tipY_raw'], label='Tip Y-Axis', color='green', alpha=0.5)
    ax1.plot(df_flat['t_norm'], df_flat['tipX_raw'], label='Tip X-Axis', color='red', alpha=0.5)
    
    ax1.set_ylabel("Raw Sensor Count (LSB)", color='black', fontsize=12)
    ax1.legend(loc='upper right')
    ax1.set_xlabel("Time (seconds)")
    ax1.grid(True, alpha=0.3)

    # ==========================================
    # PLOT 2: 45-45 WAND (CC=3200 - RESTORED)
    # ==========================================
    ax2.set_title("45°/45° Angled Wand at CC=3200 - Vector Distributed Flux", fontsize=14, fontweight='bold')
    
    ax2.plot(df_angled['t_norm'], df_angled['tipZ_raw'], label='Tip Z-Axis (Linear)', color='blue', linewidth=2)
    ax2.plot(df_angled['t_norm'], df_angled['tipY_raw'], label='Tip Y-Axis (Linear)', color='green', linewidth=2)
    ax2.plot(df_angled['t_norm'], df_angled['tipX_raw'], label='Tip X-Axis (Linear)', color='red', linewidth=2)
    
    ax2.set_ylabel("Raw Sensor Count (LSB)", color='black', fontsize=12)
    ax2.legend(loc='upper right')
    ax2.set_xlabel("Time (seconds)")
    ax2.grid(True, alpha=0.3)
    
    fig.tight_layout()
    plt.savefig(output_file, dpi=150, bbox_inches='tight')
    print(f"Saved Case Study plot to {output_file}")

if __name__ == "__main__":
    flat = r"characterization\v5.1.3 & v5.1.4\log_rebar_3200_log_2026-09-11_17-41-13-240.csv"
    angled = r"characterization\v5.1.3 & v5.1.4\log_rebar_3200_45-45_log_2026-09-17_13-40-28-240.csv"
    out = r"docs\reports\case_study_saturation_plot_3200.png"
    
    plot_case_study(flat, angled, out)
