"""Device health: when each tag recorded, and what its battery did."""

from __future__ import annotations

import numpy as np
import pandas as pd

from . import figures as F
from ._time import local_strings, utc_index
from .result import Result

BATT_CUTOFF_V = 2.5  # firmware UVLO (BATT_UVLO_MV 2500)


def _ctx(dep) -> dict:
    return {"tz": dep.tz, "colors": {i: dep.color(i) for i in dep.devices},
            "labels": {i: dep.label(i) for i in dep.devices},
            "order": dep.mobiles + dep.bases}


def availability(dep, *, max_gap_s: float | None = None) -> Result:
    """When each device was recording.

    A *session* is an unbroken run of vitals rows. Anything between sessions (shelf, power off, sync) is
    missing time and is never treated as inactivity or a zero.

    Parameters
    ----------
    max_gap_s
        Longest silence that still counts as one session. ``None`` uses three vitals intervals
        (at least 3 minutes) per device.
    """
    rows = []
    for dev_id, d in dep.devices.items():
        s = d.sessions(max_gap_s)
        for r in s.itertuples(index=False):
            rows.append({"device": dev_id, "label": dep.label(dev_id), "role": dep.role(dev_id),
                         "start": int(r.start), "end": int(r.end), "duration_h": round(r.duration_s / 3600, 3),
                         "n_samples": int(r.n_samples)})
    data = pd.DataFrame(rows, columns=["device", "label", "role", "start", "end", "duration_h", "n_samples"])
    if len(data):
        data.insert(4, "start_utc", utc_index(data.start).strftime("%Y-%m-%d %H:%M:%S"))
        data.insert(6, "end_utc", utc_index(data.end).strftime("%Y-%m-%d %H:%M:%S"))
    by_dev = dep.summary()
    syncs = []
    for dev_id, d in dep.devices.items():
        ev = d.events
        if len(ev):
            for r in ev[ev.event == "time_set"].itertuples(index=False):
                syncs.append({"device": dev_id, "unix": int(r.unix)})
    sync = pd.DataFrame(syncs, columns=["device", "unix"])
    span = dep.span
    summary = {
        "devices": len(dep.devices), "mobiles": len(dep.mobiles), "bases": len(dep.bases),
        "first_utc": utc_index([span[0]])[0].isoformat() if span else None,
        "last_utc": utc_index([span[1]])[0].isoformat() if span else None,
        "span_h": round((span[1] - span[0]) / 3600, 1) if span else 0,
        "sessions": int(len(data)),
        "phone_syncs": int(len(sync)),
        "least_covered": (by_dev.sort_values("coverage").iloc[0].device_id if len(by_dev) else None),
        "least_coverage": (float(by_dev.coverage.min()) if len(by_dev) else None),
    }
    notes = ["Gaps are missing time (shelf, power off, or sync), not rest.",
             "Diamonds mark a phone sync (time_set): the clock was set and the settings were written."]
    notes += dep.check()
    ctx = _ctx(dep)
    ctx["sync"] = sync
    return Result("availability", data, {"max_gap_s": max_gap_s},
                  {"n_devices": len(dep.devices)}, summary, {"by_device": by_dev}, notes, _fig_availability, ctx)


def _fig_availability(r: Result, theme: str) -> dict:
    ctx, t = r._context, F.theme_of(theme)
    if not len(r.data):
        return F.empty_figure(theme, "No vitals rows to draw")
    order = [i for i in ctx["order"] if i in set(r.data.device)]
    labels = [ctx["labels"][i] for i in order]
    traces = []
    for dev in order:
        s = r.data[r.data.device == dev]
        xs, ys = [], []
        for row in s.itertuples(index=False):
            x0, x1 = local_strings([row.start, row.end], ctx["tz"])
            xs += [x0, x1, None]
            ys += [ctx["labels"][dev]] * 2 + [None]
        traces.append({"type": "scatter", "mode": "lines", "x": xs, "y": ys, "name": ctx["labels"][dev],
                       "line": {"width": 14, "color": ctx["colors"][dev]}, "connectgaps": False,
                       "hovertemplate": "%{x}<extra>" + ctx["labels"][dev] + "</extra>"})
    sync = ctx["sync"]
    if len(sync):
        sync = sync[sync.device.isin(order)]
        traces.append({"type": "scatter", "mode": "markers", "x": local_strings(sync.unix, ctx["tz"]),
                       "y": [ctx["labels"][d] for d in sync.device], "name": "Phone sync",
                       "marker": {"symbol": "diamond", "size": 9, "color": t["line"],
                                  "line": {"width": 1, "color": "#0B0F3B"}},
                       "hovertemplate": "synced %{x}<extra></extra>"})
    return {"data": traces, "layout": F.layout(
        theme, margin={"l": 90, "r": 24, "t": 36, "b": 56}, height=60 * len(order) + 120,
        xaxis=F.date_axis(theme, title={"text": f"Local time ({ctx['tz']})"}),
        yaxis=F.axis(theme, categoryorder="array", categoryarray=labels[::-1], title=None, showgrid=False))}


