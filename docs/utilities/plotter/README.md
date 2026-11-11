# JUXTA Plotter

Client-side plotting for **Juxta6-0** Tag daily CSV exports (`jxta-nor-csv-v10`). Drop files into the page for interactive plots, a peer network graph, a system-event table, and a shareable summary. Everything runs in the browser — no server, no data leaves your machine.

## Brand / styling

The UI follows [`brand/JUXTA_BRAND.md`](../../../brand/JUXTA_BRAND.md) and matches the iOS companion: a deep-navy page with a fixed blue / violet / magenta aurora, translucent glass cards, and a plain darker plot panel inside each card (three distinct depths, so plots don't blend into the page). The color glow lives on the page only, not inside the plots.

- `../shared/css/tokens.css` — brand tokens shared with the utilities landing page and the pyJuxta guide. Edit here first; `css/style.css` only consumes semantic aliases.
- `css/style.css` — plotter layout (lockup header, drop zone, cards, plot panels, table, print). The solid gradient fill is still used once, on the primary CTA; the page aurora is a soft background glow. All screen-only depth effects are removed in print.
- `js/plots.js` — `BRAND` palette and `PLOT_THEMES` (dark on screen with lighter blue/violet/magenta series tints, light for print). `app.js` swaps themes on `beforeprint` / `afterprint` since Plotly bakes colors into SVG.
- `assets/juxta_symbol.png` — 256 px copy of the primary symbol (header + favicon). `juxta_tag_mobile.png` / `juxta_tag_base.png` — role artwork above Summary (`JB_*` → base). GitHub Pages deploys `docs/`, so these assets must live here.
- Fonts (Space Grotesk, Plus Jakarta Sans) load from Google Fonts with system fallbacks.

**Live (GitHub Pages):** after Pages is enabled on this repo (Actions source), the site is served from this folder:

`https://neurotech-hub.github.io/Juxta6/utilities/plotter/`

## Supported files (schema v10)

| Prefix | Contents | Columns |
| --- | --- | --- |
| `JXV` | Vitals | `sec,motion,batt_v,temp_c,humidity,lux` |
| `JXB` | BLE peer sightings | `sec,peer_id,rssi` |
| `JXS` | System / lifecycle events | absolute `unix` + event columns + `latitude,longitude` |

### Uploading several days at once

Drop or browse any mix of:

- loose `JX{S|V|B}YYYYMMDD.csv` files from as many days as you like,
- the **zip** exported by the iOS app (`DEVICE_ID.zip` → `DEVICE_ID/JX*YYYYMMDD.csv`; extracted in the browser with JSZip), or
- a whole **folder** (e.g. an unzipped `DEVICE_ID/`; dropped folders are read recursively).

Everything is extracted first, then merged and sorted by time, so plots, the summary, the peer network, and the event table all cover the full span. The same `JX*YYYYMMDD.csv` appearing twice (e.g. a zip plus its loose files) is counted once, using the last copy, with a warning. The status line under the drop zone shows how many days were merged and their date range. Time-series plots break the line where there is a long gap with no data (for example between days), instead of drawing a straight line across it. Files must all come from one device.

Filenames must be `JX{S|V|B}YYYYMMDD.csv`. For JXV/JXB, `sec` is day-relative UTC seconds; absolute time is UTC midnight of the filename date + `sec`. JXV `temp_c` / `humidity` are BME688 one-shot values at one decimal, and `lux` is a whole number from the TEMT6200. JXB `peer_id` is **7 characters**: `X` (mobile) or `B` (base) + 6 MAC hex; the plotter reconstructs `JX_`/`JB_` labels on the BLE Peers axis. JXS lat/lon are filled on `time_set` (this sync) and `day_start` (last-known); other events leave them blank. Multiple days merge and sort by time; mixed `device_id` values in JXS trigger a warning.

## What it shows

- **Summary** — role artwork (mobile/base) above cards: Device (ID + firmware), Duration, Environment, Action
- **Last location** — Leaflet map marker from the latest JXS lat/lon (`time_set` / `day_start`)
- **Activity** — motion over time
- **Battery** — voltage over time
- **Temperature & humidity** — dual y-axis (temp left, humidity right)
- **Light** — lux over time
- **BLE peers** — one row per peer, colored by RSSI
- **Peer network** — weighted graph with this device at the center and one node per JXB peer, aggregated over every loaded day. Edge width and node size = number of sightings; edge length and color = mean RSSI (shorter and warmer = stronger, same color scale as BLE peers); dotted rings mark reference RSSI levels. Hover a node for sightings, mean/min/max RSSI, and first/last seen. Shows the 40 most-seen peers if there are more
- **Lifecycle timeline** — stepped JXS state chart (event held until the next); y-axis uses the canonical lifecycle order so out-of-sequence runs zigzag
- **System events** — JXS table with timezone conversion and lat/lon
- **Summary copy** — click-to-copy plain text (no duplicate device line)

Timezone dropdown: Central, Eastern, or UTC (default). Data is recorded in UTC; selecting UTC shows timestamps as stored.

**Demo:** use **Load Dog example (31 Oct)** (lower right under the drop zone) for one real day of the leash tag (`JX_5C0848`, 2026-10-31) from the [pyJuxta example deployment](../pyjuxta/data/). That needs `docs/` served over HTTP so the plotter can fetch `../pyjuxta/data/JX_5C0848/` (GitHub Pages or `python3 -m http.server` from `docs/`).

Map tiles load from Esri (dark canvas) when a package has coordinates (approx. location visible to the tile CDN; CSV stays in-browser). PapaParse, Plotly, JSZip, and Leaflet load from CDNs.

## Running locally

```bash
cd docs
python3 -m http.server 8000
```

Then open http://localhost:8000/utilities/plotter/ — or open `index.html` directly.

## Deploying to GitHub Pages

This monorepo deploys **`docs/`** via [`.github/workflows/pages.yml`](../../../.github/workflows/pages.yml). The site root redirects to `utilities/`. The plotter is `utilities/plotter/`.

1. Push to `main`.
2. Repo **Settings → Pages → Build and deployment → Source: GitHub Actions**.
3. No build step. PapaParse, Plotly, JSZip, and Leaflet load from CDNs. `docs/.nojekyll` keeps GitHub from running Jekyll.

## License

Proprietary — see the repository [LICENSE](../../../LICENSE). Reuse requires explicit written permission.
