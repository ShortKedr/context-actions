"use strict";

const embedded = new URLSearchParams(location.search).has("embed");
const reducedMotion = matchMedia("(prefers-reduced-motion: reduce)");
const stage = document.querySelector(".stage");
const sceneCopy = document.querySelector(".scene-copy");
const title = document.querySelector("#scene-title");
const summary = document.querySelector("#scene-summary");
const explorer = document.querySelector(".explorer");
const processing = document.querySelector(".processing");
const fileStatus = document.querySelector(".file-status");
const jobStatus = document.querySelector(".job-status");
const play = document.querySelector("#play");
const position = document.querySelector("#position");
const time = document.querySelector("#time");
const chapters = [...document.querySelectorAll("[data-time]")];
const duration = Number(position.max);
const runningAt = 6.1;
const doneAt = 8.35;
const steps = [
  {
    at: 0, chapter: 0, id: "file",
    title: "Start in File Explorer",
    summary: "Right-click the video you want to convert.",
  },
  {
    at: 0.8, chapter: 0, id: "context",
    title: "Open Context Actions",
    summary: "Open the Context Actions submenu from the right-click menu.",
  },
  {
    at: 1.7, chapter: 0, id: "menu",
    title: "Choose MP4 / H.264",
    summary: "Select Convert to MP4 / H.264 to open the compact processing window.",
  },
  {
    at: 4.2, chapter: 1, id: "ready",
    title: "Run the conversion",
    summary: "Keep Balanced quality and save beside the source, then select Run.",
    fileStatus: "Ready",
    jobStatus: "Ready",
  },
  {
    at: runningAt, chapter: 1, id: "running",
    title: "Converting to MP4",
    summary: "Select Cancel if you need to stop the conversion.",
    fileStatus: "Processing…",
    jobStatus: "Processing…",
  },
  {
    at: doneAt, chapter: 2, id: "done",
    title: "Your MP4 is ready",
    summary: "Open the result or show it in its folder.",
    fileStatus: "Done",
    jobStatus: "Done · 1 succeeded · 0 failed",
  },
];

let currentTime = 0;
let startTime;
let frame;

function formatTime(seconds) {
  return `0:${String(Math.floor(seconds)).padStart(2, "0")}`;
}

function showTime(seconds) {
  currentTime = Math.min(Math.max(0, seconds), duration);
  const step = steps.findLast(step => step.at <= currentTime);

  if (stage.dataset.step !== step.id) {
    stage.dataset.step = step.id;
    title.textContent = step.title;
    summary.textContent = step.summary;
    if (embedded && !reducedMotion.matches) {
      sceneCopy.animate(
        [{ opacity: 0.35, transform: "translateY(4px)" }, { opacity: 1, transform: "none" }],
        { duration: 260, easing: "ease-out" },
      );
    }
    explorer.hidden = step.chapter !== 0;
    processing.hidden = step.chapter === 0;

    if (!processing.hidden) {
      fileStatus.textContent = step.fileStatus;
      jobStatus.textContent = step.jobStatus;
    }

    chapters.forEach((chapter, index) => {
      if (index === step.chapter) chapter.setAttribute("aria-current", "true");
      else chapter.removeAttribute("aria-current");
    });
  }

  const progress = Math.min(Math.max(0, (currentTime - runningAt) / (doneAt - runningAt)), 1);
  stage.style.setProperty("--offset", `-${currentTime}s`);
  stage.style.setProperty("--progress", progress);

  if (embedded) return;

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
  const elapsed = (now - startTime) / 1000;
  showTime(embedded ? elapsed % duration : elapsed);
  if (embedded || currentTime < duration) {
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
chapters.forEach(chapter => chapter.addEventListener("click", () => {
  pause();
  seek(Number(chapter.dataset.time));
}));

showTime(0);
if (embedded) {
  document.documentElement.classList.add("embedded");
  startTime = performance.now();
  frame = requestAnimationFrame(tick);
} else {
  document.querySelector(".controls").hidden = false;
  document.querySelector(".chapters").hidden = false;
}
