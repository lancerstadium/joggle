#!/usr/bin/env python3
"""Render the evidence-bounded Markdown draft as an anonymous ACM PDF source."""

from __future__ import annotations

import argparse
import re
from pathlib import Path

from markdown_it import MarkdownIt
from markdown_it.token import Token


CITATIONS = {
    "https://doi.org/10.1109/CGO51591.2021.9370308": "lattner2021mlir",
    "https://www.usenix.org/conference/osdi18/presentation/chen": "chen2018tvm",
    "https://arxiv.org/abs/2008.08272": "jin2020onnxmlir",
    "https://doi.org/10.1109/MM.2022.3178068": "liu2022tinyiree",
    "https://doi.org/10.1109/CGO.2017.7863730": "steuwer2017lift",
    "https://arxiv.org/abs/2201.03611": "steuwer2022rise",
    "https://arxiv.org/abs/2504.17577": "wang2025tilelang",
    "https://doi.org/10.1145/2491956.2462176": "ragankelley2013halide",
    "https://www.usenix.org/conference/osdi20/presentation/zheng": "zheng2020ansor",
    "https://arxiv.org/abs/2405.05751": "wu2024mirage",
    "https://arxiv.org/abs/2606.26344": "kothari2026axon",
    "https://proceedings.mlsys.org/paper_files/paper/2021/file/6c44dc73014d66ba49b28d483a8f8b0d-Paper.pdf": "david2021tflm",
    "https://www.usenix.org/conference/osdi22/presentation/zhu": "zhu2022roller",
    "https://www.usenix.org/conference/osdi23/presentation/shi": "shi2023welder",
    "https://www.usenix.org/conference/osdi24/presentation/wang-lei": "wang2024ladder",
    "https://doi.org/10.1145/3575693.3575702": "ding2023hidet",
    "https://doi.org/10.1145/3519939.3523446": "ikarashi2022exo",
    "https://doi.org/10.1145/3460945.3464953": "smith2021glenside",
    "https://doi.org/10.1145/3341301.3359630": "jia2019taso",
    "https://arxiv.org/abs/2101.01332": "yang2021tensat",
    "https://www.usenix.org/conference/osdi20/presentation/ma": "ma2020rammer",
    "https://doi.org/10.1109/TC.2021.3066883": "burrello2021dory",
    "https://proceedings.neurips.cc/paper_files/paper/2020/hash/86c51678350f656dcc7f490a43946ee5-Abstract.html": "lin2020mcunet",
    "https://arxiv.org/abs/1805.00907": "rotem2018glow",
    "https://doi.org/10.1109/CGO.2019.8661197": "baghdadi2019tiramisu",
    "https://doi.org/10.1145/3575693.3576933": "feng2023tensorir",
    "https://proceedings.mlsys.org/paper_files/paper/2022/hash/1f8053a67ec8e0b57455713cefdd8218-Abstract.html": "xing2022bolt",
    "https://doi.org/10.1145/3575693.3575707": "liu2023nnsmith",
    "https://doi.org/10.1145/3597926.3598053": "ma2023hirgen",
    "https://doi.org/10.1109/ISSRE52982.2021.00030": "zheng2021compilerbugs",
    "https://doi.org/10.1145/3489048.3522655": "xiao2022metamorphic",
    "https://arxiv.org/abs/2311.02103": "lai2025relax",
    "https://doi.org/10.1145/3839457": "jain2026act",
    "https://arxiv.org/abs/2604.13523": "gao2026atlaas",
    "https://proceedings.mlsys.org/paper_files/paper/2025/hash/dbf02b21d77409a2db30e56866a8ab3a-Abstract-Conference.html": "ye2025flashinfer",
    "https://proceedings.mlsys.org/paper_files/paper/2025/hash/8cb5b08f912600de3de07c6503599ba8-Abstract-Conference.html": "daghero2025sparse",
    "https://tvm.apache.org/docs/how_to/tutorials/bring_your_own_codegen.html": "tvmByocDocs",
    "https://tvm.apache.org/docs/install/from_source.html": "tvmBuildDocs",
    "https://onnx.ai/onnx-mlir/AddCustomAccelerators.html": "onnxmlirAccelDocs",
    "https://onnx.ai/onnx-mlir/BuildOnLinuxOSX.html": "onnxmlirBuildDocs",
    "https://iree.dev/": "ireeDocs",
}


