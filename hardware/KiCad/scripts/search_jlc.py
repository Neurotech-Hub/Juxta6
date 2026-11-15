#!/opt/homebrew/bin/python3.11
"""Search JLCPCB/LCSC parts via easyeda2kicad's API helper."""

from __future__ import annotations

import argparse
import sys

try:
    from easyeda2kicad.easyeda.easyeda_api import EasyedaApi
except ImportError as e:
    sys.stderr.write(
        "easyeda2kicad not importable from this Python.\n"
        "Use the Homebrew Python that owns the CLI, e.g.\n"
        "  /opt/homebrew/bin/python3.11 hardware/KiCad/scripts/search_jlc.py …\n"
        f"({e})\n"
    )
    sys.exit(1)


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("keyword", help="MPN or keyword (e.g. BME688, MX25L3233FZBI)")
    p.add_argument("--page", type=int, default=1)
    p.add_argument("--page-size", type=int, default=10)
    p.add_argument(
        "--type",
        choices=("base", "expand"),
        default=None,
        help="JLCPCB Basic (base) or Extended (expand) library filter",
    )
    args = p.parse_args()

    api = EasyedaApi()
    res = api.search_jlcpcb_components(
        keyword=args.keyword,
        page=args.page,
        page_size=args.page_size,
        part_type=args.type,
    )
    print(f"total={res.get('total')}  keyword={args.keyword!r}")
    for it in res.get("results") or []:
        print(
            f"{it.get('lcsc'):12}  stock={it.get('stock'):>7}  "
            f"{it.get('type'):8}  pkg={it.get('package')}  "
            f"{it.get('name')}  model={it.get('model')}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
