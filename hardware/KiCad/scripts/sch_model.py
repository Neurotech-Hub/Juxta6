"""Minimal KiCad 10 schematic model: symbols, absolute pin endpoints, wires, labels."""
from __future__ import annotations

import math
import re
from dataclasses import dataclass, field
from pathlib import Path

import sexpdata
from sexpdata import Symbol

GRID = 0.01


def key(x: float, y: float) -> tuple[int, int]:
    return (round(x / GRID), round(y / GRID))


def head(node) -> str | None:
    if isinstance(node, list) and node and isinstance(node[0], Symbol):
        return node[0].value()
    return None


def children(node, name: str):
    return [c for c in node if head(c) == name]


def child(node, name: str):
    for c in node:
        if head(c) == name:
            return c
    return None


@dataclass
class LibPin:
    number: str
    name: str
    x: float
    y: float
    angle: int
    etype: str


@dataclass
class LibSym:
    lib_id: str
    pins: list[LibPin]
    is_power: bool
    bbox: tuple[float, float, float, float]  # lib coords minx, miny, maxx, maxy (y-up)


@dataclass
class Inst:
    ref: str
    lib_id: str
    x: float
    y: float
    rot: int
    mirror: str | None
    value: str
    node: list


@dataclass
class Pin:
    ref: str
    number: str
    name: str
    x: float
    y: float
    out_dir: tuple[int, int]  # outward unit vector in schematic coords (y-down)


@dataclass
class Sheet:
    path: Path
    tree: list
    libs: dict[str, LibSym] = field(default_factory=dict)
    insts: list[Inst] = field(default_factory=list)
    wires: list[tuple[float, float, float, float]] = field(default_factory=list)
    labels: list[tuple[str, float, float, str]] = field(default_factory=list)  # name,x,y,kind


def _walk_graphics(node, acc: list[tuple[float, float]]):
    if not isinstance(node, list):
        return
    h = head(node)
    if h in ("xy", "start", "end", "center", "at") and len(node) >= 3:
        try:
            acc.append((float(node[1]), float(node[2])))
        except (TypeError, ValueError):
            pass
    if h == "property":
        return
    for c in node:
        _walk_graphics(c, acc)


def parse_lib_symbol(node) -> LibSym:
    lib_id = node[1]
    pins: list[LibPin] = []
    pts: list[tuple[float, float]] = []
    is_power = child(node, "power") is not None

    def visit(n):
        for c in n:
            hc = head(c)
            if hc == "symbol":
                visit(c)
            elif hc == "pin":
                at = child(c, "at")
                nm = child(c, "name")
                num = child(c, "number")
                pins.append(
                    LibPin(
                        number=str(num[1]),
                        name=str(nm[1]),
                        x=float(at[1]),
                        y=float(at[2]),
                        angle=int(float(at[3])) if len(at) > 3 else 0,
                        etype=c[1].value() if isinstance(c[1], Symbol) else str(c[1]),
                    )
                )
                pts.append((float(at[1]), float(at[2])))
            elif hc in ("rectangle", "polyline", "circle", "arc", "text"):
                _walk_graphics(c, pts)

    visit(node)
    if pts:
        xs = [p[0] for p in pts]
        ys = [p[1] for p in pts]
        bbox = (min(xs), min(ys), max(xs), max(ys))
    else:
        bbox = (0, 0, 0, 0)
    return LibSym(lib_id=lib_id, pins=pins, is_power=is_power, bbox=bbox)


def rot_vec(x: float, y: float, rot: int) -> tuple[float, float]:
    """Rotate lib-space (y-up) vector CCW by rot degrees."""
    a = math.radians(rot)
    return (x * math.cos(a) - y * math.sin(a), x * math.sin(a) + y * math.cos(a))


