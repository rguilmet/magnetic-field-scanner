import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
import os
import argparse

def plot_case_study(flat_csv, angled_csv, output_file):
    df_flat = pd.read_csv(flat_csv)
    df_angled = pd.read_csv(angled_csv)
    
    # Calculate dt in milliseconds
    df_flat['dt'] = df_flat['time_ms'].diff().fillna(0)
    df_angled['dt'] = df_angled['time_ms'].diff().fillna(0)
    
    # Calculate a running average for dt to smooth it out for plotting (optional, but shows the stall nicely)
    df_flat['dt_smooth'] = df_flat['dt'].rolling(window=10, min_periods=1).mean()
    df_angled['dt_smooth'] = df_angled['dt'].rolling(window=10, min_periods=1).mean()

    # Center the time axis
    df_flat['t_norm'] = (df_flat['time_ms'] - df_flat['time_ms'].iloc[0]) / 1000.0
    df_angled['t_norm'] = (df_angled['time_ms'] - df_angled['time_ms'].iloc[0]) / 1000.0

    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(12, 10), sharex=False)
    plt.subplots_adjust(hspace=0.3)
    
    # ==========================================
    # PLOT 1: FLAT WAND (THE SYMPTOM)
    # ==========================================
    ax1.set_title("Flat Wand (0°, 0°) - Single Axis Saturation & RTOS Jitter", fontsize=14, fontweight='bold')
    
    # Plot Tip Z Raw (or whichever saturated, let's plot Tip X, Y, Z raw)
    ax1.plot(df_flat['t_norm'], df_flat['tipZ_raw'], label='Tip Z-Axis (Saturated)', color='blue', linewidth=2)
    ax1.plot(df_flat['t_norm'], df_flat['tipY_raw'], label='Tip Y-Axis', color='green', alpha=0.5)
    ax1.plot(df_flat['t_norm'], df_flat['tipX_raw'], label='Tip X-Axis', color='red', alpha=0.5)
    
    ax1.set_ylabel("Raw Sensor Count (LSB)", color='black', fontsize=12)
    ax1.tick_params(axis='y', labelcolor='black')
    
    # Create twin axis for dt (Jitter)
    ax1_twin = ax1.twinx()
    ax1_twin.plot(df_flat['t_norm'], df_flat['dt_smooth'], label='RTOS Time Delta (Jitter)', color='darkorange', linewidth=2, linestyle='--')
    ax1_twin.set_ylabel("Time Between Samples (ms)", color='darkorange', fontsize=12)
    ax1_twin.tick_params(axis='y', labelcolor='darkorange')
    
    # Combine legends for ax1
    lines1, labels1 = ax1.get_legend_handles_labels()
    lines2, labels2 = ax1_twin.get_legend_handles_labels()
    ax1.legend(lines1 + lines2, labels1 + labels2, loc='upper left')

    ax1.set_xlabel("Time (seconds)")
    ax1.grid(True, alpha=0.3)

    # ==========================================
    # PLOT 2: 45-45 WAND (THE CURE)
    # ==========================================
    ax2.set_title("45°/45° Angled Wand - Vector Distributed Flux (No Saturation)", fontsize=14, fontweight='bold')
    
    ax2.plot(df_angled['t_norm'], df_angled['tipZ_raw'], label='Tip Z-Axis (Linear)', color='blue', linewidth=2)
    ax2.plot(df_angled['t_norm'], df_angled['tipY_raw'], label='Tip Y-Axis (Linear)', color='green', linewidth=2)
    ax2.plot(df_angled['t_norm'], df_angled['tipX_raw'], label='Tip X-Axis (Linear)', color='red', linewidth=2)
    
    ax2.set_ylabel("Raw Sensor Count (LSB)", color='black', fontsize=12)
    ax2.tick_params(axis='y', labelcolor='black')
    
    # Create twin axis for dt (Jitter)
    ax2_twin = ax2.twinx()
    ax2_twin.plot(df_angled['t_norm'], df_angled['dt_smooth'], label='RTOS Time Delta (Stable)', color='darkorange', linewidth=2, linestyle='--')
    ax2_twin.set_ylabel("Time Between Samples (ms)", color='darkorange', fontsize=12)
    ax2_twin.tick_params(axis='y', labelcolor='darkorange')
    ax2_twin.set_ylim(ax1_twin.get_ylim()) # Match Y axis scale of the top plot for visual comparison
    
    # Combine legends for ax2
    lines3, labels3 = ax2.get_legend_handles_labels()
    lines4, labels4 = ax2_twin.get_legend_handles_labels()
    ax2.legend(lines3 + lines4, labels3 + labels4, loc='upper left')
    
    ax2.set_xlabel("Time (seconds)")
    ax2.grid(True, alpha=0.3)
    
    fig.tight_layout()
    plt.savefig(output_file, dpi=150, bbox_inches='tight')
    print(f"Saved Case Study plot to {output_file}")

if __name__ == "__main__":
    flat = r"characterization\v5.1.3 & v5.1.4\log_rebar_400_log_2026-09-11_09-10-00-240.csv"
    angled = r"characterization\v5.1.3 & v5.1.4\log_rebar_400_45-45_log_2026-09-17_13-32-25-240.csv"
    out = r"docs\reports\case_study_saturation_plot.png"
    
    # Just a quick check to see if we should use 800 or 1600 if 400 is not as dramatic
    # But 400 should show it clearly since the user observed it there.
    
    if not os.path.exists(flat):
        print(f"Cannot find {flat}")
    elif not os.path.exists(angled):
        print(f"Cannot find {angled}")
    else:
        plot_case_study(flat, angled, out)
