#!/opt/homebrew/bin/python3.11
"""Pull LCSC specialized parts from parts/lcsc_manifest.csv into the juxta6 lib.

Skips passives (import_scope=passive) and rows without an lcsc_id by default.
Only pulls pending_import / failed unless --all-specialized or explicit IDs.
"""

from __future__ import annotations

import argparse
import csv
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "parts" / "lcsc_manifest.csv"
DEFAULT_OUT = ROOT / "juxta6_v1-0" / "libs" / "juxta6"
EASYEDA = "easyeda2kicad"


def load_rows(manifest: Path) -> list[dict[str, str]]:
    with manifest.open(newline="", encoding="utf-8") as f:
        return list(csv.DictReader(f))


def should_pull(row: dict[str, str], *, all_specialized: bool, ids: set[str] | None) -> bool:
    lcsc = (row.get("lcsc_id") or "").strip()
    if not lcsc:
        return False
    if ids is not None:
        return lcsc in ids
    scope = (row.get("import_scope") or "").strip().lower()
    if scope == "passive":
        return False
    status = (row.get("status") or "").strip().lower()
    if all_specialized:
        return scope == "specialized"
    return status in {"pending_import", "failed"}


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--manifest", type=Path, default=MANIFEST)
    p.add_argument(
        "--output",
        type=Path,
        default=DEFAULT_OUT,
        help="easyeda2kicad --output prefix (.kicad_sym / .pretty / .3dshapes)",
    )
    p.add_argument("--dry-run", action="store_true")
    p.add_argument("--overwrite", action="store_true")
    p.add_argument(
        "--all-specialized",
        action="store_true",
        help="Re-pull every specialized row that has an lcsc_id (not only pending)",
    )
    p.add_argument(
        "--sleep",
        type=float,
        default=3.0,
        help="Seconds between pulls (EasyEDA rate-limits batches)",
    )
    p.add_argument("lcsc_ids", nargs="*", help="Optional subset of C###### IDs")
    args = p.parse_args()

    ids = set(args.lcsc_ids) if args.lcsc_ids else None
    rows = [r for r in load_rows(args.manifest) if should_pull(r, all_specialized=args.all_specialized, ids=ids)]

    if not rows:
        print("No specialized LCSC IDs to pull.", file=sys.stderr)
        return 1

    failed: list[str] = []
    for row in rows:
        lcsc = row["lcsc_id"].strip()
        ref = (row.get("ref") or "").strip()
        mpn = (row.get("mpn") or "").strip()
        cmd = [
            EASYEDA,
            "--full",
            "--lcsc_id",
            lcsc,
            "--output",
            str(args.output),
            "--project-relative",
            "--use-cache",
        ]
        if args.overwrite:
            cmd.append("--overwrite")
        print(f"# {ref} {mpn} -> {lcsc}")
        if args.dry_run:
            print(" ", " ".join(cmd))
            continue
        r = subprocess.run(cmd, check=False)
        if r.returncode != 0:
            print(f"FAILED {lcsc} (exit {r.returncode})", file=sys.stderr)
            failed.append(lcsc)
        time.sleep(args.sleep)

    if failed:
        print(f"Failed ({len(failed)}): {', '.join(failed)}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
