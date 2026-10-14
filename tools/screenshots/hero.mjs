// Composes the product image at the top of the README (docs/screenshots/hero-light.webp / hero-dark.webp):
// two Hamar windows in perspective over a warm night, strings of light running across it, with the logo and the key
// facts.
//
//   tools/screenshots/shoot.sh        # makes the raw page shots first, then calls this script
//   node hero.mjs                     # only the composition (raw shots in build/screenshots/raw)
//
// Requires Playwright (npm install in this folder) and cwebp.

import { chromium } from "playwright";
import { execFileSync } from "node:child_process";
import { mkdirSync, readdirSync, statSync, writeFileSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), "../..");
const RAW_DIR = join(ROOT, "build/screenshots/raw");
const PNG_DIR = join(ROOT, "build/screenshots/png");
const OUT_DIR = join(ROOT, "docs/screenshots");
const FONTS = join(ROOT, "../fjord-base/resources/fonts");

// Colours from the Brand in Source/PluginProcessor.cpp (ivory, brass, walnut)
const PALETTE = {
  light: { sky1: "#efe9dd", sky2: "#f6f2ea", glow: "rgba(184,145,63,0.28)", string: "rgba(138,101,38,0.35)", lit: "#b8913f",
           text: "#15253a", dim: "#6b6050", accent: "#8a6526", floor: "#e6dccb", edge: "rgba(0,0,0,0.08)", shadow: "rgba(60,40,15,0.30)" },
  dark:  { sky1: "#07090f", sky2: "#1a1712", glow: "rgba(230,200,138,0.24)", string: "rgba(230,200,138,0.22)", lit: "#f4e7c8",
           text: "#efe7d6", dim: "#9a8f7c", accent: "#e6c88a", floor: "#0b0a09", edge: "rgba(255,255,255,0.08)", shadow: "rgba(0,0,0,0.65)" },
};

function presetCount() {
  let n = 0;
  const walk = (dir) => {
    for (const f of readdirSync(dir)) {
      const p = join(dir, f);
      if (statSync(p).isDirectory()) walk(p);
      else if (f.endsWith(".hamar")) n++;
    }
  };
  walk(join(ROOT, "presets/Factory"));
  return n;
}

const img = (id, theme) => pathToFileURL(join(RAW_DIR, `${id}-${theme}.png`)).href;

// Strings of light across the picture, fanning like the strings of a grand; one of them glows
function strings(c) {
  const lines = [];
  for (let i = 0; i < 26; i++) {
    const y0 = 120 + i * 26, y1 = 60 + i * 34;
    const lit = i === 17;
    lines.push(`<line x1="-20" y1="${y0}" x2="1620" y2="${y1}" stroke="${lit ? c.lit : c.string}" stroke-width="${lit ? 2.4 : 1}"
                ${lit ? `style="filter: drop-shadow(0 0 6px ${c.lit}) drop-shadow(0 0 18px ${c.lit});"` : ""}/>`);
  }
  return `<svg class="strings" viewBox="0 0 1600 900" preserveAspectRatio="none">${lines.join("")}</svg>`;
}

function hero(theme) {
  const c = PALETTE[theme];
  const other = theme === "light" ? "dark" : "light";
  return `<!doctype html><meta charset="utf-8"><style>
  @font-face { font-family: Manrope; font-weight: 400; src: url(${pathToFileURL(join(FONTS, "Manrope-Regular.ttf")).href}); }
  @font-face { font-family: Manrope; font-weight: 500; src: url(${pathToFileURL(join(FONTS, "Manrope-Medium.ttf")).href}); }
  @font-face { font-family: Manrope; font-weight: 600; src: url(${pathToFileURL(join(FONTS, "Manrope-SemiBold.ttf")).href}); }
  * { box-sizing: border-box; margin: 0; padding: 0; }
  html, body { width: 100%; height: 100%; }
  body {
    font-family: Manrope, sans-serif; color: ${c.text}; overflow: hidden; position: relative;
    background:
      radial-gradient(560px 520px at 30% 46%, ${c.glow}, transparent 70%),
      radial-gradient(760px 460px at 72% 40%, ${c.glow}, transparent 75%),
      linear-gradient(180deg, ${c.sky1}, ${c.sky2} 70%, ${c.floor});
  }
  .strings { position: absolute; inset: 0; width: 100%; height: 100%; }
  .copy { position: absolute; left: 96px; top: 238px; width: 500px; z-index: 2; }
  .logo { font-size: 104px; font-weight: 500; letter-spacing: -3px; line-height: 1; }
  .logo span { color: ${c.accent}; }
  .tag { margin-top: 26px; font-size: 27px; line-height: 1.35; font-weight: 500; }
  .facts { margin-top: 30px; font-size: 15px; letter-spacing: 2.2px; text-transform: uppercase; color: ${c.dim};
           line-height: 2.1; font-weight: 600; }
  .facts b { color: ${c.accent}; font-weight: 600; }
  .scene { position: absolute; left: 600px; top: 110px; perspective: 2600px; }
  .window { display: block; position: absolute; width: 1100px; border-radius: 14px; transform-origin: left center;
            box-shadow: 0 0 0 1px ${c.edge}, 0 50px 90px -30px ${c.shadow}, 0 18px 36px -18px ${c.shadow}; }
  .back  { top: -150px; left: 190px; transform: rotateY(-18deg) rotateX(4deg) scale(0.9); opacity: 0.97; }
  .front { top: 70px; left: 0; transform: rotateY(-18deg) rotateX(4deg); }
  </style>
  ${strings(c)}
  <div class="copy">
    <div class="logo">hamar<span>.</span></div>
    <div class="tag">Pianos, modelled from the hammer up.<br>No samples - every string is alive.</div>
    <div class="facts">
      <b>36</b> instruments in <b>6</b> families<br>
      grand · upright · electric · historic · dance · fantasy<br>
      <b>${presetCount()}</b> factory presets<br>
      <b>VST3</b> · <b>AU</b> · <b>LV2</b> · standalone
    </div>
  </div>
  <div class="scene">
    <img class="window back" src="${img("electric", other)}">
    <img class="window front" src="${img("play", theme)}">
  </div>`;
}

mkdirSync(OUT_DIR, { recursive: true });
mkdirSync(PNG_DIR, { recursive: true });
const browser = await chromium.launch();
const page = await browser.newPage({ deviceScaleFactor: 1.5 });
try {
  for (const theme of ["light", "dark"]) {
    const htmlFile = join(RAW_DIR, `_hero-${theme}.html`);
    writeFileSync(htmlFile, hero(theme));
    await page.setViewportSize({ width: 1600, height: 900 });
    await page.goto(pathToFileURL(htmlFile).href);
    await page.evaluate(() => document.fonts.ready);
    await page.waitForFunction(() => [...document.images].every((i) => i.complete && i.naturalWidth > 0));
    const png = join(PNG_DIR, `hero-${theme}.png`);
    const out = join(OUT_DIR, `hero-${theme}.webp`);
    await page.screenshot({ path: png });
    execFileSync("cwebp", ["-quiet", "-q", "88", "-m", "6", png, "-o", out]);
    console.log(`hero  ${out.replace(ROOT + "/", "")} (${Math.round(statSync(out).size / 1024)} KB)`);
  }
} finally {
  await browser.close();
}
