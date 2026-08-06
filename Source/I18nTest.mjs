import { readFile, readdir } from "node:fs/promises";
import { extname, join } from "node:path";
import { fileURLToPath } from "node:url";
import vm from "node:vm";

const root = new URL("./KNSoft.ZPigeon.Web/wwwroot/", import.meta.url),
  source = (await readFile(new URL("i18n.mjs", root), "utf8")).replaceAll("\r\n", "\n"),
  zh = catalog("zh", "en"),
  en = catalog("en", "enSource"),
  translationContext = {
    navigator: { languages: ["en-US"], language: "en-US" },
    document: { documentElement: {} },
  },
  failures = [];

vm.createContext(translationContext);
vm.runInContext(
  `${source.replace(/^export /gm, "")}\nthis.translateForAudit = translateSource;`,
  translationContext,
);

for (const key of new Set([...Object.keys(zh), ...Object.keys(en)])) {
  if (!(key in zh)) failures.push(`Missing zh-CN message: ${key}`);
  if (!(key in en)) failures.push(`Missing en-US message: ${key}`);
}

let sourceReferences = 0,
  dynamicReferences = 0,
  authoredReferences = 0;
for (const path of await files(root)) {
  const content = await readFile(path, "utf8");
  for (const pattern of [
    /data-i18n(?:-title|-placeholder|-aria-label)?="([^"]+)"/g,
    /\bt\(\s*["']([^"']+)["']/g,
  ]) {
    for (const match of content.matchAll(pattern)) {
      if (!(match[1] in zh) || !(match[1] in en))
        failures.push(`${path}: unknown message key ${match[1]}`);
    }
  }
  if (path.endsWith("i18n.mjs")) continue;
  const sourceValues = [];
  for (const match of content.matchAll(/(["'`])((?:\\.|(?!\1)[\s\S])*?)\1/g)) {
    const value = match[2];
    if (/\p{Script=Han}/u.test(value) && !value.includes("${") && !/[<>]/.test(value))
      sourceValues.push(
        value.replaceAll("\\n", "\n").replaceAll('\\"', '"').replaceAll("\\'", "'"),
      );
  }
  for (const match of content.matchAll(
    /(?:>|\b(?:title|placeholder|aria-label)=")\s*([^<>"\n]*\p{Script=Han}[^<>"\n]*)\s*(?:<|")/gu,
  ))
    sourceValues.push(match[1].trim());
  for (const value of sourceValues) {
    sourceReferences++;
    if (translationContext.translateForAudit("en-US", value) === value)
      failures.push(`${path}: untranslated source text ${JSON.stringify(value)}`);
  }
  for (const match of content.matchAll(/`((?:\\.|[^`])*)`/g)) {
    const template = match[1];
    if (!/\p{Script=Han}/u.test(template) || !template.includes("${") || /[<>]/.test(template)) continue;
    const sample = template.replace(/\$\{[^{}]*\}/g, "1").replaceAll("\\n", "\n");
    if (sample.includes("${")) continue;
    dynamicReferences++;
    if (translationContext.translateForAudit("en-US", sample) === sample)
      failures.push(`${path}: untranslated dynamic text ${JSON.stringify(template)}`);
  }
}

for (const path of await authoredFiles(new URL("./", import.meta.url))) {
  const content = await readFile(path, "utf8");
  for (const match of content.matchAll(/(?:L|\$)?"((?:\\.|[^"\\])*)"/g)) {
    const value = match[1];
    if (!/\p{Script=Han}/u.test(value)) continue;
    let sample;
    if (value.includes("{") || value.includes("%")) {
      sample = value
        .replace(/\{[^{}]*\}/g, "1")
        .replace(
          /%(?:[-+0 #]*)(?:\*|\d+)?(?:\.(?:\*|\d+))?(?:hh|h|ll|l|I32|I64|I|w|L)?[diuoxXfFeEgGaAcCsSpnZ%]/g,
          (format) => format === "%%" ? "%" : "1",
        );
    } else {
      sample = value;
    }
    sample = sample.replaceAll("\\n", "\n");
    authoredReferences++;
    if (translationContext.translateForAudit("en-US", sample) === sample)
      failures.push(`${path}: untranslated authored text ${JSON.stringify(value)}`);
  }
}

if (failures.length) throw new Error(failures.join("\n"));
console.log(
  `I18N catalogs: ${Object.keys(zh).length} stable keys, ${sourceReferences} source references, ` +
    `${dynamicReferences} dynamic references, ${authoredReferences} Native/Managed references verified.`,
);

function catalog(name, nextName) {
  const start = source.indexOf(`const ${name} = {`),
    end = source.indexOf(`\n\nconst ${nextName} =`, start);
  if (start < 0 || end < 0) throw new Error(`Cannot locate ${name} catalog.`);
  const expression = source.slice(source.indexOf("{", start), end).trimEnd().replace(/;$/, "");
  return vm.runInNewContext(`(${expression})`, Object.create(null));
}

async function files(directory) {
  const result = [];
  for (const entry of await readdir(directory, { withFileTypes: true })) {
    if (entry.name === "vendor") continue;
    const path = join(directory.pathname, entry.name);
    if (entry.isDirectory()) result.push(...await files(new URL(`${entry.name}/`, directory)));
    else if ([".html", ".mjs"].includes(extname(entry.name))) result.push(path.slice(1));
  }
  return result;
}

async function authoredFiles(directory) {
  const result = [];
  for (const entry of await readdir(directory, { withFileTypes: true })) {
    if (["OutDir", "bin", "obj", "packages", "vendor", "wwwroot"].includes(entry.name)) continue;
    const url = new URL(`${encodeURIComponent(entry.name)}${entry.isDirectory() ? "/" : ""}`, directory);
    if (entry.isDirectory()) result.push(...await authoredFiles(url));
    else if (/\.(?:c|h|inl|cpp|cs)$/.test(entry.name) && !entry.name.includes(".g."))
      result.push(fileURLToPath(url));
  }
  return result;
}
