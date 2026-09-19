# Juxta6 Plotter

Client-side plotting for **Juxta6-0** Tag daily CSV exports (`jxta-nor-csv-v7`). Drop files into the page for interactive plots, a system-event table, and a shareable summary. Everything runs in the browser — no server, no data leaves your machine.

**Live (GitHub Pages):** after Pages is enabled on this repo (Actions source), the site is served from this folder:

`https://neurotech-hub.github.io/Juxta6/`

## Supported files (schema v7)

| Prefix | Contents | Columns |
| --- | --- | --- |
| `JXV` | Vitals | `sec,motion,batt_v,temp_c` |
| `JXB` | BLE peer sightings | `sec,peer_id,rssi` |
| `JXS` | System / lifecycle events | absolute `unix` + event columns + `latitude,longitude` |

Filenames must be `JX{S\|V\|B}YYYYMMDD.csv`. For JXV/JXB, `sec` is day-relative UTC seconds; absolute time is UTC midnight of the filename date + `sec`. JXB `peer_id` has no `JX_` prefix. JXS lat/lon are filled on `time_set` (this sync) and `day_start` (last-known); other events leave them blank. Multiple days merge and sort by time; mixed `device_id` values in JXS trigger a warning.

## What it shows

- **Activity** — motion over time
- **Battery & temperature** — dual y-axis
- **BLE peers** — one row per peer, colored by RSSI
- **System events** — JXS table with timezone conversion and lat/lon
- **Summary** — click-to-copy plain text

Timezone dropdown: Central, Eastern, or UTC (default). Data is recorded in UTC; selecting UTC shows timestamps as stored.

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
