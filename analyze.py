import sys
import math
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
from scipy import stats


FILES = [
    "results_readwrite.csv",
    "results_mmap.csv",
]


def confidence_interval(values, confidence=0.95):
    values = np.asarray(values, dtype=float)
    n = len(values)

    mean = np.mean(values)
    std = np.std(values, ddof=1)

    sem = std / math.sqrt(n)
    t_critical = stats.t.ppf((1 + confidence) / 2, n - 1)

    margin = t_critical * sem

    return mean, std, mean - margin, mean + margin


def main():
    dataframes = []

    for filename in FILES:
        try:
            df = pd.read_csv(filename)
        except FileNotFoundError:
            print(f"ERROR: {filename} not found.")
            print("Run both experiments first:")
            print("  ./collect.sh readwrite")
            print("  ./collect.sh mmap")
            sys.exit(1)

        dataframes.append(df)

    df = pd.concat(dataframes, ignore_index=True)

    print("=" * 80)
    print("RAW DATA")
    print("=" * 80)
    print(df.to_string(index=False))

    print()
    print("=" * 80)
    print("STATISTICS")
    print("=" * 80)

    rows = []

    for method, group in df.groupby("method"):
        values = group["wall_time_s"].astype(float)

        mean, std, ci_low, ci_high = confidence_interval(values)

        rows.append({
            "method": method,
            "N": len(values),
            "mean_s": mean,
            "std_s": std,
            "min_s": values.min(),
            "max_s": values.max(),
            "ci95_low_s": ci_low,
            "ci95_high_s": ci_high,
            "ci95_width_s": ci_high - ci_low,
        })

    summary = pd.DataFrame(rows)

    print(
        summary.to_string(
            index=False,
            float_format=lambda x: f"{x:.6f}"
        )
    )

    summary.to_csv("summary.csv", index=False)

    print()
    print("Saved statistics to summary.csv")

    # ------------------------------------------------------------
    # Speedup relative to ordinary read/write
    # ------------------------------------------------------------

    means = dict(zip(summary["method"], summary["mean_s"]))

    if "read" in means and "mmap_read" in means:
        print()
        print(
            f"mmap_read speedup: "
            f"{means['read'] / means['mmap_read']:.2f}x"
        )

    if "write" in means and "mmap_write" in means:
        print(
            f"mmap_write speedup: "
            f"{means['write'] / means['mmap_write']:.2f}x"
        )

    # ------------------------------------------------------------
    # Context switches and page faults
    # ------------------------------------------------------------

    print()
    print("=" * 80)
    print("PAGE FAULTS / CONTEXT SWITCHES")
    print("=" * 80)

    resource_summary = (
        df.groupby("method")
        .agg(
            page_faults_mean=("page_faults", "mean"),
            page_faults_std=("page_faults", "std"),
            vol_ctx_mean=("vol_ctx", "mean"),
            vol_ctx_std=("vol_ctx", "std"),
            invol_ctx_mean=("invol_ctx", "mean"),
            invol_ctx_std=("invol_ctx", "std"),
        )
        .reset_index()
    )

    print(
        resource_summary.to_string(
            index=False,
            float_format=lambda x: f"{x:.2f}"
        )
    )

    resource_summary.to_csv(
        "resource_summary.csv",
        index=False
    )

    print()
    print("Saved resource statistics to resource_summary.csv")

    # ------------------------------------------------------------
    # Main experiment plot
    # ------------------------------------------------------------

    plot_order = [
        "read",
        "write",
        "mmap_read",
        "mmap_write",
    ]

    plot_data = summary.set_index("method").loc[
        [x for x in plot_order if x in summary["method"].values]
    ]

    means = plot_data["mean_s"].values

    # CI half-width
    errors = (
        plot_data["ci95_high_s"].values
        - plot_data["mean_s"].values
    )

    labels = plot_data.index.tolist()

    plt.figure(figsize=(10, 6))

    plt.bar(
        labels,
        means,
        yerr=errors,
        capsize=6
    )

    plt.yscale("log")

    plt.ylabel("Wall time, s")
    plt.xlabel("Method")
    plt.title("Graph traversal performance")
    plt.grid(axis="y", alpha=0.3)

    plt.tight_layout()
    plt.savefig(
        "comparison.png",
        dpi=200
    )

    plt.show()

    print()
    print("Saved plot to comparison.png")


if __name__ == "__main__":
    main()