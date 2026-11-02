#!/usr/bin/env python3
"""Build the files the pyJuxta walkthrough loads in the browser.

Writes, under docs/utilities/pyjuxta/assets/:

* ``pyjuxta-<version>-py3-none-any.whl``  the library, installed into Pyodide by the page
* ``manifest.json``                       the wheel name, version, and the example data file list

The assets are generated, not committed. The Pages workflow runs this before deploying; run it yourself
before opening the page locally:

    python3 pyjuxta/tools/build_site_assets.py
    cd docs && python3 -m http.server 8000     # then open /utilities/pyjuxta/
"""

from __future__ import annotations

import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PKG = ROOT / "pyjuxta"
SITE = ROOT / "docs" / "utilities" / "pyjuxta"
ASSETS = SITE / "assets"
DATA = SITE / "data"


def build_wheel() -> Path:
    with tempfile.TemporaryDirectory() as tmp:
        subprocess.run(
            [sys.executable, "-m", "pip", "wheel", "--no-deps", "--quiet", "--wheel-dir", tmp, str(PKG)],
            check=True,
        )
        wheels = list(Path(tmp).glob("pyjuxta-*.whl"))
        if len(wheels) != 1:
            raise SystemExit(f"expected one wheel, found {wheels}")
        for old in ASSETS.glob("pyjuxta-*.whl"):
            old.unlink()
        ASSETS.mkdir(parents=True, exist_ok=True)
        dest = ASSETS / wheels[0].name
        shutil.copy2(wheels[0], dest)
    # pip builds in the source tree; leave it as it was.
    shutil.rmtree(PKG / "build", ignore_errors=True)
    for egg in (PKG / "src").glob("*.egg-info"):
        shutil.rmtree(egg, ignore_errors=True)
    return dest


def dataset_files() -> list[str]:
    if not DATA.is_dir():
        return []
    return sorted(str(p.relative_to(DATA)).replace("\\", "/") for p in DATA.rglob("*.csv"))


def main() -> None:
    wheel = build_wheel()
    version = wheel.name.split("-")[1]
    manifest = {
        "version": version,
        "wheel": wheel.name,
        "dataset": {"base": "../data/", "files": dataset_files()},
    }
    (ASSETS / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"wrote {wheel.relative_to(ROOT)} and manifest.json ({len(manifest['dataset']['files'])} data files)")


if __name__ == "__main__":
    main()
