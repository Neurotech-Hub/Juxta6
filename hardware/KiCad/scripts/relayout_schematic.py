#!/usr/bin/env python3
"""Re-place juxta6_v1-0 child sheets for legibility from juxta6_netmap.json.

Every connected pin gets a short outward stub ending in exactly one terminal
(global/local label or VDD/GND power symbol). Symbols sit in non-overlapping
cells on the 2.54 mm grid, so stubs and labels never touch another pin.
No component-to-component wires are drawn.

Usage: relayout_schematic.py <snapshot_dir>
Reads sheets from <snapshot_dir>, writes into juxta6_v1-0/.
"""
from __future__ import annotations

import json
import re
import sys
import uuid
from collections import Counter
from pathlib import Path

import sch_model as m

ROOT = Path(__file__).resolve().parents[1]
PROJ = ROOT / "juxta6_v1-0"
ROOT_UUID = "698e8d87-293e-4fa1-bca5-9aae2c2e31e2"
PROJECT = "juxta6_v1-0"

STUB = 2.54
CHAR_W = 1.05  # ~stroke-font advance at 1.27 mm
GAP = 7.62  # clearance between cells
POWER_NETS = ("VDD", "GND")
PASSIVE_LIBS = ("Device:R", "Device:C", "Device:L", "Device:FerriteBead")

# Functional rows per sheet (left -> right, top -> bottom).
ROWS: dict[str, list[list[str]]] = {
    "01_power": [
        ["U2", "L1", "R1", "R2", "R3"],
        ["C1", "C3", "C6", "C2", "C4", "C5", "FB1", "FB2"],
        ["Bat1", "J1"],
    ],
    "02_mcu_clocks": [
        ["X1", "C26", "X2", "C27", "C28", "L2", "R4"],
        [
            "U1",
            [
                ["C10", "C11", "C12", "C13"],
                ["C14", "C15", "C16", "C17"],
                ["C18", "C19", "C20", "C21"],
                ["C22", "C23", "C24", "C25"],
            ],
        ],
    ],
    "03_rf": [
        ["U6", "R5", "C30"],
        ["C31", "C32", "C33", "C34", "C35", "C36", "C37", "C38", "C39", "C40", "C41", "C42"],
        ["L3", "L4", "L5", "L6"],
        ["A1", "A2"],
    ],
    "04_sensors": [
        ["U3", "R6", "R7", "C50", "C51"],
        ["U4", "R8", "R9", "C52", "C53", "C54", "C55"],
    ],
    "05_peripherals": [
        ["U8", "R10", "C60", "C61"],
        ["LED1", "R11", "R12", "R13"],
        ["SW1", "R17", "C63"],
        ["Q1", "R14", "C62", "Q2", "R15", "R16", "Q3"],
    ],
    "06_debug": [["TP1", "TP2", "TP3", "TP4", "TP5"]],
}

ORIGIN = (25.4, 50.8)
MAX_X = 395.0


def uid() -> str:
    return str(uuid.uuid4())


def snap(v: float, g: float = 2.54) -> float:
    return round(round(v / g) * g, 4)


def fmt(v: float) -> str:
    s = f"{v:.4f}".rstrip("0").rstrip(".")
    return "0" if s == "-0" else s


# ---------------------------------------------------------------- text scanning
def node_end(text: str, i: int) -> int:
    """Index just past the node starting at text[i] == '('."""
    depth = 0
    in_str = esc = False
    for j in range(i, len(text)):
        c = text[j]
        if in_str:
            if esc:
                esc = False
            elif c == "\\":
                esc = True
            elif c == '"':
                in_str = False
            continue
        if c == '"':
            in_str = True
        elif c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
            if depth == 0:
                return j + 1
    raise ValueError("unbalanced")


def child_spans(text: str, start: int, end: int) -> list[tuple[int, int, str]]:
    """Direct children of the node text[start:end]."""
    out = []
    i = text.index("(", start) + 1
    in_str = esc = False
    while i < end - 1:
        c = text[i]
        if in_str:
            if esc:
                esc = False
            elif c == "\\":
                esc = True
            elif c == '"':
                in_str = False
            i += 1
            continue
        if c == '"':
            in_str = True
            i += 1
        elif c == "(":
            e = node_end(text, i)
            j = i + 1
            while j < e and text[j] not in " \t\n()":
                j += 1
            out.append((i, e, text[i + 1 : j]))
            i = e
        else:
            i += 1
    return out


