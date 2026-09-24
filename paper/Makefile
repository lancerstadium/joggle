.PHONY: all clean

# Authoring order: revise README.md, synchronize the TeX section, then build.
all: sigconf.pdf supplement.pdf

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
	figures/evaluation-workflow.png figures/figure-07-models.pdf
	latexmk -pdf -interaction=nonstopmode -halt-on-error sigconf.tex

supplement.pdf: supplement.tex sections/appendix-operator-data.tex \
	sections/appendix-model-data.tex \
	sections/appendix-extension-data.tex figures/agent-gelu-before.pdf \
	figures/agent-gelu-after.pdf
	latexmk -pdf -interaction=nonstopmode -halt-on-error supplement.tex

clean:
	latexmk -C sigconf.tex
	latexmk -C supplement.tex
