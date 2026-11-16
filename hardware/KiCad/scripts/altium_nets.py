#!/usr/bin/env python3
"""Rough net extraction from an Altium .SchDoc (binary v5) for reference reading.

Usage: altium_nets.py <file.SchDoc> [REF ...]
Prints nets touching the listed designators (all nets if none given).
"""
from __future__ import annotations

import sys
from collections import defaultdict
from pathlib import Path


def records(path: Path) -> list[dict[str, str]]:
    raw = path.read_bytes()
    text = "".join(chr(b) if 32 <= b < 127 else "\n" for b in raw)
    out = []
    for i, r in enumerate(text.split("|RECORD=")):
        if i == 0:
            continue
        d = {"RECORD": r.split("|", 1)[0].strip()}
        for part in r.split("|")[1:]:
            if "=" in part:
                k, v = part.split("=", 1)
                d[k.strip()] = v.split("\n", 1)[0]
        out.append(d)
    return out


def coord(d: dict[str, str], key: str) -> float:
    v = float(d.get(key, "0") or 0)
    frac = d.get(key + "_Frac") or d.get(key.replace(".", "_") + "_Frac")
    if frac:
        v += float(frac) / 100000.0
    return v


def main() -> None:
    path = Path(sys.argv[1])
    want = set(sys.argv[2:])
    recs = records(path)
    # Index -> record; Altium OwnerIndex counts records from the header (index 0).
    comps: dict[int, str] = {}
    values: dict[int, str] = {}
    for idx, d in enumerate(recs, start=1):
        if d["RECORD"] == "34" and d.get("Name") == "Designator":
            comps[int(d["OwnerIndex"])] = d.get("Text", "?")
        if d["RECORD"] == "41" and d.get("Name") in ("Value", "Comment"):
            values.setdefault(int(d["OwnerIndex"]), d.get("Text", ""))

    pts: list[tuple[float, float, str]] = []
    for d in recs:
        if d["RECORD"] == "2":
            owner = int(d.get("OwnerIndex", "-1"))
            x, y = coord(d, "Location.X"), coord(d, "Location.Y")
            ln = coord(d, "PinLength")
            rot = int(d.get("PinConglomerate", "0")) & 3
            dx, dy = [(ln, 0), (0, ln), (-ln, 0), (0, -ln)][rot]
            ref = comps.get(owner, f"#{owner}")
            pts.append((x + dx, y + dy, f"{ref}.{d.get('Designator')}({d.get('Name')})"))
        elif d["RECORD"] in ("25", "17", "18"):
            tag = {"25": "NET", "17": "PWR", "18": "PORT"}[d["RECORD"]]
            pts.append((coord(d, "Location.X"), coord(d, "Location.Y"), f"{tag}:{d.get('Text')}"))

    # Union-find over wire segments + points lying on them.
    segs = []
    for d in recs:
        if d["RECORD"] == "27":
            n = int(d.get("LocationCount", "2") or 2)
            vs = [(coord(d, f"X{i}"), coord(d, f"Y{i}")) for i in range(1, n + 1)]
            segs.append(vs)

    parent: dict[int, int] = {}

    def find(a: int) -> int:
        parent.setdefault(a, a)
        while parent[a] != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a

    def union(a: int, b: int) -> None:
        parent[find(a)] = find(b)

    def on_seg(p, a, b, tol=0.6) -> bool:
        (px, py), (ax, ay), (bx, by) = p, a, b
        if abs(ax - bx) < tol:
            return abs(px - ax) < tol and min(ay, by) - tol <= py <= max(ay, by) + tol
        if abs(ay - by) < tol:
            return abs(py - ay) < tol and min(ax, bx) - tol <= px <= max(ax, bx) + tol
        return False

    nodes = [(x, y) for x, y, _ in pts]
    seg_ids = list(range(len(pts), len(pts) + len(segs)))
    for si, vs in zip(seg_ids, segs):
        find(si)
        for pi, p in enumerate(nodes):
            if any(on_seg(p, vs[k], vs[k + 1]) for k in range(len(vs) - 1)):
                union(pi, si)
    # Segments touching each other at endpoints.
    for i, a in enumerate(segs):
        for j, b in enumerate(segs):
            if j <= i:
                continue
            ends_a = [a[0], a[-1]]
            ends_b = [b[0], b[-1]]
            if any(on_seg(e, b[k], b[k + 1]) for e in ends_a for k in range(len(b) - 1)) or any(
                on_seg(e, a[k], a[k + 1]) for e in ends_b for k in range(len(a) - 1)
            ):
                union(seg_ids[i], seg_ids[j])
    # Coincident points (pin directly on pin / label on pin).
    for i in range(len(nodes)):
        for j in range(i + 1, len(nodes)):
            if abs(nodes[i][0] - nodes[j][0]) < 0.6 and abs(nodes[i][1] - nodes[j][1]) < 0.6:
                union(i, j)

    nets = defaultdict(list)
    for i, (_, _, name) in enumerate(pts):
        nets[find(i)].append(name)

    # Merge groups sharing a NET/PWR name.
    by_name: dict[str, set[str]] = defaultdict(set)
    loose = []
    for members in nets.values():
        names = [m for m in members if m.startswith(("NET:", "PWR:", "PORT:"))]
        if names:
            by_name[names[0]].update(members)
        else:
            loose.append(members)
    groups = [sorted(v) for v in by_name.values()] + [sorted(v) for v in loose]
    for g in groups:
        refs = {m.split(".")[0] for m in g if "." in m}
        if want and not (refs & want):
            continue
        if len(g) < 2:
            continue
        print(" | ".join(g))
    if want:
        print("\nvalues:", {comps[k]: values.get(k, "") for k in comps if comps[k] in want})


if __name__ == "__main__":
    main()
