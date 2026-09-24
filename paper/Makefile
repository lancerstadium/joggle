.PHONY: all figures clean

# Authoring order: revise README.md, synchronize the TeX section, then build.
all: sigconf.pdf supplement.pdf

# Rebuild the three measured result figures without rerunning benchmarks.
# All use the same single-column physical size and font scale.
figures:
	python3 render_update.py
	python3 ../artifact/figures/figure_07_performance.py data/figure-07-operators.csv --output figures/figure-07-performance.pdf --summary data/figure-07-operators-summary.csv --font-size 5.5
	python3 ../artifact/figures/figure_07_models.py data/figure-07-models.csv --output figures/figure-07-models.pdf --summary data/figure-07-models-summary.csv --font-size 5.5

TASK_GRAPHS = $(patsubst %.dot,%.pdf,$(wildcard figures/task-*.dot))

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
	sections/01-introduction.tex sections/02-motivation.tex sections/03-design.tex \
	sections/04-evaluation.tex sections/05-related-work.tex \
	sections/06-discussion.tex sections/07-conclusion.tex \
	figures/figure-01-thesis-map.png figures/figure-02-workflow.png \
	figures/figure-03-operator-extension.png figures/figure-07-performance.pdf \
	figures/system-architecture.png figures/reactive-update.png \
	figures/evaluation-workflow.png figures/figure-07-models.pdf \
	figures/figure-06-update.pdf
	latexmk -pdf -interaction=nonstopmode -halt-on-error sigconf.tex

supplement.pdf: supplement.tex sections/appendix-operator-data.tex \
	sections/appendix-model-data.tex \
	sections/appendix-update-data.tex \
	sections/appendix-graph-cases.tex $(TASK_GRAPHS) \
	sections/appendix-extension-data.tex figures/agent-gelu-before.pdf \
	figures/agent-gelu-after.pdf
	latexmk -pdf -interaction=nonstopmode -halt-on-error supplement.tex

clean:
	latexmk -C sigconf.tex
	latexmk -C supplement.tex
