const systemTheme = matchMedia("(prefers-color-scheme: dark)");
const storageKey = "context-actions-theme";
const themeColor = document.querySelector('meta[name="theme-color"]');
let preferredTheme;
let themeSwitch;

try {
  const saved = localStorage.getItem(storageKey);
  if (saved === "dark" || saved === "light") preferredTheme = saved;
} catch {
  // The theme still works when the browser blocks storage.
}

function applyTheme() {
  const theme = preferredTheme ?? (systemTheme.matches ? "dark" : "light");
  document.documentElement.dataset.theme = theme;
  themeColor.content = theme === "dark" ? "#101724" : "#f7f8f4";
  themeSwitch?.setAttribute("aria-checked", String(theme === "dark"));
}

applyTheme();
systemTheme.addEventListener("change", applyTheme);

document.addEventListener("DOMContentLoaded", () => {
  themeSwitch = document.querySelector(".theme-switch");
  applyTheme();
  themeSwitch.hidden = false;

  themeSwitch.addEventListener("click", () => {
    preferredTheme = document.documentElement.dataset.theme === "dark" ? "light" : "dark";
    applyTheme();
    try {
      localStorage.setItem(storageKey, preferredTheme);
    } catch {
      // Keep the choice for the current page.
    }
  });
});
