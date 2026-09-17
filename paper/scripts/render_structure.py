#!/usr/bin/env python3
"""Draw the representation hierarchy and the three roles a module can hold.

Both halves are definitions from the manuscript rather than measurements: the
nesting is the representation's containment, and the roles are the distinction
the derivation boundary depends on. Single column, vector output.
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


def frame(ax, x, y, w, h, color, face="white", lw=0.9):
    ax.add_patch(Rectangle((x, y), w, h, facecolor=face, edgecolor=color,
                           linewidth=lw, zorder=2))


def main():
    plt.rcParams.update({"font.family": "DejaVu Sans", "pdf.fonttype": 42})
    fig = plt.figure(figsize=(3.33, 3.05))
    ax = fig.add_axes([0, 0, 1, 1])
    ax.set_xlim(0, 100)
    ax.set_ylim(0, 100)
    ax.axis("off")

    # (a) Containment. One label line per level, each box strictly inside its
    # parent so no label can collide with the next frame.
    ax.text(0, 100, "(a) Representation", fontsize=8, color=INK, va="top")
    frame(ax, 0, 58, 100, 36, MOD, face="#F4F8FC", lw=1.0)
    ax.text(3, 90, "Mod", fontsize=8, color=MOD, va="center")
    ax.text(13, 90, "one program: definitions and data", fontsize=7.2,
            color=INK, va="center")
    frame(ax, 5, 62, 90, 24, MOD, lw=0.8)
    ax.text(8, 82, "Fn", fontsize=8, color=MOD, va="center")
    ax.text(16, 82, "typed, generic, overloadable", fontsize=7.2, color=INK,
            va="center")
    frame(ax, 10, 66, 80, 12, MOD, lw=0.8)
    ax.text(13, 74, "Blk", fontsize=7.6, color=MOD, va="center")
    ax.text(22, 74, "control flow", fontsize=7.0, color=INK, va="center")
    frame(ax, 15, 68, 70, 4, MOD, lw=0.8)
    ax.text(50, 70, "Op  →  Val", fontsize=7.0, color=INK, va="center")

    # (b) Roles. Labels sit inside their own box; the arrow label sits above the
    # arrow with a clear band, and the notes go on their own lines below.
    ax.text(0, 52, "(b) Roles across three modules", fontsize=8, color=INK, va="top")
    for x, tag, sub, color, face in ((0, "S", "installed  $f$", SUB, "#F4F6F8"),
                                     (36, "K", "derived  $f_e$", DER, "#F1F8F6"),
                                     (72, "P", "subject", SUB, "#F4F6F8")):
        frame(ax, x, 20, 28, 20, color, face=face, lw=1.0)
        ax.text(x + 14, 33, tag, fontsize=9, color=INK, ha="center", va="center")
        ax.text(x + 14, 25, sub, fontsize=7.2, color=INK, ha="center", va="center")
    ax.add_patch(FancyArrowPatch((28.5, 30), (35.5, 30), arrowstyle="-|>",
                                 mutation_scale=8, color=DER, linewidth=1.1))
    ax.text(32, 44, "derive", fontsize=7.0, color=DER, ha="center", va="center")
    ax.add_patch(FancyArrowPatch((64.5, 30), (71.5, 30), arrowstyle="-|>",
                                 mutation_scale=8, color=MOD, linewidth=1.1))
    ax.text(68, 44, "invoke", fontsize=7.0, color=MOD, ha="center", va="center")

    ax.text(0, 14, "Both act on $P$, and neither replaces it.", fontsize=7.0,
            color=INK, va="center")
    ax.text(0, 8, "Edits to $K$ and to $P$ roll back separately.", fontsize=7.0,
            color=INK, va="center")
    ax.text(0, 2, "Package: module.jog + lib/*.jog (+ src/)", fontsize=7.0,
            color=INK, va="center")

    out = ROOT / "figures" / "structure.pdf"
    fig.savefig(out, bbox_inches=None)
    fig.savefig(out.with_suffix(".png"), dpi=300, bbox_inches=None)
    plt.close(fig)
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
