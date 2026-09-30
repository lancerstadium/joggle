// Run against the actual Jekyll output; no application or external server needed.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const http = require('node:http');
const path = require('node:path');
const { chromium } = require('playwright');

const directory = path.resolve(process.argv[2] || 'local/site');
const index = fs.readFileSync(path.join(directory, 'index.html'), 'utf8');
const base = index.match(/href="([^"]*)\/assets\/css\/style\.css"/)[1];
const types = { '.html': 'text/html', '.css': 'text/css', '.js': 'text/javascript', '.svg': 'image/svg+xml' };
const pages = [];
function collect(dir) {
  for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
    const file = path.join(dir, entry.name);
    if (entry.isDirectory()) collect(file);
    else if (entry.name.endsWith('.html')) pages.push('/' + path.relative(directory, file).split(path.sep).join('/'));
  }
}
collect(directory);

const server = http.createServer((request, response) => {
  let url = decodeURIComponent(new URL(request.url, 'http://localhost').pathname);
  if (base && !url.startsWith(base + '/')) { response.writeHead(404).end(); return; }
  url = url.slice(base.length);
  if (url.endsWith('/')) url += 'index.html';
  const file = path.resolve(directory, '.' + url);
  if (!file.startsWith(directory + path.sep) || !fs.existsSync(file) || !fs.statSync(file).isFile()) {
    response.writeHead(404).end(); return;
  }
  response.setHeader('Content-Type', types[path.extname(file)] || 'application/octet-stream');
  fs.createReadStream(file).pipe(response);
});

(async () => {
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  const origin = `http://127.0.0.1:${server.address().port}`;
  let browser;
  try {
    browser = await chromium.launch({ headless: true, channel: process.env.JOGGLE_BROWSER_CHANNEL || undefined });
    for (const [width, colorScheme] of [[320, 'light'], [390, 'dark'], [768, 'light'], [1440, 'dark']]) {
      const context = await browser.newContext({ viewport: { width, height: 850 }, colorScheme });
      // Responsive validation is offline; unavailable Mermaid falls back to code.
      await context.route('**/*', route => route.request().url().startsWith(origin) ? route.continue() : route.abort());
      const page = await context.newPage();
      const errors = [];
      page.on('pageerror', error => errors.push(error.message));
      let testedNavigation = false;
      for (const route of pages) {
        await page.goto(origin + base + route, { waitUntil: 'load' });
        await page.evaluate(() => document.fonts.ready);
        const size = await page.evaluate(() => ({ width: document.documentElement.clientWidth, scroll: document.documentElement.scrollWidth }));
        if (size.scroll > size.width + 1) {
          fs.mkdirSync('local/site-checks', { recursive: true });
          await page.screenshot({ path: `local/site-checks/overflow-${width}.png` });
          console.error(await page.locator('body *').evaluateAll(elements => elements
            .filter(element => element.getBoundingClientRect().right > document.documentElement.clientWidth + 1)
            .slice(0, 12).map(element => ({ tag: element.tagName, class: element.className, text: element.textContent.slice(0, 70) }))));
        }
        if (route.includes('/guides/quantized-c')) {
          fs.mkdirSync('local/site-checks', { recursive: true });
          await page.screenshot({ path: `local/site-checks/quantized-${width}-${colorScheme}.png` });
        }
        assert.ok(size.scroll <= size.width + 1, `${route} overflows at ${width}px: ${size.scroll}`);
        if (width < 761 && await page.locator('.nav-toggle').count()) {
          assert.equal(await page.locator('.sidebar').isVisible(), false, `${route}: menu starts closed`);
          if (!testedNavigation) {
            await page.locator('.nav-toggle').click();
            assert.equal(await page.locator('.sidebar').isVisible(), true);
            await page.locator('#nav-filter').fill('quant');
            assert.ok(await page.locator('.nav-group a:visible').count() > 0);
            await page.locator('#nav-filter').press('Escape');
            assert.equal(await page.locator('.sidebar').isVisible(), false);
            testedNavigation = true;
          }
        }
      }
      assert.deepEqual(errors, [], 'uncaught browser errors');
      console.log(`${pages.length} pages: ${width}px ${colorScheme}, no viewport overflow`);
      await context.close();
    }
  } finally {
    await browser?.close();
    server.close();
  }
})().catch(error => { console.error(error); process.exitCode = 1; });