def escape(text: str) -> str:
    replacements = {
        "\\": r"\textbackslash{}",
        "{": r"\{",
        "}": r"\}",
        "$": r"\$",
        "&": r"\&",
        "%": r"\%",
        "#": r"\#",
        "_": r"\_",
        "~": r"\textasciitilde{}",
        "^": r"\textasciicircum{}",
        "≥": r"$\geq$",
        "≤": r"$\leq$",
        "×": r"$\times$",
        "→": r"$\rightarrow$",
    }
    return "".join(replacements.get(character, character) for character in text)


def inline(token: Token) -> str:
    output: list[str] = []
    suppressed_link: str | None = None
    for child in token.children or []:
        if child.type == "link_open":
            href = child.attrGet("href") or ""
            citation = CITATIONS.get(href)
            if citation:
                output.append(rf"\cite{{{citation}}}")
                suppressed_link = href
            else:
                output.append(rf"\href{{{escape(href)}}}{{")
        elif child.type == "link_close":
            if suppressed_link is not None:
                suppressed_link = None
            else:
                output.append("}")
        elif suppressed_link is not None:
            continue
        elif child.type == "text":
            output.append(escape(child.content))
        elif child.type == "code_inline":
            output.append(r"\nolinkurl{" + child.content.replace("}", r"\}") + "}")
        elif child.type == "strong_open":
            output.append(r"\textbf{")
        elif child.type == "strong_close":
            output.append("}")
        elif child.type == "em_open":
            output.append(r"\emph{")
        elif child.type == "em_close":
            output.append("}")
        elif child.type in {"softbreak", "hardbreak"}:
            output.append("\n")
        else:
            raise ValueError(f"unsupported inline token: {child.type}")
    return "".join(output)


def plain(token: Token) -> str:
    return "".join(
        child.content
        for child in token.children or []
        if child.type in {"text", "code_inline"}
    )


def figure(token: Token) -> str | None:
    children = token.children or []
    if len(children) != 1 or children[0].type != "image":
        return None
    source = children[0].attrGet("src") or ""
    if not re.fullmatch(r"[A-Za-z0-9_./-]+\.pdf", source) or ".." in source:
        raise ValueError(f"figure must be a repository-local PDF: {source}")
    caption = children[0].content
    label = "fig:" + re.sub(r"[^a-z0-9]+", "-", caption.lower()).strip("-")
    return "\n".join([
        r"\begin{figure*}[t]",
        r"\centering",
        rf"\includegraphics[width=\textwidth]{{../{source}}}",
        rf"\caption{{{escape(caption)}}}",
        rf"\label{{{label}}}",
        r"\end{figure*}",
    ])


def table(tokens: list[Token], start: int, caption: str | None) -> tuple[str, int]:
    rows: list[list[str]] = []
    current: list[str] | None = None
    index = start + 1
    while tokens[index].type != "table_close":
        token = tokens[index]
        if token.type == "tr_open":
            current = []
        elif token.type == "tr_close":
            if current is None:
                raise ValueError("table row closed before it opened")
            rows.append(current)
            current = None
        elif token.type == "inline" and current is not None:
            current.append(inline(token))
        index += 1
    if not rows or not rows[0]:
        raise ValueError("empty table")
    columns = len(rows[0])
    if any(len(row) != columns for row in rows):
        raise ValueError("ragged table")
    label = "tab:" + re.sub(r"[^a-z0-9]+", "-", (caption or "table").lower()).strip("-")
    alignment = "@{}" + "l" + "X" * (columns - 1) + "@{}"
    body = [
        r"\begin{table*}[t]",
        r"\centering",
        r"\scriptsize",
        rf"\caption{{{escape(caption or 'Results')}}}",
        rf"\label{{{label}}}",
        rf"\begin{{tabularx}}{{\textwidth}}{{{alignment}}}",
        r"\toprule",
        " & ".join(rows[0]) + r" \\",
        r"\midrule",
    ]
    body.extend(" & ".join(row) + r" \\" for row in rows[1:])
    body.extend([r"\bottomrule", r"\end{tabularx}", r"\end{table*}"])
    return "\n".join(body), index


