#!/opt/homebrew/bin/python3.11
"""Refresh JLCPCB stock for specialized rows in lcsc_manifest.csv and print a decision table."""

from __future__ import annotations

import argparse
import csv
import sys
from pathlib import Path

from easyeda2kicad.easyeda.easyeda_api import EasyedaApi

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "parts" / "lcsc_manifest.csv"


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--manifest", type=Path, default=MANIFEST)
    p.add_argument(
        "--write",
        action="store_true",
        help="Write jlc_stock column back into the manifest for rows with lcsc_id",
    )
    args = p.parse_args()

    with args.manifest.open(newline="", encoding="utf-8") as f:
        rows = list(csv.DictReader(f))
        fieldnames = list(rows[0].keys()) if rows else []

    api = EasyedaApi()
    print(f"{'ref':8} {'lcsc':12} {'stock':>7}  {'status':16}  mpn / notes")
    print("-" * 100)
    for row in rows:
        if (row.get("import_scope") or "").strip().lower() == "passive":
            continue
        lcsc = (row.get("lcsc_id") or "").strip()
        ref = (row.get("ref") or "").strip()
        mpn = (row.get("mpn") or row.get("lcsc_mpn") or "").strip()
        status = (row.get("status") or "").strip()
        stock_s = ""
        if lcsc:
            res = api.search_jlcpcb_components(keyword=lcsc, page=1, page_size=5)
            stock = None
            for it in res.get("results") or []:
                if it.get("lcsc") == lcsc:
                    stock = it.get("stock")
                    break
            if stock is None and (res.get("results") or []):
                # keyword search by C-code sometimes returns empty; try model
                stock = "?"
            else:
                stock = 0 if stock is None else stock
            stock_s = str(stock)
            if args.write and stock != "?":
                row["jlc_stock"] = str(stock)
        else:
            stock_s = "-"
        note = (row.get("notes") or "")[:60]
        print(f"{ref:8} {lcsc or '-':12} {stock_s:>7}  {status:16}  {mpn}  {note}")

    if args.write:
        with args.manifest.open("w", newline="", encoding="utf-8") as f:
            w = csv.DictWriter(f, fieldnames=fieldnames)
            w.writeheader()
            w.writerows(rows)
        print(f"\nUpdated stock column in {args.manifest}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
