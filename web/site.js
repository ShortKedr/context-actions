const motionPreference = matchMedia("(prefers-reduced-motion: no-preference)");

if ("IntersectionObserver" in window && Element.prototype.animate) {
  let initialEntries = true;
  const observer = new IntersectionObserver((entries) => {
    for (const { target, isIntersecting } of entries) {
      if (!isIntersecting) continue;

      observer.unobserve(target);
      if (initialEntries || !motionPreference.matches || target.matches(":focus-within")) continue;

      target.animate([
        { opacity: 0.45, transform: "translateY(12px)" },
        { opacity: 1, transform: "translateY(0)" }
      ], { duration: 420, easing: "cubic-bezier(.2, .65, .3, 1)" });
    }
    initialEntries = false;
  });

  document.querySelectorAll("[data-reveal]").forEach((block) => observer.observe(block));
}

if ("IntersectionObserver" in window) {
  const video = document.querySelector(".walkthrough-frame video");
  let videoInView = false;

  function syncWalkthroughPlayback() {
    if (videoInView && motionPreference.matches) {
      video.play().catch(() => {
        // The poster remains available if the browser blocks autoplay.
      });
    } else {
      video.pause();
    }
  }

  const videoObserver = new IntersectionObserver(([entry]) => {
    videoInView = entry.intersectionRatio >= 0.25;
    syncWalkthroughPlayback();
  }, { threshold: 0.25 });

  videoObserver.observe(video);
  motionPreference.addEventListener("change", syncWalkthroughPlayback);
}
