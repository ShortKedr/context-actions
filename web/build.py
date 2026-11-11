"""Assemble the static GitHub Pages site with the existing product media."""

from pathlib import Path
from shutil import copyfile


WEB = Path(__file__).resolve().parent
ROOT = WEB.parent
OUTPUT = WEB / "dist"

FILES = {
    WEB / "index.html": "index.html",
    WEB / "site.css": "site.css",
    WEB / "theme.js": "theme.js",
    ROOT / "resources" / "logo-150.png": "assets/context-actions.png",
    ROOT / "docs" / "showcase" / "demo.mp4": "assets/walkthrough.mp4",
}


if __name__ == "__main__":
    for source, relative_path in FILES.items():
        destination = OUTPUT / relative_path
        destination.parent.mkdir(parents=True, exist_ok=True)
        copyfile(source, destination)
