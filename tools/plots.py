#!/usr/bin/env python3
"""Plot the convergence and pocket counts written by tools/run_math.py."""

import argparse
import csv
from pathlib import Path

from matplotlib.figure import Figure
from matplotlib.ticker import EngFormatter, MaxNLocator, PercentFormatter, StrMethodFormatter


ROOT = Path(__file__).resolve().parents[1]
INK = "#243244"
MUTED = "#526879"
TEAL = "#00756c"
ORANGE = "#ae500d"
BLUE = "#3a63a8"


def read_rows(path):
    with path.open(encoding="utf-8", newline="") as source:
        rows = list(csv.DictReader(source))
    if not rows:
        raise ValueError(f"{path} has no measurements")
    return rows


def chart(title):
    figure = Figure(figsize=(9.6, 4.7), facecolor="white")
    figure.subplots_adjust(left=0.12, right=0.97, bottom=0.16, top=0.74)
    figure.text(0.12, 0.91, title, fontsize=19, weight="bold", color=INK)
    axis = figure.subplots()
    axis.spines[["top", "right"]].set_visible(False)
    axis.spines[["left", "bottom"]].set_color("#b9c7cc")
    axis.tick_params(colors=MUTED, labelsize=10)
    axis.yaxis.grid(True, color="#e1e8e9")
    axis.set_axisbelow(True)
    return figure, axis


def save_chart(figure, axis, path):
    axis.legend(loc="lower left", bbox_to_anchor=(0, 1.02), borderaxespad=0,
                ncols=2, frameon=False, fontsize=10, labelcolor=INK)
    figure.savefig(path, dpi=150, backend="agg")
    print(path)


def plot_convergence(rows, path):
    spins = [int(row["spins"]) for row in rows]
    figure, axis = chart("Cumulative return to player")
    for key, label, color, style in [
        ("standard_rtp", "Standard · number 0", BLUE, "-"),
        ("lucky_rtp", "Lucky · number 0", ORANGE, "--"),
        ("portfolio_rtp", "Lucky · all 37 numbers", TEAL, "-"),
    ]:
        values = [float(row[key]) for row in rows]
        axis.plot(spins, values, color=color, linestyle=style, linewidth=2, label=label)
        axis.plot(spins[-1], values[-1], marker="o", markersize=4, color=color)
    theory = float(rows[-1]["theoretical_rtp"])
    axis.axhline(theory, color=INK, linestyle=":", linewidth=1.8,
                 label=f"Theoretical RTP · {theory:.4%}")
    axis.set_xlabel("Completed spins", color=INK, labelpad=8)
    axis.xaxis.set_major_locator(MaxNLocator(nbins=6, integer=True))
    axis.xaxis.set_major_formatter(EngFormatter(places=None, sep=""))
    axis.set_xlim(left=0, right=spins[-1] * 1.015)
    axis.set_ylabel("Credits returned / credits staked", color=INK, labelpad=8)
    axis.yaxis.set_major_formatter(PercentFormatter(xmax=1))
    save_chart(figure, axis, path)


def plot_distribution(rows, path):
    pockets = [int(row["number"]) for row in rows]
    if pockets != list(range(37)):
        raise ValueError("histogram.csv must contain one row for each number from 0 to 36")
    observed = [int(row["observed"]) for row in rows]
    expected = float(rows[0]["expected"])
    figure, axis = chart(f"Pocket frequencies over {sum(observed):,} spins")
    # Each stem starts at the expected count so small differences remain legible.
    axis.vlines(pockets, expected, observed, color="#8bc3bd", linewidth=2.5)
    axis.scatter(pockets, observed, color=TEAL, s=28, zorder=3, label="Observed count")
    axis.axhline(expected, color=INK, linestyle=":", linewidth=1.8,
                 label=f"Expected per pocket · {expected:,.1f}")
    axis.set_xlim(-0.7, 36.7)
    axis.set_xticks(range(0, 37, 2))
    axis.set_xticks(range(1, 36, 2), minor=True)
    axis.set_xlabel("Printed wheel number", color=INK, labelpad=8)
    axis.set_ylabel("Landings per pocket", color=INK, labelpad=8)
    axis.yaxis.set_major_locator(MaxNLocator(nbins=5, integer=True))
    axis.yaxis.set_major_formatter(StrMethodFormatter("{x:,.0f}"))
    save_chart(figure, axis, path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--results", type=Path, default=ROOT / "tools/results",
                        help="directory containing convergence.csv and histogram.csv")
    parser.add_argument("--output", type=Path, default=ROOT / "docs/images",
                        help="directory for the two PNGs")
    args = parser.parse_args()
    try:
        convergence = read_rows(args.results / "convergence.csv")
        histogram = read_rows(args.results / "histogram.csv")
        if int(convergence[-1]["spins"]) != sum(int(row["observed"]) for row in histogram):
            raise ValueError("final spin count and histogram total differ")
        args.output.mkdir(parents=True, exist_ok=True)
        plot_convergence(convergence, args.output / "rtp-convergence.png")
        plot_distribution(histogram, args.output / "pocket-distribution.png")
    except (OSError, KeyError, ValueError) as error:
        parser.exit(1, f"Could not plot results: {error}\n")


if __name__ == "__main__":
    main()