# ---------------------------------------------------------------- geometry
def text_w(s: str) -> float:
    return len(s) * CHAR_W


def term_len(net: str, kind: str) -> float:
    if net in POWER_NETS:
        return 7.6
    if kind == "global":
        return text_w(net) + 3.5
    return text_w(net) + 0.8


class Placement:
    def __init__(self, inst: m.Inst, lib: m.LibSym, pins: list[m.Pin], nets: dict[str, str], kinds):
        self.inst = inst
        self.lib = lib
        self.nets = nets  # pin number -> net
        self.kinds = kinds
        # pins relative to origin
        self.rpins = [
            (p, p.x - inst.x, p.y - inst.y) for p in pins
        ]
        minx, miny, maxx, maxy = lib.bbox
        self.body = (minx, -maxy, maxx, -miny)
        self.passive = inst.lib_id in PASSIVE_LIBS or inst.lib_id == "Connector:TestPoint"
        self._extents()

    def _extents(self):
        x0, y0, x1, y1 = self.body
        for p, rx, ry in self.rpins:
            x0, y0, x1, y1 = min(x0, rx), min(y0, ry), max(x1, rx), max(y1, ry)
            net = self.nets.get(p.number)
            if not net:
                continue
            dx, dy = p.out_dir
            L = STUB + term_len(net, self.kinds(net))
            ex, ey = rx + dx * L, ry + dy * L
            half = 1.6 if net not in POWER_NETS else 2.0
            if dx:
                x0, x1 = min(x0, ex), max(x1, ex)
                y0, y1 = min(y0, ry - half), max(y1, ry + half)
            else:
                y0, y1 = min(y0, ey), max(y1, ey)
                x0, x1 = min(x0, rx - half), max(x1, rx + half)
        self.core = (x0, y0, x1, y1)
        ref = self.inst.ref
        val = self.inst.value
        if self.passive:
            bx1 = self.body[2]
            tw = max(text_w(ref), text_w(val))
            self.ref_at = (bx1 + 1.27, -1.27, "left")
            self.val_at = (bx1 + 1.27, 1.27, "left")
            x1 = max(x1, bx1 + 1.27 + tw)
        else:
            cx = (self.body[0] + self.body[2]) / 2
            self.ref_at = (cx, y0 - 1.9, None)
            self.val_at = (cx, y1 + 1.9, None)
            tw = max(text_w(ref), text_w(val)) / 2
            x0, x1 = min(x0, cx - tw), max(x1, cx + tw)
            y0, y1 = y0 - 3.2, y1 + 3.2
        self.ext = (x0, y0, x1, y1)

    @property
    def w(self):
        return self.ext[2] - self.ext[0]

    @property
    def h(self):
        return self.ext[3] - self.ext[1]


# ---------------------------------------------------------------- emitters
def wire(x1, y1, x2, y2) -> str:
    return (
        f"\t(wire\n\t\t(pts\n\t\t\t(xy {fmt(x1)} {fmt(y1)}) (xy {fmt(x2)} {fmt(y2)})\n\t\t)\n"
        f"\t\t(stroke\n\t\t\t(width 0)\n\t\t\t(type default)\n\t\t)\n\t\t(uuid \"{uid()}\")\n\t)"
    )


def dir_rot(dx, dy) -> int:
    return {(1, 0): 0, (0, -1): 90, (-1, 0): 180, (0, 1): 270}[(dx, dy)]


def label(net, x, y, dx, dy, kind) -> str:
    rot = dir_rot(dx, dy)
    just = "left" if rot in (0, 90) else "right"
    if kind == "global":
        return (
            f"\t(global_label \"{net}\"\n\t\t(shape bidirectional)\n\t\t(at {fmt(x)} {fmt(y)} {rot})\n"
            f"\t\t(fields_autoplaced yes)\n"
            f"\t\t(effects\n\t\t\t(font\n\t\t\t\t(size 1.27 1.27)\n\t\t\t)\n\t\t\t(justify {just})\n\t\t)\n"
            f"\t\t(uuid \"{uid()}\")\n\t)"
        )
    return (
        f"\t(label \"{net}\"\n\t\t(at {fmt(x)} {fmt(y)} {rot})\n"
        f"\t\t(effects\n\t\t\t(font\n\t\t\t\t(size 1.27 1.27)\n\t\t\t)\n\t\t\t(justify {just} bottom)\n\t\t)\n"
        f"\t\t(uuid \"{uid()}\")\n\t)"
    )