def render(source: str) -> tuple[str, int]:
    tokens = MarkdownIt("commonmark").enable("table").parse(source)
    title = plain(tokens[1])
    lines: list[str] = []
    abstract: list[str] = []
    in_abstract = False
    pending_caption: str | None = None
    index = 0
    while index < len(tokens):
        token = tokens[index]
        if token.type == "heading_open":
            level = int(token.tag[1])
            content = plain(tokens[index + 1])
            if level == 1:
                index += 3
                continue
            if level == 2 and content == "Abstract":
                in_abstract = True
                index += 3
                continue
            if level == 2:
                in_abstract = False
                content = re.sub(r"^\d+\.\s*", "", content)
                lines.append(rf"\section{{{escape(content)}}}")
            elif level == 3:
                content = re.sub(r"^\d+\.\d+\s*", "", content)
                lines.append(rf"\subsection{{{escape(content)}}}")
            else:
                lines.append(rf"\subsubsection{{{escape(content)}}}")
            index += 3
            continue
        if token.type == "paragraph_open":
            image = figure(tokens[index + 1])
            if image is not None:
                lines.extend([image, ""])
                index += 3
                continue
            rendered = inline(tokens[index + 1])
            raw = plain(tokens[index + 1])
            match = re.fullmatch(r"Table\s+\d+\.\s+(.+)", raw)
            if match:
                pending_caption = match.group(1).rstrip(".")
            elif in_abstract:
                abstract.append(rendered)
            elif not lines and raw.startswith("Working manuscript for"):
                pass
            else:
                lines.extend([rendered, ""])
            index += 3
            continue
        if token.type == "table_open":
            rendered, index = table(tokens, index, pending_caption)
            pending_caption = None
            lines.extend([rendered, ""])
        elif token.type == "ordered_list_open":
            lines.append(r"\begin{enumerate}")
        elif token.type == "ordered_list_close":
            lines.extend([r"\end{enumerate}", ""])
        elif token.type == "bullet_list_open":
            lines.append(r"\begin{itemize}")
        elif token.type == "bullet_list_close":
            lines.extend([r"\end{itemize}", ""])
        elif token.type == "list_item_open":
            lines.append(r"\item")
        elif token.type == "html_block":
            pass
        elif token.type not in {
            "heading_close", "inline", "paragraph_close", "list_item_close",
            "thead_open", "thead_close", "tbody_open", "tbody_close", "tr_open",
            "tr_close", "th_open", "th_close", "td_open", "td_close", "table_close",
        }:
            raise ValueError(f"unsupported block token: {token.type}")
        index += 1

    preamble = rf"""\documentclass[sigplan,anonymous,review]{{acmart}}
\usepackage{{booktabs}}
\usepackage{{tabularx}}
\usepackage{{microtype}}
\settopmatter{{printfolios=true,printacmref=false}}
\setcopyright{{none}}
\renewcommand\footnotetextcopyrightpermission[1]{{}}
\acmConference[EuroSys '27]{{Twenty-Second European Conference on Computer Systems}}{{April 19--23, 2027}}{{Rabat, Morocco}}
\acmYear{{2027}}
\title[Joggle: Distributable Extensions]{{{escape(title)}}}
\author{{Anonymous Author(s)}}
\affiliation{{\institution{{Anonymous}}}}
\ccsdesc[500]{{Software and its engineering~Compilers}}
\ccsdesc[300]{{Computer systems organization~Embedded systems}}
\begin{{document}}
\begin{{abstract}}
{' '.join(abstract)}
\end{{abstract}}
\keywords{{inference compilation, compiler extensibility, co-design, intermediate representation, edge inference}}
\maketitle
"""
    ending = r"""
\bibliographystyle{ACM-Reference-Format}
\bibliography{../references}
\end{document}
"""
    return preamble + "\n".join(lines).rstrip() + "\n" + ending, len(tokens)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, default=Path("paper/manuscript.md"))
    parser.add_argument("--output", type=Path, default=Path("paper/submission/main.tex"))
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    source = args.source.read_text(encoding="utf-8")
    rendered, token_count = render(source)
    if args.check:
        if not args.output.is_file() or args.output.read_text(encoding="utf-8") != rendered:
            raise SystemExit("submission render is stale")
        print(f"submission render is current ({token_count} Markdown tokens)")
        return
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(rendered, encoding="utf-8")
    print(f"rendered {token_count} Markdown tokens to {args.output}")


if __name__ == "__main__":
    main()
