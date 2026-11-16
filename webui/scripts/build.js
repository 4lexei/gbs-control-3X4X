const fs = require("fs");
const i18n = require("./i18n");
// 3X4X: the language is chosen here: "node build.js" builds the web in Spanish, "node build.js en" in English (from
// lang/en.json). Each firmware carries one language only.
const LANG = (process.argv[2] || process.env.LANG_WEB || "es").toLowerCase();
let html = fs.readFileSync("./../src/index.html.tpl", "utf-8");
let js = fs.readFileSync("./../src/index.js", "utf-8");
let cssText = fs.readFileSync("./../src/style.css", "utf-8");
if (LANG !== "es") {
  const lang = i18n.loadLanguage(LANG);
  const usedPage = new Set();
  const usedScript = new Set();
  html = i18n.translatePage(html, lang.page, usedPage);
  cssText = i18n.translateCss(cssText, lang.page, usedPage);
  js = i18n.translateScript(js, lang.script, usedScript);
  const left = [...new Set(i18n.pageTexts(html).concat(i18n.cssTexts(cssText), i18n.scriptTexts(js)))].filter(i18n.looksSpanish);
  const unused = Object.keys(lang.page).filter((k) => !usedPage.has(k))
    .concat(Object.keys(lang.script).filter((k) => !usedScript.has(k)));
  i18n.report(LANG, left, unused);
}
html = html.replace(/<html lang="[^"]*">/, `<html lang="${LANG}">`);

const icon1024 = fs
  .readFileSync("./../assets/icons/icon-1024-maskable.png")
  .toString("base64");
const oswald = fs
  .readFileSync("./../assets/fonts/oswald.woff2")
  .toString("base64");
const material = fs
  .readFileSync("./../assets/fonts/material.woff2")
  .toString("base64");
const favicon = fs
  .readFileSync("./../assets/icons/gbsc-logo.png")
  .toString("base64");

const css = cssText
  .replace("${oswald}", oswald)
  .replace("${material}", material);

const manifest = fs
  .readFileSync("./../src/manifest.json", "utf-8")
  .replace(/\$\{icon1024\}/g, `data:image/png;base64,${icon1024}`);

const OUT = LANG === "es" ? "webui" : `webui_${LANG}`; // webui.html / webui_en.html
fs.writeFileSync(
  `./../build/${OUT}.html`,
  html
    .replace("${styles}", css)
    .replace("${js}", () => js) // a function, so a "$" in the script is never read as a pattern
    .replace("${favicon}", `data:image/png;base64,${favicon}`)
    .replace(
      "${manifest}",
      `data:application/json;base64,${Buffer.from(manifest).toString("base64")}`
    )
    .replace("${icon1024}", `data:image/png;base64,${icon1024}`),
  "utf8"
);

console.log(`${OUT}.html GENERATED in webui/build/ (language: ${LANG})`);
