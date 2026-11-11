"""Assemble the static GitHub Pages site with the existing walkthrough."""

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
    ROOT / "resources" / "logo-44.png": "resources/logo-44.png",
    ROOT / "docs" / "showcase" / "index.html": "docs/showcase/index.html",
    ROOT / "docs" / "showcase" / "showcase.css": "docs/showcase/showcase.css",
    ROOT / "docs" / "showcase" / "showcase.js": "docs/showcase/showcase.js",
}


if __name__ == "__main__":
    for source, relative_path in FILES.items():
        destination = OUTPUT / relative_path
        destination.parent.mkdir(parents=True, exist_ok=True)
        copyfile(source, destination)
