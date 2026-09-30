# Vendored browser modules (MIT)

The play page is a static app served as-is by the site (VitePress copies `docs/public/`
without bundling), so its dependencies live here as plain ES modules.

| Folder | Source | Version | Licence |
|---|---|---|---|
| `gasm/` | `runners/web/gasm-host.js`, `gasm-worker.js` of [gasm](https://github.com/emdzej/gasm), commit `39ffc69` | `@emdzej/gasm-host` 0.3.0 (unreleased at the time of copying) | MIT, `gasm/LICENSE` |
| `csfs/` | `dist/` of `@emdzej/csfs-core`, `@emdzej/csfs-fsa`, `@emdzej/csfs-opfs` (as vendored by gasm's `scripts/vendor-web.sh`), source maps removed | 0.3.0 | MIT, `csfs/LICENSE` |

`index.html` maps the bare `@emdzej/csfs-*` specifiers to these folders with an import map.

Once `@emdzej/gasm-host@0.3.0` is published on npm, replace `gasm/` with the files from the
package (`gasm-host.js`, `gasm-worker.js`; the package has no dependencies), pinned to an exact
version, and update the table. Do not edit the vendored files: fix things upstream.
