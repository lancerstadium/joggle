# EuroSys submission draft

`main.tex` is the single authoritative manuscript source. Keeping the paper in
LaTeX avoids a second Markdown source drifting from hand-designed figures,
tables, equations, and venue formatting. It is not yet a final submission: the
controlled Linux results, citation audit, research-transparency statements,
and external review remain open.

The Motivation, Design, and compiler-function mechanism were partially
rewritten against [the September 15 research plan](../plan.md).
[CHECKLIST.md](CHECKLIST.md) records delivery. The current regression verifies
code/artifact isolation, not a derivation/composition advantage over another
compiler; the decisive external evaluation is still open. Do not interpret
existing figures or the abstract as that missing validation.

Compile it with:

```sh
latexmk -pdf -interaction=nonstopmode -halt-on-error \
  -cd paper/submission/main.tex
```

The official EuroSys 2027 limit is 12 pages of technical content plus
references. Check the compiled PDF rather than estimating the count from source
lines or words. Evidence protocols and raw records live in the parent `paper/`
directory; they are not alternate manuscript sources.
