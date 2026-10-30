// Render the checked-in official SVGs for Qt's PNG-only deployment (optional asset maintenance step).
// Uses Playwright from KANTAN_NODE_MODULES (or the global install) and Chrome/Chromium from KANTAN_CHROME
// (or Playwright's bundled browser).
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const modules = process.env.KANTAN_NODE_MODULES;
const { chromium } = require(modules ? path.join(modules, 'playwright') : 'playwright');
(async () => {
  const options = { headless: true };
  if (process.env.KANTAN_CHROME) options.executablePath = process.env.KANTAN_CHROME;
  const browser = await chromium.launch(options);
  const root = path.resolve(__dirname, '../app/assets/icons');
  const svgs = fs.readdirSync(root).filter(f => f.endsWith('.svg')).sort();
  for (const scale of [1, 2, 3]) {
    const page = await browser.newPage({ viewport: { width: 24, height: 24 }, deviceScaleFactor: scale });
    for (const file of svgs) {
      const svg = fs.readFileSync(path.join(root, file), 'utf8').replaceAll('#212121', '#e1e6ee');
      await page.setContent('<style>html,body{margin:0;background:transparent}svg{display:block}</style>' + svg);
      await page.screenshot({ path: path.join(root, file.replace('.svg', scale === 1 ? '.png' : `@${scale}x.png`)), omitBackground: true });
    }
    await page.close();
  }
  await browser.close();
  const hashes = svgs.map(file => ({ file, sha256: crypto.createHash('sha256').update(fs.readFileSync(path.join(root, file))).digest('hex') }));
  fs.writeFileSync(path.join(root, 'source-hashes.json'), JSON.stringify(hashes, null, 2) + '\n');
})();
