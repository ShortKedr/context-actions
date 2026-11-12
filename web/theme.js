const systemTheme = matchMedia("(prefers-color-scheme: dark)");
const reducedMotion = matchMedia("(prefers-reduced-motion: reduce)");
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

function currentTheme() {
  return preferredTheme ?? (systemTheme.matches ? "dark" : "light");
}

function applyTheme() {
  const theme = currentTheme();
  document.documentElement.dataset.theme = theme;
  themeColor.content = theme === "dark" ? "#131222" : "#f6f5fa";
  themeSwitch?.setAttribute("aria-checked", String(theme === "dark"));
}

function changeTheme() {
  if (currentTheme() === document.documentElement.dataset.theme) return;

  if (!reducedMotion.matches && document.startViewTransition) {
    document.startViewTransition(applyTheme);
    return;
  }

  applyTheme();
  if (!reducedMotion.matches) {
    document.body?.animate?.([{ opacity: 0.72 }, { opacity: 1 }], {
      duration: 240,
      easing: "ease-out"
    });
  }
}

applyTheme();
systemTheme.addEventListener("change", changeTheme);

document.addEventListener("DOMContentLoaded", () => {
  themeSwitch = document.querySelector(".theme-switch");
  applyTheme();
  themeSwitch.hidden = false;

  themeSwitch.addEventListener("click", () => {
    preferredTheme = currentTheme() === "dark" ? "light" : "dark";
    changeTheme();
    try {
      localStorage.setItem(storageKey, preferredTheme);
    } catch {
      // Keep the choice for the current page.
    }
  });
});
