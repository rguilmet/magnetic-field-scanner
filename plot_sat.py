import pandas as pd
import matplotlib.pyplot as plt

df = pd.read_csv("characterization/v5.1.3/log_saturation_3200_log_2026-09-11_09-04-58-240.csv")
plt.plot(df['nT'])
plt.title("Saturation 3200 CC")
plt.savefig("C:/Users/rguilmet/.gemini/antigravity/brain/f1d175fb-954a-463e-96d9-f351d6ab2294/scratch/sat_3200_plot.png")
