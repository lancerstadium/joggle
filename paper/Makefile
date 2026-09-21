.PHONY: all clean

# Authoring order: revise README.md, synchronize the TeX section, then build.
all: joggle.pdf

joggle.pdf: joggle.tex sections/03-design.tex figures/figure-02-system-model.png figures/figure-03-dependencies.png
	latexmk -pdf -interaction=nonstopmode -halt-on-error joggle.tex

clean:
	latexmk -C joggle.tex
