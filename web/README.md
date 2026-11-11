# Context Actions website

Build and serve the static site from the repository root with Python 3.

```sh
python web/build.py
python -m http.server 8000 --directory web/dist
```

Open [the local preview](http://localhost:8000). The build packages the website and shared product assets in `web/dist`. The local build uses the published release snapshot in `release.json`.

## GitHub Pages

In the GitHub repository, open **Settings → Pages** and choose **GitHub Actions** as the source under **Build and deployment**. If the `github-pages` environment restricts deployments to specific branches, allow `main`.

The [website workflow](../.github/workflows/website.yml) queries the latest published GitHub release with a read-only workflow token. It rebuilds on pushes to `main`, after a successful release workflow, and daily for changes made directly in GitHub Releases. A manual run from `main` also refreshes the page. The version and links for uploaded assets are rendered at build time. Downloads from a private repository require GitHub access to that repository.
