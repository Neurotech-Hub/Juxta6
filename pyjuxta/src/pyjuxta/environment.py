"""Temperature, humidity, and light at the tag (JXV ``temp_c``, ``humidity``, ``lux``)."""

from __future__ import annotations

import numpy as np
import pandas as pd

from . import figures as F
from .result import Result

VARIABLES = {
    "temp_c": ("Temperature", "°C", ".1f"),
    "humidity": ("Relative humidity", "%", ".0f"),
    "lux": ("Light", "lux", ".0f"),
}
LUX_TICKS = [0, 1, 10, 100, 1000, 10000]


def environment(dep, variable: str = "temp_c", devices=None, *, bin_s: int = 900, log_lux: bool = True) -> Result:
    """Binned temperature, humidity, or light for the chosen devices.

    The values describe the **sensor's surroundings**: a tag in a pocket reads body-warmed air, and one on a
    collar can be covered or turned away from the light. They are not core body temperature, and light needs
    orientation and covering in mind. An empty cell in the log (a failed sample) is missing, never zero.

    Parameters
    ----------
    variable
        ``"temp_c"``, ``"humidity"``, or ``"lux"``.
    bin_s
        Bin width in seconds; each bin is the mean of the samples in it.
    log_lux
        Draw light on a log scale (log10(1 + lux)); light spans several orders of magnitude.
    """
    if variable not in VARIABLES:
        raise ValueError(f"variable must be one of {', '.join(VARIABLES)}")
    ids = dep.resolve_ids(devices)
    parts, per = [], []
    for dev_id in ids:
        v = dep.devices[dev_id].vitals
        if not len(v):
            continue
        col = v[variable]
        ok = col.notna()
        b = ((v.unix[ok] // bin_s) * bin_s).to_numpy()
        g = pd.DataFrame({"bin_unix": b, "value": col[ok].to_numpy()}).groupby("bin_unix").agg(
            value=("value", "mean"), n=("value", "size")).reset_index()
        g.insert(0, "device", dev_id)
        g.insert(1, "label", dep.label(dev_id))
        parts.append(g)
        vals = col[ok]
        per.append({"device": dev_id, "label": dep.label(dev_id), "role": dep.role(dev_id),
                    "samples": int(ok.sum()), "missing": int((~ok).sum()),
                    "min": float(vals.min()) if len(vals) else np.nan,
                    "median": float(vals.median()) if len(vals) else np.nan,
                    "max": float(vals.max()) if len(vals) else np.nan})
    data = pd.concat(parts, ignore_index=True) if parts else pd.DataFrame(columns=["device", "label", "bin_unix", "value", "n"])
    by_dev = pd.DataFrame(per)
    name, unit, _ = VARIABLES[variable]
    summary = {"variable": variable, "unit": unit}
    if len(by_dev):
        hi = by_dev.loc[by_dev["max"].idxmax()]
        lo = by_dev.loc[by_dev["min"].idxmin()]
        spread = by_dev.assign(range=by_dev["max"] - by_dev["min"])
        wide = spread.loc[spread.range.idxmax()]
        summary.update({"highest_device": hi.label, "highest": float(hi["max"]), "lowest_device": lo.label,
                        "lowest": float(lo["min"]), "widest_range_device": wide.label, "widest_range": float(wide.range)})
    notes = ["These are readings at the sensor, not body temperature. A tag in a pocket or on a collar sits in its own microclimate.",
             "A missing sample is left out of the average and is never plotted as zero."]
    if variable == "lux":
        notes.append("Light depends on which way the sensor faces and whether it is covered. Compare a tag with itself "
                     "over time before comparing tags with each other.")
    params = {"variable": variable, "bin_s": bin_s, "log_lux": log_lux}
    ctx = {"tz": dep.tz, "colors": {i: dep.color(i) for i in ids}, "labels": {i: dep.label(i) for i in ids}, "order": ids}
    return Result("environment", data, params, {"n_observations": int(by_dev.samples.sum()) if len(by_dev) else 0},
                  summary, {"by_device": by_dev}, notes, _fig_environment, ctx)


def _fig_environment(r: Result, theme: str) -> dict:
    ctx, p = r._context, r.parameters
    if not len(r.data):
        return F.empty_figure(theme, "No samples in range")
    name, unit, fmt = VARIABLES[p["variable"]]
    use_log = p["variable"] == "lux" and p["log_lux"]
    traces = []
    for dev in ctx["order"]:
        s = r.data[r.data.device == dev].sort_values("bin_unix")
        if not len(s):
            continue
        raw = s.value.to_numpy()
        y = np.log1p(raw) if use_log else raw
        x, (yy, rr) = F.break_lines(s.bin_unix.to_numpy(), [y, raw], ctx["tz"], p["bin_s"] * 3)
        traces.append({"type": "scatter", "mode": "lines", "x": x, "y": yy, "customdata": rr,
                       "name": ctx["labels"][dev], "connectgaps": False,
                       "line": {"width": 1.6, "color": ctx["colors"][dev]},
                       "hovertemplate": "%{customdata:" + fmt + "} " + unit + "<br>%{x}<extra>" + ctx["labels"][dev] + "</extra>"})
    yax = F.axis(theme, title={"text": f"{name} ({unit})"})
    if use_log:
        top = float(np.log1p(r.data.value.max()))
        ticks = [v for v in LUX_TICKS if np.log1p(v) <= top + 0.5]
        yax.update({"tickvals": [float(np.log1p(v)) for v in ticks], "ticktext": [str(v) for v in ticks]})
    return {"data": traces, "layout": F.layout(
        theme, height=340, xaxis=F.date_axis(theme, title={"text": f"Local time ({ctx['tz']})"}), yaxis=yax)}
