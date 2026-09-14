#!/usr/bin/env python3
"""Visualize tag-rssi-adv RTT logs (rssi_pkt lines).

Usage:
  python3 data/plot_tag_rssi.py
  python3 data/plot_tag_rssi.py data/tag-rssi.log
  python3 data/plot_tag_rssi.py data/tag-rssi.log -o data/tag-rssi.png

Requires: matplotlib
"""

from __future__ import annotations

import argparse
import re
import statistics
import sys
from pathlib import Path

PKT_RE = re.compile(
    r"rssi_pkt\s+"
    r"id=(?P<id>\S+)\s+"
    r"seq=(?P<seq>\d+|-)\s+"
    r"rssi=(?P<rssi>-?\d+)\s+"
    r"rx_ant=(?P<rx_ant>\d+)\s+"
    r"tx_ant=(?P<tx_ant>\d+|-)\s+"
    r"uptime_ms=(?P<uptime>\d+)"
)


def parse_log(path: Path) -> list[dict]:
    rows: list[dict] = []
    with path.open(encoding="utf-8", errors="replace") as f:
        for line in f:
            m = PKT_RE.search(line)
            if not m:
                continue
            seq_s = m.group("seq")
            rows.append(
                {
                    "id": m.group("id"),
                    "seq": None if seq_s == "-" else int(seq_s),
                    "rssi": int(m.group("rssi")),
                    "rx_ant": int(m.group("rx_ant")),
                    "tx_ant": m.group("tx_ant"),
                    "uptime_ms": int(m.group("uptime")),
                }
            )
    return rows


def summarize(rows: list[dict]) -> None:
    print(f"packets: {len(rows)}")
    if not rows:
        return
    ids = sorted({r["id"] for r in rows})
    print(f"peers:   {', '.join(ids)}")
    t0 = rows[0]["uptime_ms"]
    t1 = rows[-1]["uptime_ms"]
    print(f"span:    {(t1 - t0) / 1000.0:.1f} s (uptime)")

    for ant in (1, 2):
        vals = [r["rssi"] for r in rows if r["rx_ant"] == ant]
        if not vals:
            print(f"ANT{ant}:   (no samples)")
            continue
        print(
            f"ANT{ant}:   n={len(vals)}  "
            f"mean={statistics.mean(vals):.1f}  "
            f"median={statistics.median(vals):.1f}  "
            f"min={min(vals)}  max={max(vals)} dBm"
        )


def plot(rows: list[dict], out: Path | None) -> None:
    import matplotlib.pyplot as plt

    t0 = rows[0]["uptime_ms"]
    fig, axes = plt.subplots(2, 1, figsize=(10, 7), constrained_layout=True)

    ax = axes[0]
    for ant, color, marker in ((1, "#c44e52", "o"), (2, "#4c72b0", "s")):
        xs = [(r["uptime_ms"] - t0) / 1000.0 for r in rows if r["rx_ant"] == ant]
        ys = [r["rssi"] for r in rows if r["rx_ant"] == ant]
        if xs:
            ax.scatter(xs, ys, s=18, alpha=0.75, c=color, marker=marker, label=f"ANT{ant}")
    ax.set_xlabel("time (s, relative)")
    ax.set_ylabel("RSSI (dBm)")
    ax.set_title("Advertising RSSI by RX antenna")
    ax.grid(True, alpha=0.3)
    ax.legend(loc="best")

    ax = axes[1]
    bins = range(
        min(r["rssi"] for r in rows) - 1,
        max(r["rssi"] for r in rows) + 2,
    )
    for ant, color in ((1, "#c44e52"), (2, "#4c72b0")):
        vals = [r["rssi"] for r in rows if r["rx_ant"] == ant]
        if vals:
            ax.hist(vals, bins=bins, alpha=0.55, color=color, label=f"ANT{ant}", edgecolor="none")
    ax.set_xlabel("RSSI (dBm)")
    ax.set_ylabel("count")
    ax.set_title("RSSI distribution")
    ax.grid(True, axis="y", alpha=0.3)
    ax.legend(loc="best")

    if out:
        fig.savefig(out, dpi=140)
        print(f"wrote {out}")
    else:
        plt.show()


def main() -> int:
    here = Path(__file__).resolve().parent
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument(
        "log",
        nargs="?",
        default=str(here / "tag-rssi.log"),
        help="RTT log path (default: data/tag-rssi.log)",
    )
    p.add_argument("-o", "--output", help="Save figure to path instead of showing")
    args = p.parse_args()

    path = Path(args.log)
    if not path.is_file():
        print(f"not found: {path}", file=sys.stderr)
        return 1

    rows = parse_log(path)
    summarize(rows)
    if not rows:
        return 1

    try:
        plot(rows, Path(args.output) if args.output else None)
    except ImportError:
        print("matplotlib is required: pip install matplotlib", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
