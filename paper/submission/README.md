# EuroSys submission draft

`main.tex` is a mechanically rendered, anonymous review draft of
`paper/manuscript.md`. It is not a final submission: the controlled Linux
results, citation audit, research-transparency statements, and external review
are still open.

Regenerate and compile it with:

```sh
python3 paper/submission/render.py
latexmk -pdf -interaction=nonstopmode -halt-on-error \
  -cd paper/submission/main.tex
```

The renderer fails on unsupported Markdown constructs instead of silently
dropping them. `--check` verifies that `main.tex` matches the current evidence
draft. The official EuroSys 2027 limit is 12 pages of technical content plus
references; page count is checked from the compiled PDF, not estimated from
Markdown words.
