#!/usr/bin/env python3
"""Draw the representation hierarchy and the three roles a module can hold.

Both halves are definitions from the manuscript rather than measurements. The
figure is displayed at 0.82 column width, so its type is sized to land at or
above the ten-point floor the call for papers requires: labels are short and the
explanatory text lives in the caption, which is set by LaTeX at body size.
"""

from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyArrowPatch, Rectangle

ROOT = Path(__file__).resolve().parent.parent
INK = "#1B2733"
MOD = "#28609A"
DER = "#178477"
SUB = "#7A8791"
SIZE = 13.0


def frame(ax, x, y, w, h, color, face="white", lw=1.0):
    ax.add_patch(Rectangle((x, y), w, h, facecolor=face, edgecolor=color,
                           linewidth=lw, zorder=2))


def main():
    plt.rcParams.update({"font.family": "DejaVu Sans", "pdf.fonttype": 42})
    fig = plt.figure(figsize=(3.33, 2.35))
    ax = fig.add_axes([0, 0, 1, 1])
    ax.set_xlim(0, 100)
    ax.set_ylim(0, 100)
    ax.axis("off")

    # (a) Containment as a horizontal chain: a single row keeps every level on
    # its own baseline, so no label can collide with the next frame.
    ax.text(0, 99, "(a) Containment", fontsize=SIZE, color=INK, va="top")
    levels = ["Mod", "Fn", "Blk", "Op → Val"]
    for index, name in enumerate(levels):
        x = index * 25
        frame(ax, x, 66, 22, 14, MOD, face="#F4F8FC" if index == 0 else "white")
        ax.text(x + 11, 73, name, fontsize=SIZE, color=INK, ha="center", va="center")
        if index:
            ax.text(x - 1.5, 73, "⊃", fontsize=SIZE, color=MOD,
                    ha="center", va="center")

    # (b) Three roles.
    ax.text(0, 46, "(b) Roles", fontsize=SIZE, color=INK, va="top")
    for x, tag, color, face in ((0, "S", SUB, "#F4F6F8"),
                                (36, "K", DER, "#F1F8F6"),
                                (72, "P", SUB, "#F4F6F8")):
        frame(ax, x, 14, 28, 20, color, face=face)
        ax.text(x + 14, 24, tag, fontsize=SIZE, color=INK, ha="center", va="center")
    ax.add_patch(FancyArrowPatch((28.5, 24), (35.5, 24), arrowstyle="-|>",
                                 mutation_scale=11, color=DER, linewidth=1.3))
    ax.add_patch(FancyArrowPatch((64.5, 24), (71.5, 24), arrowstyle="-|>",
                                 mutation_scale=11, color=MOD, linewidth=1.3))
    ax.text(32, 37, "derive", fontsize=SIZE - 1, color=DER, ha="center", va="center")
    ax.text(68, 37, "invoke", fontsize=SIZE - 1, color=MOD, ha="center", va="center")
    ax.text(0, 5, "neither replaces P", fontsize=SIZE - 1, color=INK, va="center")

    out = ROOT / "figures" / "structure.pdf"
    fig.savefig(out, bbox_inches=None)
    fig.savefig(out.with_suffix(".png"), dpi=300, bbox_inches=None)
    plt.close(fig)
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
