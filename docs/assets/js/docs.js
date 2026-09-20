(() => {
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