def load(path: Path) -> Sheet:
    tree = sexpdata.loads(path.read_text())
    sh = Sheet(path=path, tree=tree)
    ls = child(tree, "lib_symbols")
    if ls:
        for s in children(ls, "symbol"):
            lsym = parse_lib_symbol(s)
            sh.libs[lsym.lib_id] = lsym
    for s in children(tree, "symbol"):
        lib_id = child(s, "lib_id")[1]
        at = child(s, "at")
        mir = child(s, "mirror")
        ref = val = ""
        for p in children(s, "property"):
            if p[1] == "Reference":
                ref = p[2]
            elif p[1] == "Value":
                val = p[2]
        sh.insts.append(
            Inst(
                ref=ref,
                lib_id=lib_id,
                x=float(at[1]),
                y=float(at[2]),
                rot=int(float(at[3])) if len(at) > 3 else 0,
                mirror=mir[1].value() if mir else None,
                value=val,
                node=s,
            )
        )
    for w in children(tree, "wire"):
        pts = child(w, "pts")
        xy = children(pts, "xy")
        sh.wires.append((float(xy[0][1]), float(xy[0][2]), float(xy[1][1]), float(xy[1][2])))
    for kind in ("global_label", "label", "hierarchical_label"):
        for lab in children(tree, kind):
            at = child(lab, "at")
            sh.labels.append((lab[1], float(at[1]), float(at[2]), kind))
    return sh


def inst_pins(sh: Sheet, inst: Inst) -> list[Pin]:
    lib = sh.libs[inst.lib_id]
    out = []
    for p in lib.pins:
        px, py = p.x, p.y
        if inst.mirror == "x":
            py = -py
        elif inst.mirror == "y":
            px = -px
        rx, ry = rot_vec(px, py, inst.rot)
        ax, ay = inst.x + rx, inst.y - ry
        # pin angle = direction from endpoint toward body; outward is opposite
        bdx, bdy = rot_vec(math.cos(math.radians(p.angle)), math.sin(math.radians(p.angle)), inst.rot)
        ox, oy = -bdx, bdy  # flip y for schematic, negate for outward
        out.append(
            Pin(
                ref=inst.ref,
                number=p.number,
                name=p.name,
                x=round(ax, 4),
                y=round(ay, 4),
                out_dir=(int(round(ox)), int(round(oy))),
            )
        )
    return out


def power_net(sh: Sheet, inst: Inst) -> str | None:
    lib = sh.libs.get(inst.lib_id)
    if lib and lib.is_power:
        return inst.value
    return None


def intended_nets(sh: Sheet):
    """Map (ref, pin_number) -> set(net names) by following wires endpoint-to-endpoint only.

    Pins of other components reached through a chain are recorded as pin links so that
    component-to-component wires keep their connection.
    """
    comp_pins: dict[tuple[int, int], list[Pin]] = {}
    power_at: dict[tuple[int, int], str] = {}
    for inst in sh.insts:
        pn = power_net(sh, inst)
        if pn:
            power_at[key(inst.x, inst.y)] = pn
            continue
        for p in inst_pins(sh, inst):
            comp_pins.setdefault(key(p.x, p.y), []).append(p)
    label_at: dict[tuple[int, int], list[str]] = {}
    for name, x, y, _ in sh.labels:
        label_at.setdefault(key(x, y), []).append(name)
    adj: dict[tuple[int, int], list[tuple[int, int]]] = {}
    for x1, y1, x2, y2 in sh.wires:
        a, b = key(x1, y1), key(x2, y2)
        adj.setdefault(a, []).append(b)
        adj.setdefault(b, []).append(a)

    result: dict[tuple[str, str], dict] = {}
    for k0, plist in comp_pins.items():
        for p in plist:
            names: set[str] = set()
            links: set[tuple[str, str]] = set()
            names.update(label_at.get(k0, []))
            if k0 in power_at:
                names.add(power_at[k0])
            for other in plist:
                if other is not p:
                    links.add((other.ref, other.number))
            seen = {k0}
            stack = list(adj.get(k0, []))
            while stack:
                k = stack.pop()
                if k in seen:
                    continue
                seen.add(k)
                names.update(label_at.get(k, []))
                if k in power_at:
                    names.add(power_at[k])
                if k in comp_pins:
                    for o in comp_pins[k]:
                        links.add((o.ref, o.number))
                    continue  # don't traverse through another pin
                stack.extend(adj.get(k, []))
            result[(p.ref, p.number)] = {"pin": p, "names": names, "links": links}
    return result
