import pandas as pd
import matplotlib.pyplot as plt

df = pd.read_csv("characterization/v5.1.3/log_rebar_3200_log_2026-09-11_09-12-26-240.csv")
plt.plot(df['nT'])
plt.title("Rebar 3200 CC")
plt.savefig("characterization/v5.1.3/rebar_3200_plot.png")
