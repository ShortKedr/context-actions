# Context Actions website

Build and serve the static site from the repository root with Python 3.

```sh
python web/build.py
python -m http.server 8000 --directory web/dist
```

Open [the local preview](http://localhost:8000). The build packages the website and shared product assets in `web/dist`.

## GitHub Pages

In the GitHub repository, open **Settings → Pages** and choose **GitHub Actions** as the source under **Build and deployment**. If the `github-pages` environment restricts deployments to specific branches, allow `main`.

The [website workflow](../.github/workflows/website.yml) builds and deploys on pushes to `main`. The deploy job reports the published URL in the Actions run.
