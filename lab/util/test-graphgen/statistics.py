import sys
from pathlib import Path

import pandas as pd
import scipy.stats as stats
import matplotlib.pyplot as plt
import matplotlib
import numpy as np
from scipy.stats import gaussian_kde

matplotlib.use("Agg")


def parse_time(s):
    parts = str(s).split(":")

    if len(parts) == 3:
        h, m, sec = parts
        return int(h) * 3600 + int(m) * 60 + float(sec)

    if len(parts) == 2:
        m, sec = parts
        return int(m) * 60 + float(sec)

    return float(s)


if len(sys.argv) < 2:
    print(f"Usage: {Path(sys.argv[0]).name} <csv_file>")
    sys.exit(1)

csv_path = Path(sys.argv[1])
out_prefix = csv_path.stem

if not csv_path.exists():
    print(f"File not found: {csv_path}")
    sys.exit(1)


df = pd.read_csv(csv_path)

df["wall_seconds"] = df["wall_time_s"].apply(parse_time)
df = df[["wall_seconds", "graph"]]

grouped = df.groupby("graph")


for graph, group in grouped:

    curr = group["wall_seconds"]

    mean = curr.mean()

    std = curr.std()
    n = len(curr)
    se = std / (n ** 0.5)

    low, high = stats.t.interval(
        0.95,
        df=n - 1,
        loc=mean,
        scale=se
    )

    print(f"\n{graph}")
    print(f"N = {n}")
    print(f"Среднее = {mean:.4f}")
    print(f"Стандартное отклонение = {std:.4f}")
    print(f"Стандартная ошибка = {se:.4f}")
    print(f"95% ДИ = [{low:.4f}; {high:.4f}]")
