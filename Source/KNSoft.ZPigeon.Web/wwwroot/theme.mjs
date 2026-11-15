const themeKey = "zpigeon.theme",
  accentKey = "zpigeon.accent",
  system = matchMedia("(prefers-color-scheme: dark)");
let preference = localStorage.getItem(themeKey) ?? "system",
  accent = localStorage.getItem(accentKey) ?? "blue";

export const accents = {
  blue: ["#70a5ff", "#175cd3"],
  teal: ["#52caba", "#087f72"],
  violet: ["#b5a0ff", "#6941c6"],
};

export function applyTheme() {
  const dark = preference === "dark" || (preference === "system" && system.matches),
    root = document.documentElement;
  root.dataset.theme = dark ? "dark" : "light";
  root.style.colorScheme = dark ? "dark" : "light";
  root.style.setProperty("--accent", (accents[accent] ?? accents.blue)[dark ? 0 : 1]);
  window.dispatchEvent(new Event("themechange"));
}

export function setTheme(theme, color = accent) {
  if (!["system", "light", "dark"].includes(theme) || !Object.hasOwn(accents, color)) return;
  preference = theme;
  accent = color;
  localStorage.setItem(themeKey, theme);
  localStorage.setItem(accentKey, color);
  applyTheme();
}

export function getTheme() {
  return { theme: preference, accent };
}

export function terminalTheme() {
  const style = getComputedStyle(document.documentElement);
  return {
    background: style.getPropertyValue("--terminal").trim(),
    foreground: style.getPropertyValue("--text").trim(),
    cursor: style.getPropertyValue("--text").trim(),
    selectionBackground: style.getPropertyValue("--terminal-selection").trim(),
  };
}

system.addEventListener("change", applyTheme);
window.addEventListener("storage", (event) => {
  if (event.key !== themeKey && event.key !== accentKey) return;
  preference = localStorage.getItem(themeKey) ?? "system";
  accent = localStorage.getItem(accentKey) ?? "blue";
  applyTheme();
});
applyTheme();