POWER_ROT = {
    "VDD": {(0, -1): 0, (0, 1): 180, (1, 0): 270, (-1, 0): 90},
    "GND": {(0, 1): 0, (0, -1): 180, (1, 0): 90, (-1, 0): 270},
}


def power(net, x, y, dx, dy, ref, sheet_path, stacked: bool) -> str:
    """Field angles are relative to the symbol: 90 keeps sideways text horizontal.

    stacked=True (IC top/bottom edge, 2.54 mm pin pitch) draws the value vertically.
    """
    rot = POWER_ROT[net][(dx, dy)]
    if dx:
        vx, vy, fa = x + dx * 3.81, y, 90
        just = "left" if net == "VDD" else "right"
    elif stacked:
        vx, vy, fa, just = x, y + dy * 5.5, 90, None
    else:
        vx, vy, fa, just = x, y + dy * 4.45, 0, None
    jtxt = f"\n\t\t\t\t(justify {just})" if just else ""
    return f"""\t(symbol
\t\t(lib_id "power:{net}")
\t\t(at {fmt(x)} {fmt(y)} {rot})
\t\t(unit 1)
\t\t(exclude_from_sim no)
\t\t(in_bom yes)
\t\t(on_board yes)
\t\t(dnp no)
\t\t(uuid "{uid()}")
\t\t(property "Reference" "{ref}"
\t\t\t(at {fmt(x)} {fmt(y)} 0)
\t\t\t(effects
\t\t\t\t(font
\t\t\t\t\t(size 1.27 1.27)
\t\t\t\t)
\t\t\t\t(hide yes)
\t\t\t)
\t\t)
\t\t(property "Value" "{net}"
\t\t\t(at {fmt(vx)} {fmt(vy)} {fa})
\t\t\t(effects
\t\t\t\t(font
\t\t\t\t\t(size 1.27 1.27)
\t\t\t\t){jtxt}
\t\t\t)
\t\t)
\t\t(property "Footprint" ""
\t\t\t(at {fmt(x)} {fmt(y)} 0)
\t\t\t(effects
\t\t\t\t(font
\t\t\t\t\t(size 1.27 1.27)
\t\t\t\t)
\t\t\t\t(hide yes)
\t\t\t)
\t\t)
\t\t(property "Datasheet" "~"
\t\t\t(at {fmt(x)} {fmt(y)} 0)
\t\t\t(effects
\t\t\t\t(font
\t\t\t\t\t(size 1.27 1.27)
\t\t\t\t)
\t\t\t\t(hide yes)
\t\t\t)
\t\t)
\t\t(pin "1"
\t\t\t(uuid "{uid()}")
\t\t)
\t\t(instances
\t\t\t(project "{PROJECT}"
\t\t\t\t(path "{sheet_path}"
\t\t\t\t\t(reference "{ref}")
\t\t\t\t\t(unit 1)
\t\t\t\t)
\t\t\t)
\t\t)
\t)"""


def prop_node(name, value, x, y, just) -> str:
    jtxt = f"\n\t\t\t\t(justify {just})" if just else ""
    return (
        f"(property \"{name}\" \"{value}\"\n\t\t\t(at {fmt(x)} {fmt(y)} 0)\n"
        f"\t\t\t(effects\n\t\t\t\t(font\n\t\t\t\t\t(size 1.27 1.27)\n\t\t\t\t){jtxt}\n\t\t\t)\n\t\t)"
    )


def rewrite_instance(text: str, s: int, e: int, pl: Placement, nx: float, ny: float) -> str:
    seg = text[s:e]
    dx, dy = nx - pl.inst.x, ny - pl.inst.y
    out = []
    last = 0
    for cs, ce, h in child_spans(seg, 0, len(seg)):
        out.append(seg[last:cs])
        node = seg[cs:ce]
        if h == "at":
            node = f"(at {fmt(nx)} {fmt(ny)} {pl.inst.rot})"
        elif h == "property":
            name = node.split('"')[1]
            if name == "Reference":
                rx, ry, j = pl.ref_at
                node = prop_node("Reference", pl.inst.ref, nx + rx, ny + ry, j)
            elif name == "Value":
                vx, vy, j = pl.val_at
                node = prop_node("Value", pl.inst.value, nx + vx, ny + vy, j)
            else:
                for ps, pe, ph in child_spans(node, 0, len(node)):
                    if ph == "at":
                        parts = node[ps + 1 : pe - 1].split()
                        ax, ay = float(parts[1]) + dx, float(parts[2]) + dy
                        rest = " ".join(parts[3:])
                        node = node[:ps] + f"(at {fmt(ax)} {fmt(ay)}{(' ' + rest) if rest else ''})" + node[pe:]
                        break
        out.append(node)
        last = ce
    out.append(seg[last:])
    return "".join(out)


