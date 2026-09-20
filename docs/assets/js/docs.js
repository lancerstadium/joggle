(() => {
  const root = document.documentElement;
  const toggle = document.querySelector(".theme-toggle");
  const icon = document.querySelector("#site-icon");
  const systemDark = () => window.matchMedia("(prefers-color-scheme: dark)").matches;
  const dark = () => root.dataset.theme === "dark" || (!root.dataset.theme && systemDark());

  const syncToggle = () => {
    if (!toggle) return;
    toggle.setAttribute("aria-label", dark() ? "Use light theme" : "Use dark theme");
    toggle.querySelector("span").textContent = dark() ? "☀" : "☾";
    if (icon) {
      icon.href = icon.href.replace(
        /joggle-(?:light|dark)\.svg$/,
        dark() ? "joggle-dark.svg" : "joggle-light.svg"
      );
    }
  };

  toggle?.addEventListener("click", () => {
    root.dataset.theme = dark() ? "light" : "dark";
    try { localStorage.setItem("joggle-theme", root.dataset.theme); } catch (_) {}
    window.location.reload();
  });
  window.matchMedia("(prefers-color-scheme: dark)").addEventListener("change", syncToggle);
  syncToggle();

  const navFilter = document.querySelector("#nav-filter");
  navFilter?.addEventListener("input", () => {
    const query = navFilter.value.trim().toLowerCase();
    document.querySelectorAll(".nav-group").forEach((group) => {
      let visible = 0;
      group.querySelectorAll("a").forEach((link) => {
        const matches = !query || link.textContent.toLowerCase().includes(query);
        link.hidden = !matches;
        if (matches) visible += 1;
      });
      group.hidden = Boolean(query) && visible === 0;
      group.querySelectorAll(".nav-folder").forEach((folder) => {
        const matches = [...folder.querySelectorAll("a")].some((link) => !link.hidden);
        folder.hidden = Boolean(query) && !matches;
        if (query && matches) folder.open = true;
      });
    });
  });
  document.addEventListener("keydown", (event) => {
    if (event.key !== "/" || event.metaKey || event.ctrlKey || event.altKey) return;
    if (event.target.matches("input, textarea, select")) return;
    event.preventDefault();
    navFilter?.focus();
  });

  const diagrams = [...document.querySelectorAll("pre code.language-mermaid")];
  if (diagrams.length) {
    diagrams.forEach((code) => {
      const diagram = document.createElement("div");
      diagram.className = "mermaid";
      diagram.textContent = code.textContent;
      code.parentElement.replaceWith(diagram);
    });
    import("https://cdn.jsdelivr.net/npm/mermaid@11/dist/mermaid.esm.min.mjs")
      .then(({ default: mermaid }) => {
        mermaid.initialize({ startOnLoad: false, theme: dark() ? "dark" : "neutral" });
        return mermaid.run({ querySelector: ".mermaid" });
      })
      .catch(() => {
        document.querySelectorAll(".mermaid").forEach((diagram) => diagram.classList.add("mermaid-failed"));
      });
  }

  document.querySelectorAll("blockquote").forEach((note) => {
    const first = note.querySelector("p");
    const match = first?.textContent.match(/^\[!(IMPORTANT|NOTE|TIP|WARNING|CAUTION)\]\s*/);
    if (!match) return;
    for (const node of first.childNodes) {
      if (node.nodeType !== Node.TEXT_NODE) continue;
      node.textContent = node.textContent.replace(/^\[![A-Z]+\]\s*/, "");
      break;
    }
    note.classList.add("admonition");
    note.dataset.kind = match[1];
  });

  document.querySelectorAll("pre").forEach((pre) => {
    if (pre.closest(".mermaid") || pre.closest(".code-shell")) return;
    const code = pre.querySelector("code");
    if (!code) return;
    const languageClass = [...code.classList].find((name) => name.startsWith("language-"));
    const language = languageClass?.slice("language-".length) || "text";
    if (language === "jog" && code.children.length === 0) {
      const escape = (value) => value
        .replaceAll("&", "&amp;")
        .replaceAll("<", "&lt;")
        .replaceAll(">", "&gt;");
      const token = /("(?:\\.|[^"\\])*"|\/\/[^\n]*|\b(?:mod|use|fn|local|let|var|for|in|if|else|return|true|false|nil)\b|\b(?:Mod|Fn|Blk|Op|Val|Ty|Attr|bool|str|int|i(?:8|16|32|64)|f(?:16|32|64)|tensor)\b|\b\d+(?:\.\d+)?\b)/g;
      let cursor = 0;
      let html = "";
      for (const match of code.textContent.matchAll(token)) {
        html += escape(code.textContent.slice(cursor, match.index));
        const value = match[0];
        let kind = "keyword";
        if (value.startsWith('"')) kind = "string";
        else if (value.startsWith("//")) kind = "comment";
        else if (/^\d/.test(value)) kind = "number";
        else if (/^(?:Mod|Fn|Blk|Op|Val|Ty|Attr|bool|str|int|i\d+|f\d+|tensor)$/.test(value)) kind = "type";
        html += `<span class="tok-${kind}">${escape(value)}</span>`;
        cursor = match.index + value.length;
      }
      html += escape(code.textContent.slice(cursor));
      code.innerHTML = html;
    }
    const toolbar = document.createElement("div");
    toolbar.className = "code-toolbar";
    toolbar.innerHTML = `<span class="code-language">${language}</span>`;
    const copy = document.createElement("button");
    copy.className = "copy-code";
    copy.type = "button";
    copy.textContent = "Copy";
    copy.setAttribute("aria-label", "Copy code");
    copy.addEventListener("click", async () => {
      try {
        await navigator.clipboard.writeText(code.textContent);
        copy.textContent = "Copied";
        window.setTimeout(() => { copy.textContent = "Copy"; }, 1400);
      } catch (_) {
        copy.textContent = "Select to copy";
      }
    });
    toolbar.appendChild(copy);

    const highlight = pre.parentElement?.classList.contains("highlight") ? pre.parentElement : null;
    if (highlight) {
      highlight.classList.add("code-shell");
      highlight.prepend(toolbar);
    } else {
      const shell = document.createElement("div");
      shell.className = "code-shell";
      pre.replaceWith(shell);
      shell.append(toolbar, pre);
    }
  });

  const article = document.querySelector("article");
  const toc = document.querySelector("#toc");
  if (!article || !toc) return;

  const headings = [...article.querySelectorAll("h2, h3")];
  if (headings.length < 2) {
    document.querySelector(".toc-shell")?.classList.add("empty");
    return;
  }

  headings.forEach((heading, index) => {
    if (!heading.id) heading.id = `section-${index + 1}`;
    const link = document.createElement("a");
    link.href = `#${heading.id}`;
    link.textContent = heading.textContent;
    if (heading.tagName === "H3") link.className = "toc-sub";
    toc.appendChild(link);
  });
})();
