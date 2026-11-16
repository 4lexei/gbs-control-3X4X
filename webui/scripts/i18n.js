// 3X4X: the web is written in Spanish. A language file (webui/lang/<code>.json) turns it into another language
// when the web is built, so each firmware carries one language only.
//
// The language file has two lists, each one "Spanish text": "translated text":
//   "page"   - texts of the page itself (index.html.tpl and the CSS): button labels, help paragraphs, confirmations.
//   "script" - texts the script writes on the screen (index.ts): mode descriptions, notes, messages, the status bar.
// Only whole texts are replaced, exactly as they are in the source. A text not in the list stays in Spanish, and the
// build lists it so it can be added. Keys starting with "/" in "script" replace a regular expression of the script
// (the confirmations are recognised by their wording, so their patterns have to follow the language).
const fs = require("fs");

let ts = null;
const loadTs = () => {
  if (ts) return ts;
  try {
    ts = require("typescript");
  } catch (e) {
    console.error("The language build needs TypeScript: run 'npm install' in webui/ first.");
    process.exit(1);
  }
  return ts;
};

const escTemplate = (s) => s.replace(/\\/g, "\\\\").replace(/`/g, "\\`").replace(/\$\{/g, "\\${");

// replaces the script's string literals, template pieces and regular expressions found in the list
const translateScript = (src, map, used) => {
  const t = loadTs();
  const sf = t.createSourceFile("index.js", src, t.ScriptTarget.ES2017, true);
  const edits = [];
  const take = (key) => {
    if (!Object.prototype.hasOwnProperty.call(map, key)) return null;
    used.add(key);
    return map[key];
  };
  const visit = (n) => {
    if (t.isStringLiteral(n)) {
      const v = take(n.text);
      if (v !== null) edits.push([n.getStart(), n.getEnd(), JSON.stringify(v)]);
    } else if (t.isNoSubstitutionTemplateLiteral(n)) {
      const v = take(n.text);
      if (v !== null) edits.push([n.getStart(), n.getEnd(), "`" + escTemplate(v) + "`"]);
    } else if (t.isTemplateExpression(n)) {
      const v = take(n.head.text);
      if (v !== null) edits.push([n.head.getStart(), n.head.getEnd(), "`" + escTemplate(v) + "${"]);
      n.templateSpans.forEach((sp) => {
        const lit = sp.literal;
        const w = take(lit.text);
        const tail = lit.kind === t.SyntaxKind.TemplateTail;
        if (w !== null) edits.push([lit.getStart(), lit.getEnd(), "}" + escTemplate(w) + (tail ? "`" : "${")]);
      });
    } else if (n.kind === t.SyntaxKind.RegularExpressionLiteral) {
      const v = take(n.getText());
      if (v !== null) edits.push([n.getStart(), n.getEnd(), v]);
    }
    t.forEachChild(n, visit);
  };
  visit(sf);
  edits.sort((a, b) => b[0] - a[0]);
  let out = src;
  edits.forEach(([s, e, r]) => (out = out.slice(0, s) + r + out.slice(e)));
  return out;
};

// every text the script carries, to find the Spanish ones left untranslated
const scriptTexts = (src) => {
  const t = loadTs();
  const sf = t.createSourceFile("index.js", src, t.ScriptTarget.ES2017, true);
  const out = [];
  const visit = (n) => {
    if (t.isStringLiteral(n) || t.isNoSubstitutionTemplateLiteral(n)) out.push(n.text);
    else if (t.isTemplateExpression(n)) {
      out.push(n.head.text);
      n.templateSpans.forEach((sp) => out.push(sp.literal.text));
    }
    t.forEachChild(n, visit);
  };
  visit(sf);
  return out;
};

const PAGE_ATTRS = /\b(gbs-confirm|placeholder|title|aria-label|alt)="([^"]*)"/g;

// replaces the page's text between tags (compared without the surrounding spaces) and its visible attributes
const translatePage = (html, map, used) => {
  const take = (key) => {
    if (!Object.prototype.hasOwnProperty.call(map, key)) return null;
    used.add(key);
    return map[key];
  };
  let out = html.replace(/>([^<>]+)</g, (all, text) => {
    const key = text.replace(/\s+/g, " ").trim();
    if (!key) return all;
    const v = take(key);
    if (v === null) return all;
    const lead = text.match(/^\s*/)[0];
    const trail = text.match(/\s*$/)[0];
    return ">" + lead + v + trail + "<";
  });
  out = out.replace(PAGE_ATTRS, (all, name, value) => {
    const v = take(value);
    return v === null ? all : `${name}="${v.replace(/"/g, "&quot;")}"`;
  });
  return out;
};

const pageTexts = (html) => {
  const out = [];
  for (const m of html.matchAll(/>([^<>]+)</g)) out.push(m[1].replace(/\s+/g, " ").trim());
  for (const m of html.matchAll(PAGE_ATTRS)) out.push(m[2]);
  return out;
};

// the CSS: texts drawn with content: "..."
const translateCss = (css, map, used) =>
  css.replace(/content:\s*"([^"]*)"/g, (all, text) => {
    if (!Object.prototype.hasOwnProperty.call(map, text)) return all;
    used.add(text);
    return `content: ${JSON.stringify(map[text])}`;
  });
const cssTexts = (css) => [...css.matchAll(/content:\s*"([^"]*)"/g)].map((m) => m[1]);

// looks Spanish: an accent, ¿ ¡ « », or one of the commonest Spanish words
const looksSpanish = (s) =>
  /[A-Za-zÁÉÍÓÚáéíóúñ]{2}/.test(s) &&
  (/[áéíóúñÁÉÍÓÚÑ¿¡«»]/.test(s) ||
    /\b(de|la|el|los|las|con|para|que|una|del|sin|por|pulsa|elige|borde|bordes|guardar|cargar|borrar|perfil|consola|ranura|paso|pasos)\b/i.test(s));

const loadLanguage = (code) => {
  const file = `${__dirname}/../lang/${code}.json`;
  if (!fs.existsSync(file)) {
    console.error(`No language file ${file}`);
    process.exit(1);
  }
  const l = JSON.parse(fs.readFileSync(file, "utf8"));
  return { code: l.language || code, page: l.page || {}, script: l.script || {} };
};

const report = (lang, left, unused) => {
  if (left.length) {
    console.log(`\n${left.length} text(s) still in Spanish (add them to lang/${lang}.json):`);
    left.forEach((s) => console.log("  " + JSON.stringify(s)));
  }
  if (unused.length) {
    console.log(`\n${unused.length} entr(ies) in lang/${lang}.json not found in the web (the Spanish text changed?):`);
    unused.forEach((s) => console.log("  " + JSON.stringify(s)));
  }
  if (!left.length && !unused.length) console.log(`lang/${lang}.json: every text translated.`);
};

module.exports = { translateScript, scriptTexts, translatePage, pageTexts, translateCss, cssTexts, looksSpanish, loadLanguage, report };
