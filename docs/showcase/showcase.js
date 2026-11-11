"use strict";

const chapters = [...document.querySelectorAll("[data-time]")];
const scenes = [
  {
    title: "Right-click a video.",
    summary: "In File Explorer, open Context Actions and choose Convert to MP4 / H.264.",
    image: "explorer.jpg",
    alt: "The Context Actions submenu in File Explorer with Convert to MP4 / H.264.",
  },
  {
    title: "Convert video to MP4.",
    summary: "Choose a quality preset and select Run. When processing finishes, open the MP4 or show it in its folder.",
    image: "video.jpg",
    alt: "Completed H.264 conversion with links to open the result or show it in its folder.",
  },
];

const stage = document.querySelector(".stage");
const title = document.querySelector("#scene-title");
const summary = document.querySelector("#scene-summary");
const image = document.querySelector("#scene-image");
const play = document.querySelector("#play");
const position = document.querySelector("#position");
const time = document.querySelector("#time");
const duration = Number(position.max);
const reducedMotion = matchMedia("(prefers-reduced-motion: reduce)");
const starts = chapters.map(chapter => Number(chapter.dataset.time));
let currentChapter = 0;
let currentTime = 0;
let startTime;
let frame;

for (const scene of scenes) {
  new Image().src = scene.image;
}

function formatTime(seconds) {
  return `0:${String(Math.floor(seconds)).padStart(2, "0")}`;
}

function showTime(seconds) {
  currentTime = Math.min(Math.max(0, seconds), duration);
  const index = starts.findLastIndex(start => start <= currentTime);
  const scene = scenes[index];
  const elapsed = currentTime - starts[index];

  if (index !== currentChapter) {
    chapters[currentChapter].removeAttribute("aria-current");
    chapters[index].setAttribute("aria-current", "true");
    currentChapter = index;
    title.textContent = scene.title;
    summary.textContent = scene.summary;
    image.src = scene.image;
    image.alt = scene.alt;
  }

  const entrance = index === 0 || reducedMotion.matches ? 0 : Math.max(0, 1 - elapsed / 0.5) ** 3;
  stage.style.setProperty("--scene-shift", `${entrance * 22}px`);
  stage.style.setProperty("--scene-opacity", 1 - entrance * 0.22);

  position.value = currentTime;
  const clock = formatTime(currentTime);
  const total = formatTime(duration);
  const label = `${clock} of ${total}`;
  if (position.getAttribute("aria-valuetext") !== label) {
    position.setAttribute("aria-valuetext", label);
    time.textContent = `${clock} / ${total}`;
  }
}

function setPlayLabel(label) {
  play.textContent = label;
  play.setAttribute("aria-label", `${label} tour`);
}

function pause() {
  cancelAnimationFrame(frame);
  frame = undefined;
  setPlayLabel(currentTime === duration ? "Replay" : "Play");
}

function tick(now) {
  showTime((now - startTime) / 1000);
  if (currentTime < duration) {
    frame = requestAnimationFrame(tick);
  } else {
    pause();
  }
}

function seek(seconds) {
  showTime(seconds);
  if (frame !== undefined && currentTime < duration) {
    startTime = performance.now() - currentTime * 1000;
  } else {
    pause();
  }
}

play.addEventListener("click", () => {
  if (frame !== undefined) {
    pause();
    return;
  }
  if (currentTime === duration) showTime(0);
  startTime = performance.now() - currentTime * 1000;
  setPlayLabel("Pause");
  frame = requestAnimationFrame(tick);
});

position.addEventListener("input", () => seek(Number(position.value)));
chapters.forEach((chapter, index) => chapter.addEventListener("click", () => seek(starts[index])));
reducedMotion.addEventListener("change", () => showTime(currentTime));