def battery(dep, *, bin_s: int = 900, max_gap_s: float | None = None) -> Result:
    """Pack voltage over time for every device, in volts (two decimals, as logged).

    This is the voltage the coin cell showed under the tag's own load, not a state of charge. It sags with
    cold, radio use, and age. The firmware locks out below 2.5 V.
    """
    parts, per = [], []
    for dev_id, d in dep.devices.items():
        v = d.vitals
        if not len(v):
            continue
        b = (v.unix // bin_s) * bin_s
        g = v.groupby(b).batt_v.mean().rename("batt_v").reset_index().rename(columns={"unix": "bin_unix"})
        g.insert(0, "device", dev_id)
        parts.append(g)
        per.append({"device": dev_id, "label": dep.label(dev_id), "start_v": float(v.batt_v.iloc[0]),
                    "end_v": float(v.batt_v.iloc[-1]), "min_v": float(v.batt_v.min()),
                    "max_v": float(v.batt_v.max()),
                    "headroom_to_cutoff_v": round(float(v.batt_v.min()) - BATT_CUTOFF_V, 2)})
    data = pd.concat(parts, ignore_index=True) if parts else pd.DataFrame(columns=["device", "bin_unix", "batt_v"])
    by_dev = pd.DataFrame(per)
    summary = {}
    if len(by_dev):
        low = by_dev.sort_values("min_v").iloc[0]
        summary = {"lowest_device": low.device, "lowest_v": float(low.min_v),
                   "headroom_to_cutoff_v": float(low.headroom_to_cutoff_v)}
    notes = ["Voltage under load is not state of charge. A cold or busy coin cell reads lower than a rested one.",
             f"The firmware refuses to run below {BATT_CUTOFF_V:.1f} V."]
    ctx = _ctx(dep)
    ctx["gap"] = max(bin_s * 3, 1800) if max_gap_s is None else max_gap_s
    return Result("battery", data, {"bin_s": bin_s}, {"n_devices": len(by_dev)}, summary,
                  {"by_device": by_dev}, notes, _fig_battery, ctx)


def _fig_battery(r: Result, theme: str) -> dict:
    ctx, t = r._context, F.theme_of(theme)
    if not len(r.data):
        return F.empty_figure(theme, "No vitals rows to draw")
    traces = []
    for dev in [i for i in ctx["order"] if i in set(r.data.device)]:
        s = r.data[r.data.device == dev].sort_values("bin_unix")
        x, y = F.break_lines(s.bin_unix.to_numpy(), s.batt_v.to_numpy(), ctx["tz"], ctx["gap"])
        traces.append({"type": "scatter", "mode": "lines", "x": x, "y": y, "name": ctx["labels"][dev],
                       "line": {"width": 2, "color": ctx["colors"][dev]}, "connectgaps": False,
                       "hovertemplate": "%{y:.2f} V<br>%{x}<extra>" + ctx["labels"][dev] + "</extra>"})
    lo = float(r.data.batt_v.min())
    shapes, ann = [], []
    ymin = min(lo - 0.05, BATT_CUTOFF_V - 0.02)
    shapes.append({"type": "line", "xref": "paper", "x0": 0, "x1": 1, "yref": "y", "y0": BATT_CUTOFF_V,
                   "y1": BATT_CUTOFF_V, "line": {"color": t["muted"], "width": 1, "dash": "dot"}})
    ann.append({"xref": "paper", "x": 1, "yref": "y", "y": BATT_CUTOFF_V, "text": "firmware cutoff 2.5 V",
                "showarrow": False, "xanchor": "right", "yanchor": "bottom", "font": {"size": 11, "color": t["muted"]}})
    return {"data": traces, "layout": F.layout(
        theme, height=340, shapes=shapes, annotations=ann,
        xaxis=F.date_axis(theme, title={"text": f"Local time ({ctx['tz']})"}),
        yaxis=F.axis(theme, title={"text": "Battery (V)"}, range=[ymin, float(r.data.batt_v.max()) + 0.05]))}
