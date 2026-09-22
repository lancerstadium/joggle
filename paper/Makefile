.PHONY: all clean

# Authoring order: revise README.md, synchronize the TeX section, then build.
all: sigconf.pdf

sigconf.pdf: sigconf.tex references.bib \
	sections/01-introduction.tex sections/02-motivation.tex sections/03-design.tex \
	sections/04-evaluation.tex sections/05-related-work.tex \
	sections/06-discussion.tex sections/07-conclusion.tex \
	sections/appendix-operator-data.tex \
	figures/figure-01-thesis-map.png figures/figure-02-workflow.png \
	figures/figure-03-operator-extension.png figures/figure-07-performance.pdf
	latexmk -pdf -interaction=nonstopmode -halt-on-error sigconf.tex

clean:
	latexmk -C sigconf.tex
