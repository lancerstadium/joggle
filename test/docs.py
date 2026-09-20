#!/usr/bin/env python3
"""Validate the local Markdown graph used by the README and GitHub Pages."""

from __future__ import annotations

import re
import sys
from pathlib import Path


LINK = re.compile(r"\[[^]]+\]\(([^)]+)\)")


def main() -> int:
    root = Path(sys.argv[1] if len(sys.argv) > 1 else ".").resolve()
    docs = root / "docs"
    documents = [root / "README.md", *sorted(docs.rglob("*.md"))]
    failures: list[str] = []
    for document in documents:
        source = document.read_text(encoding="utf-8")
        if "{{" in source or "{%" in source:
            failures.append(
                f"{document.relative_to(root)}: unescaped Liquid delimiter"
            )
        for target in LINK.findall(source):
            if target.startswith(("http://", "https://", "mailto:", "#")):
                continue
            path = target.split("#", 1)[0]
            if not path:
                continue
            resolved = (document.parent / path).resolve()
            if not resolved.exists():
                failures.append(f"{document.relative_to(root)}: missing {target}")
            elif document.is_relative_to(docs) and not resolved.is_relative_to(docs):
                failures.append(
                    f"{document.relative_to(root)}: unpublished local target {target}"
                )

    guide_index = (docs / "guides" / "index.md").read_text(encoding="utf-8")
    for guide in sorted((docs / "guides").glob("*.md")):
        if guide.name == "index.md":
            continue
        if guide.name not in guide_index:
            failures.append(
                f"docs/guides/index.md: guide is not indexed: {guide.name}"
            )

    for required in (
        docs / "_layouts" / "default.html",
        docs / "_data" / "navigation.yml",
        docs / "assets" / "css" / "style.css",
        docs / "assets" / "js" / "docs.js",
        docs / "assets" / "logo" / "joggle-light.svg",
        docs / "assets" / "logo" / "joggle-dark.svg",
    ):
        if not required.exists():
            failures.append(
                f"docs: missing site asset: {required.relative_to(docs)}"
            )

    layout = (docs / "_layouts" / "default.html").read_text(encoding="utf-8")
    script = (docs / "assets" / "js" / "docs.js").read_text(encoding="utf-8")
    stylesheet = (docs / "assets" / "css" / "style.css").read_text(
        encoding="utf-8"
    )
    for contract, text in (
        ("theme-toggle", layout),
        ("nav-folder", layout),
        ("nav-filter", layout),
        ("joggle-theme", script),
        ("language-mermaid", script),
        ("navigator.clipboard", script),
        ("copy-code", script),
        ("navFilter", script),
        ('data-theme="dark"', stylesheet),
        ("code-toolbar", stylesheet),
    ):
        if contract not in text:
            failures.append(f"docs: missing site contract: {contract}")

    config = (docs / "_config.yml").read_text(encoding="utf-8")
    if "jekyll-relative-links" not in config or "relative_links:" not in config:
        failures.append(
            "docs/_config.yml: Markdown links need jekyll-relative-links"
        )

    workflow = (root / ".github" / "workflows" / "pages.yml").read_text(
        encoding="utf-8"
    )
    for required in (
        "source: docs",
        "actions/jekyll-build-pages@",
        "actions/upload-pages-artifact@",
        "actions/deploy-pages@",
        "actions: read",
        "pages: write",
        "id-token: write",
    ):
        if required not in workflow:
            failures.append(f"pages.yml: missing deployment contract: {required}")

    if failures:
        print("\n".join(failures), file=sys.stderr)
        return 1
    print(f"documentation graph: ok ({len(documents)} Markdown files)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