def point_in_poly(x: float, y: float, poly: list[tuple[float, float]]) -> bool:
    inside = False
    n = len(poly)
    for i in range(n):
        x1, y1 = poly[i]
        x2, y2 = poly[(i + 1) % n]
        if (y1 > y) != (y2 > y) and x < (x2 - x1) * (y - y1) / (y2 - y1) + x1:
            inside = not inside
    return inside


def extract_lib(text: str, lib_id: str) -> str | None:
    i = text.find(f'(symbol "{lib_id}"')
    if i < 0:
        return None
    return text[i : node_end(text, i)]


# ---------------------------------------------------------------- main
def main() -> None:
    snapdir = Path(sys.argv[1])
    netmap = json.loads((PROJ / "juxta6_netmap.json").read_text())
    usage = Counter()
    for sheet, pins in netmap.items():
        for net in set(pins.values()):
            usage[net] += 1

    def kind_of(net: str) -> str:
        return "global" if usage[net] > 1 else "local"

    gnd_lib = extract_lib((snapdir / "04_sensors.kicad_sch").read_text(), "power:GND")
    pwr_n = 0

    for sheet, rows in ROWS.items():
        src = snapdir / f"{sheet}.kicad_sch"
        text = src.read_text()
        sh = m.load(src)
        sheet_uuid = m.child(sh.tree, "uuid")[1]
        sheet_path = f"/{ROOT_UUID}/{sheet_uuid}"
        pins_by_ref: dict[str, dict[str, str]] = {}
        for k, net in netmap[sheet].items():
            ref, num = k.split(".", 1)
            pins_by_ref.setdefault(ref, {})[num] = net

        insts = {i.ref: i for i in sh.insts if not sh.libs[i.lib_id].is_power}

        def flat(items):
            for it in items:
                if isinstance(it, list):
                    yield from flat(it)
                else:
                    yield it

        missing = set(insts) - set(flat(rows))
        if missing:
            rows = rows + [sorted(missing)]
        pls = {
            ref: Placement(inst, sh.libs[inst.lib_id], m.inst_pins(sh, inst), pins_by_ref.get(ref, {}), kind_of)
            for ref, inst in insts.items()
        }

        pos: dict[str, tuple[float, float]] = {}

        def pack_row(row, x0: float, y0: float) -> tuple[float, float]:
            """Pack one row at (x0, y0); nested lists are stacked sub-rows. Returns (w, h)."""
            x, y, row_h, max_x = x0, y0, 0.0, x0
            first = True
            for it in row:
                if isinstance(it, list):
                    by, bw = y0, 0.0
                    for sub in it:
                        sw, shh = pack_row(sub, x, by)
                        by += shh + GAP
                        bw = max(bw, sw)
                    x += bw + GAP
                    row_h = max(row_h, by - GAP - y0)
                    max_x = max(max_x, x - GAP)
                    first = False
                    continue
                if it not in pls:
                    continue
                pl = pls[it]
                if not first and x + pl.w > MAX_X:
                    y += row_h + GAP
                    x, row_h = x0, 0.0
                ox = snap(x - pl.ext[0])
                oy = snap(y - pl.ext[1])
                pos[it] = (ox, oy)
                x = ox + pl.ext[2] + GAP
                row_h = max(row_h, oy + pl.ext[3] - y)
                max_x = max(max_x, x - GAP)
                first = False
            return max_x - x0, (y - y0) + row_h

        y = ORIGIN[1]
        for row in rows:
            _, h = pack_row(row, ORIGIN[0], y)
            y += h + GAP

        # Rule areas keep enclosing the same parts they enclosed in the snapshot.
        members = set(pos)
        for cs, ce, h in child_spans(text, text.index("(kicad_sch"), node_end(text, text.index("(kicad_sch"))):
            if h == "rule_area":
                poly = [
                    (float(a), float(b))
                    for a, b in re.findall(r"\(xy ([-0-9.]+) ([-0-9.]+)\)", text[cs:ce])
                ]
                members = {r for r in pos if point_in_poly(insts[r].x, insts[r].y, poly)}
        bx0, by0, bx1, by1 = 20.0, 20.0, 20.0, 20.0
        for ref, (ox, oy) in pos.items():
            if ref not in members:
                continue
            e = pls[ref].ext
            bx0, by0 = min(bx0, ox + e[0]), min(by0, oy + e[1])
            bx1, by1 = max(bx1, ox + e[2]), max(by1, oy + e[3])
        rx0, ry0 = snap(min(bx0, 15.24)), snap(min(by0, 15.24))
        rx1, ry1 = snap(bx1 + 5.08), snap(by1 + 5.08)
        for ref in set(pos) - members:
            ox, oy = pos[ref]
            e = pls[ref].ext
            if ox + e[0] < rx1 and ox + e[2] > rx0 and oy + e[1] < ry1 and oy + e[3] > ry0:
                print(f"WARN {sheet}: {ref} overlaps rule area it was outside of")

        # rebuild file
        root_s = text.index("(kicad_sch")
        root_e = node_end(text, root_s)
        out = [text[:root_s]]
        last = root_s
        new_items: list[str] = []
        drop = {"wire", "global_label", "label", "junction", "no_connect", "hierarchical_label"}
        inst_iter = iter(sh.insts)
        for cs, ce, h in child_spans(text, root_s, root_e):
            node_text = text[cs:ce]
            if h in drop:
                out.append(text[last:cs].rstrip("\t"))
                last = ce
                continue
            if h == "symbol":
                inst = next(inst_iter)
                if sh.libs[inst.lib_id].is_power:
                    out.append(text[last:cs].rstrip("\t"))
                    last = ce
                    continue
                pl = pls[inst.ref]
                nx, ny = pos[inst.ref]
                out.append(text[last:cs])
                out.append(rewrite_instance(text, cs, ce, pl, nx, ny))
                last = ce
                continue
            if h == "rule_area":
                ps = node_text.index("(pts")
                pe = node_end(node_text, ps)
                rect = (
                    f"(pts\n\t\t\t\t(xy {fmt(rx0)} {fmt(ry0)}) (xy {fmt(rx0)} {fmt(ry1)}) "
                    f"(xy {fmt(rx1)} {fmt(ry1)}) (xy {fmt(rx1)} {fmt(ry0)})\n\t\t\t)"
                )
                out.append(text[last:cs])
                out.append(node_text[:ps] + rect + node_text[pe:])
                last = ce
                continue
            if h == "lib_symbols" and gnd_lib and '(symbol "power:GND"' not in node_text:
                out.append(text[last:cs])
                close = node_text.rstrip()[:-1]
                out.append(close + "\n\t" + gnd_lib.replace("\n", "\n\t") + "\n\t)")
                last = ce
                continue
            if h == "sheet_instances":
                out.append(text[last:cs])
                # emit stubs/terminals
                for ref, pl in pls.items():
                    nx, ny = pos[ref]
                    for p, rx, ry in pl.rpins:
                        net = pl.nets.get(p.number)
                        if not net:
                            continue
                        dx, dy = p.out_dir
                        px, py = nx + rx, ny + ry
                        tx, ty = px + dx * STUB, py + dy * STUB
                        new_items.append(wire(px, py, tx, ty))
                        if net in POWER_NETS:
                            pwr_n += 1
                            new_items.append(
                                power(net, tx, ty, dx, dy, f"#PWR{pwr_n:04d}", sheet_path, not pl.passive)
                            )
                        else:
                            new_items.append(label(net, tx, ty, dx, dy, kind_of(net)))
                out.append("\n".join(new_items) + "\n\t")
                out.append(text[cs:ce])
                last = ce
                continue
        out.append(text[last:])
        new = "".join(out)
        (PROJ / f"{sheet}.kicad_sch").write_text(new)
        print(f"{sheet}: placed {len(pos)} symbols, {sum(len(v) for v in pins_by_ref.values())} terminals, bottom y={y:.1f}")


if __name__ == "__main__":
    main()
