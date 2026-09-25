.PHONY: all figures agent-figure clean

# Authoring order: revise README.md, synchronize the TeX section, then build.
all: sigconf.pdf supplement.pdf

# Data and plotting changes must reach the PDFs through the normal build.
# Each renderer also refreshes its PNG preview and, where applicable, summaries.
MEASURED_FIGURES = figures/extension-size.pdf figures/figure-05-footprint.pdf figures/figure-06-update.pdf \
	figures/figure-07-performance.pdf figures/figure-07-models.pdf
PLOT_COMMON = ../artifact/figures/common.py

figures: $(MEASURED_FIGURES)

figures/extension-size.pdf: data/extension-size.csv data/extension-size.json \
	render_extensions.py $(PLOT_COMMON) $(wildcard ../artifact/extensions/*/reference.*)
	python3 render_extensions.py

# Render only after the complete matched Agent matrix has been assembled.
# Re-audit raw trajectories on every invocation; partial exports are rejected.
agent-figure: data/figure-04-extension.csv data/figure-04-extension.json
	python3 ../artifact/figures/figure_04_extension.py data/figure-04-extension.csv --output figures/figure-04-extension.pdf

figures/figure-05-footprint.pdf: data/package-footprint.csv data/package-footprint.json \
	../artifact/figures/figure_05_footprint.py $(PLOT_COMMON)
	python3 ../artifact/figures/figure_05_footprint.py data/package-footprint.csv --packages --output figures/figure-05-footprint.pdf

figures/figure-06-update.pdf: data/figure-06-update.csv data/figure-06-stages.csv \
	data/figure-06-update.json render_update.py $(PLOT_COMMON)
	python3 render_update.py

figures/figure-07-performance.pdf: data/figure-07-operators.csv \
	../artifact/figures/figure_07_performance.py $(PLOT_COMMON)
	python3 ../artifact/figures/figure_07_performance.py data/figure-07-operators.csv --output figures/figure-07-performance.pdf --summary data/figure-07-operators-summary.csv

figures/figure-07-models.pdf: data/figure-07-models.csv \
	../artifact/figures/figure_07_models.py ../artifact/figures/figure_07_performance.py \
	../artifact/merge_benchmark_rows.py ../artifact/validate_figure.py \
	../artifact/manifests/benchmark-cases.json $(PLOT_COMMON)
	python3 ../artifact/figures/figure_07_models.py data/figure-07-models.csv --output figures/figure-07-models.pdf --summary data/figure-07-models-summary.csv

TASK_GRAPHS = $(patsubst %.dot,%.pdf,$(wildcard figures/task-*.dot))
A5_GRAPHS = $(patsubst %.dot,%.pdf,$(wildcard figures/a5-*.dot))

figures/a5-%.pdf: figures/a5-%.dot
	dot -Tpdf $< -o $@

figures/task-%.pdf: figures/task-%.dot
	dot -Tpdf $< -o $@

figures/agent-gelu-before.pdf: figures/agent-gelu-before.dot
	dot -Tpdf $< -o $@

figures/agent-gelu-after.pdf: figures/agent-gelu-after.dot
	dot -Tpdf $< -o $@

figures/agent-gelu-before.svg: figures/agent-gelu-before.dot
	dot -Tsvg $< -o $@

figures/agent-gelu-after.svg: figures/agent-gelu-after.dot
	dot -Tsvg $< -o $@

sigconf.pdf: sigconf.tex references.bib \
	../artifact/extensions/vert-fused-op/reference.jog \
	sections/01-introduction.tex sections/02-motivation.tex sections/03-design.tex \
	sections/04-evaluation.tex sections/05-related-work.tex \
	sections/06-discussion.tex sections/07-conclusion.tex \
	figures/figure-01-thesis-map.png figures/figure-02-workflow.png \
	figures/figure-03-operator-extension.png figures/figure-07-performance.pdf \
	figures/system-architecture.png figures/reactive-update.png \
	figures/evaluation-workflow.png figures/figure-07-models.pdf \
	figures/figure-06-update.pdf figures/figure-05-footprint.pdf figures/extension-size.pdf
	latexmk -pdf -interaction=nonstopmode -halt-on-error sigconf.tex

supplement.pdf: supplement.tex $(A5_GRAPHS) sections/appendix-operator-data.tex \
	../artifact/extensions/con-gelu-expand/reference.jog \
	../artifact/extensions/vert-fused-op/reference.jog \
	sections/appendix-model-data.tex \
	sections/appendix-update-data.tex \
	sections/appendix-package-data.tex \
	sections/appendix-graph-cases.tex $(TASK_GRAPHS) \
	sections/appendix-extension-data.tex figures/agent-gelu-before.pdf \
	figures/agent-gelu-after.pdf
	latexmk -pdf -interaction=nonstopmode -halt-on-error supplement.tex

clean:
	latexmk -C sigconf.tex
	latexmk -C supplement.tex
