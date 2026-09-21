# JUXTA Plotter

Client-side plotting for **Juxta6-0** Tag daily CSV exports (`jxta-nor-csv-v8`). Drop files into the page for interactive plots, a system-event table, and a shareable summary. Everything runs in the browser — no server, no data leaves your machine.

## Brand / styling

The UI follows [`brand/JUXTA_BRAND.md`](../brand/JUXTA_BRAND.md) as a dark-navy dashboard (§16).

- `css/tokens.css` — brand tokens (palette, surfaces, gradient, radii, spacing, type). Edit here first; `style.css` only consumes semantic aliases.
- `css/style.css` — layout and components (lockup header, drop zone, cards, table, print). The gradient is used once, on the primary CTA.
- `js/plots.js` — `BRAND` palette and `PLOT_THEMES` (dark on screen, light for print). `app.js` swaps themes on `beforeprint` / `afterprint` since Plotly bakes colors into SVG.
- `assets/juxta_symbol.png` — 256 px copy of the primary symbol (header + favicon). GitHub Pages deploys only `plotter/`, so the asset must live here.
- Fonts (Space Grotesk, Plus Jakarta Sans) load from Google Fonts with system fallbacks.

**Live (GitHub Pages):** after Pages is enabled on this repo (Actions source), the site is served from this folder:

`https://neurotech-hub.github.io/Juxta6/`

## Supported files (schema v8)

| Prefix | Contents | Columns |
| --- | --- | --- |
| `JXV` | Vitals | `sec,motion,batt_v,temp_c,humidity` |
| `JXB` | BLE peer sightings | `sec,peer_id,rssi` |
| `JXS` | System / lifecycle events | absolute `unix` + event columns + `latitude,longitude` |

Filenames must be `JX{S|V|B}YYYYMMDD.csv`. For JXV/JXB, `sec` is day-relative UTC seconds; absolute time is UTC midnight of the filename date + `sec`. JXV `temp_c` / `humidity` are BME688 one-shot values at one decimal. JXB `peer_id` has no `JX_` prefix. JXS lat/lon are filled on `time_set` (this sync) and `day_start` (last-known); other events leave them blank. Multiple days merge and sort by time; mixed `device_id` values in JXS trigger a warning.

## What it shows

- **Summary** — four cards: Duration (start/end), Environment (temp/humidity), Action (motion/peers), Firmware
- **Last location** — Leaflet map marker from the latest JXS lat/lon (`time_set` / `day_start`)
- **Activity** — motion over time
- **Battery** — voltage over time
- **Temperature & humidity** — dual y-axis (temp left, humidity right)
- **BLE peers** — one row per peer, colored by RSSI
- **System events** — JXS table with timezone conversion and lat/lon
- **Summary copy** — click-to-copy plain text (no duplicate device line)

Timezone dropdown: Central, Eastern, or UTC (default). Data is recorded in UTC; selecting UTC shows timestamps as stored.

**Demo:** use **Load demo data** (lower right under the drop zone) for a synthetic `jxta-nor-csv-v8` day (`JX_A1B2C3`, ~3 h production) without uploading files.

Map tiles load from Esri (dark canvas) when a package has coordinates (approx. location visible to the tile CDN; CSV stays in-browser).

## Running locally

```bash
cd plotter
python3 -m http.server 8000
```

Then open http://localhost:8000 — or open `index.html` directly.

## Deploying to GitHub Pages

This monorepo deploys **only** `plotter/` via [`.github/workflows/pages.yml`](../.github/workflows/pages.yml).

1. Push to `main`.
2. Repo **Settings → Pages → Build and deployment → Source: GitHub Actions**.
3. Site root = plotter contents (PapaParse / Plotly from CDN; no build step).

## License

Proprietary — see the repository [LICENSE](../LICENSE). Reuse requires explicit written permission.
