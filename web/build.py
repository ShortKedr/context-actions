"""Assemble the static site with published release downloads and the walkthrough."""

from html import escape
import json
import os
from pathlib import Path
import re
from shutil import copyfile
from urllib.parse import quote
from urllib.request import Request, urlopen


WEB = Path(__file__).resolve().parent
ROOT = WEB.parent
OUTPUT = WEB / "dist"
REPOSITORY = "ShortKedr/context-actions"

FILES = {
    WEB / "site.css": "site.css",
    WEB / "theme.js": "theme.js",
    WEB / "demo-poster.jpg": "assets/demo-poster.jpg",
    ROOT / "resources" / "logo-150.png": "assets/context-actions.png",
    ROOT / "resources" / "logo-44.png": "resources/logo-44.png",
    ROOT / "docs" / "showcase" / "index.html": "docs/showcase/index.html",
    ROOT / "docs" / "showcase" / "demo.mp4": "docs/showcase/demo.mp4",
    ROOT / "docs" / "showcase" / "showcase.css": "docs/showcase/showcase.css",
    ROOT / "docs" / "showcase" / "showcase.js": "docs/showcase/showcase.js",
}

LINUX_FORMATS = (
    ("Debian / Ubuntu (.deb)", ".deb"),
    ("Fedora / openSUSE (.rpm)", ".rpm"),
    ("Arch Linux (.pkg.tar.zst)", ".pkg.tar.zst"),
    ("Portable archive (.tar.gz)", ".tar.gz"),
)
LINUX_ARCHITECTURES = (
    ("x86-64", ("amd64", "x86_64")),
    ("ARM64", ("arm64", "aarch64")),
)


def release_assets(repository):
    token = os.environ.get("GITHUB_TOKEN")
    if not token:
        release = json.loads((WEB / "release.json").read_text(encoding="utf-8"))
        return release["tag"], release["assets"]

    api = os.environ.get("GITHUB_API_URL", "https://api.github.com").rstrip("/")
    request = Request(
        f"{api}/repos/{repository}/releases/latest",
        headers={
            "Authorization": f"Bearer {token}",
            "Accept": "application/vnd.github+json",
            "X-GitHub-Api-Version": "2022-11-28",
        },
    )
    with urlopen(request, timeout=20) as response:
        release = json.load(response)
    assets = [asset["name"] for asset in release["assets"] if asset["state"] == "uploaded"]
    return release["tag_name"], assets


def link(url, title, classes="text-link", aria_label=None):
    label = f' aria-label="{escape(aria_label)}"' if aria_label else ""
    return f'<a class="{classes}" href="{escape(url)}"{label}>{escape(title)}</a>'


def platform_card(platform, description, installer, archive, urls):
    primary, title = (installer if installer[0] in urls else archive)
    if primary not in urls:
        return ""

    button = link(urls[primary], title, "button button-primary", f"{title} for {platform}")
    archive_link = ""
    if primary != archive[0] and archive[0] in urls:
        archive_link = link(urls[archive[0]], archive[1], aria_label=f"{archive[1]} for {platform}")
    return f'<article class="download-card"><h3>{platform}</h3><p>{description}</p>{button}{archive_link}</article>'


def linux_card(version, urls):
    packages = []
    for label, extension in LINUX_FORMATS:
        # .pkg.tar.zst and .tar.gz are separate formats, with an architecture in the file name.
        architecture_links = []
        for architecture, aliases in LINUX_ARCHITECTURES:
            pattern = rf"context-actions[-_]{re.escape(version)}.*[-_.](?:{'|'.join(aliases)}){re.escape(extension)}"
            asset = next((name for name in sorted(urls) if re.fullmatch(pattern, name, re.IGNORECASE)), None)
            if asset:
                architecture_links.append(link(urls[asset], architecture, aria_label=f"{label} for Linux {architecture}"))
        if architecture_links:
            packages.append(f'<li><span>{label}</span><span class="package-arches">{" ".join(architecture_links)}</span></li>')

    installer = urls.get("ContextActions-linux-installer.sh")
    if not packages and not installer:
        return ""

    helper = link(installer, "Reinstall or downgrade (.sh)") if installer else ""
    return (
        '<article class="download-card"><h3>Linux</h3><p>Packages by distribution and architecture</p>'
        '<details><summary class="button button-primary">Download for Linux <span aria-hidden="true">↓</span></summary>'
        f'<ul class="package-list">{"".join(packages)}</ul>{helper}</details></article>'
    )


def render_index():
    repository = os.environ.get("GITHUB_REPOSITORY", REPOSITORY)
    if not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", repository):
        raise ValueError("Expected GITHUB_REPOSITORY in owner/repository format.")
    tag, names = release_assets(repository)
    if not re.fullmatch(r"v[0-9]+\.[0-9]+\.[0-9]+", tag):
        raise ValueError("Expected a published release tag in vMAJOR.MINOR.PATCH format.")

    base = f"https://github.com/{repository}/releases"
    urls = {name: f"{base}/download/{tag}/{quote(name, safe='')}" for name in names}
    version = tag[1:]
    cards = (
        platform_card(
            "Windows", "Windows 11 · x64",
            (f"ContextActions-{version}-win-x64-setup.exe", "Download setup (.exe)"),
            ("ContextActions-win-x64.zip", "Portable ZIP"), urls,
        ),
        platform_card(
            "macOS", "Intel and Apple silicon",
            (f"ContextActions-{version}-macos-universal.pkg", "Download installer (.pkg)"),
            (f"ContextActions-{version}-macos-universal.zip", "App ZIP"), urls,
        ),
        linux_card(version, urls),
    )
    downloads = "\n          ".join(card for card in cards if card)
    if downloads:
        downloads = f'<div class="download-grid">\n          {downloads}\n        </div>'
    else:
        downloads = "<p>No packages are available for this release.</p>"
    checksum = link(urls["SHA256SUMS.txt"], "SHA-256 checksums") if "SHA256SUMS.txt" in urls else ""
    template = (WEB / "index.html").read_text(encoding="utf-8")
    return (template.replace("@RELEASE_TAG@", escape(tag))
            .replace("@RELEASE_URL@", f"{base}/tag/{tag}")
            .replace("<!-- DOWNLOAD_CARDS -->", downloads)
            .replace("<!-- CHECKSUM_LINK -->", checksum))


if __name__ == "__main__":
    index = render_index()
    for source, relative_path in FILES.items():
        destination = OUTPUT / relative_path
        destination.parent.mkdir(parents=True, exist_ok=True)
        copyfile(source, destination)
    (OUTPUT / "index.html").write_text(index, encoding="utf-8")
