# Mem Reduct - landing page

Static landing page for Mem Reduct for Linux, built with Vite + React.
Pulls live stars/forks/issues, the latest release and contributors from the
GitHub API (client-side, no tokens, cached in localStorage for 10 minutes).

```sh
npm install
npm run dev       # local dev server
npm run build     # static output in dist/
npm run preview   # serve the built output
```

Deployed automatically to GitHub Pages by
`site/deploy/deploy-pages.yml` (copy it to `.github/workflows/`) on every push to `master` that touches
`site/` (enable Pages: repo Settings → Pages → Source: GitHub Actions).
