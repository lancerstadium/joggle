.PHONY: all clean

# Authoring order: revise README.md, synchronize the TeX section, then build.
all: sigconf.pdf

sigconf.pdf: sigconf.tex references.bib \
	sections/01-introduction.tex sections/02-motivation.tex sections/03-design.tex \
	figures/figure-01-thesis-map.png figures/figure-02-workflow.png \
	figures/figure-03-call-model.png figures/figure-04-dependencies.png
	latexmk -pdf -interaction=nonstopmode -halt-on-error sigconf.tex

clean:
	latexmk -C sigconf.tex
