# pyJuxta

Python library for Juxta6 daily logs (`jxta-nor-csv-v10`, firmware `juxta6-0-prod` 6.6.0). It loads a whole deployment (mobile `JX_` tags and base `JB_` stations) and answers questions about it. Every analysis returns a `Result`: the numbers, the settings that shaped them, how much of the deployment they rest on, caveats, and a figure.

Guided walkthrough that runs this library in the browser, on an example deployment or your own files: [docs/utilities/pyjuxta/](../docs/utilities/pyjuxta/).

## Install

```bash
pip install -e pyjuxta            # from a clone of the repo
pip install -e "pyjuxta[plot]"    # adds plotly for Result.show()
pip install -e "pyjuxta[test]"    # pytest and tzdata
```

Python 3.11 or newer; numpy and pandas are the only required dependencies.

## Use

```python
import pyjuxta as jx

dep = jx.load_deployment("deployment/", timezone="America/Chicago")   # folders, zips, or CSVs
dep.set_labels({"JX_54F7A1": "Human", "JX_5C0848": "Dog"})
dep.check()                                    # data-quality findings

dep.availability()                             # when was each tag recording
dep.activity(["Human", "Dog"], bin_s=900)      # motion on a fixed UTC grid
dep.diel("Human"); dep.rhythms("Human")        # day profile; IS/IV/L5/M10/RA behind a quality gate
dep.environment("temp_c")
dep.visits("Dog", min_rssi=-80)                # base-station visits and strongest base per window
dep.proximity_matrix()
dep.proximity("Human", "Dog")                  # encounters and reciprocity
dep.synchrony("Human", "Dog")                  # correlation vs shifted copies, time of day removed, together vs apart

res = dep.proximity("Human", "Dog")
res.summary; res.tables; res.parameters; res.quality; res.notes
res.figure("light")                            # Plotly figure as a plain dict
res.show()                                     # needs the plot extra
```

`load_deployment` accepts a folder of device folders, a folder of zips, a zip, a zip of zips, a single CSV, a list of paths, or a mapping of `{path: bytes}`.

## Principles

- A missing row is missing information, not zero. Bins with too little coverage are `NaN`; smoothing never fills them.
- A detection is not a distance. RSSI summaries say so next to the numbers.
- Rhythm metrics (IS, IV, L5, M10, RA) are computed only when the data has enough complete local days; otherwise the result says why not.
- Null comparisons (shifted copies, matched hours) are seeded and report how few shifts the data allowed.

Scope and status are tracked in [`agents/pyJuxta_Analysis_Specification.md`](../agents/pyJuxta_Analysis_Specification.md).

## Tests

```bash
cd pyjuxta
python -m venv .venv && .venv/bin/pip install -e ".[test]"
.venv/bin/pytest
```

Tests build small synthetic v10 files, check the firmware contract (time, peer ids, motion accumulation after a gap), and, when `docs/utilities/pyjuxta/data/` is present, run every analysis on the example deployment.

## Site assets

The walkthrough installs a wheel of this package inside the browser (Pyodide). The wheel and a data manifest are generated, not committed:

```bash
python3 pyjuxta/tools/build_site_assets.py
cd docs && python3 -m http.server 8000      # open /utilities/pyjuxta/
```

The Pages workflow runs the same script before deploying.
