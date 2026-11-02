"""Plotly figure specs (plain dicts) for every analysis.

Specs are ``{"data": [...], "layout": {...}}``. They render with Plotly.js in the walkthrough and with
``plotly.graph_objects.Figure(spec)`` in Python. Nothing here recomputes an analysis; it only draws a
:class:`~pyjuxta.Result`.
"""

from __future__ import annotations

import numpy as np
import pandas as pd

from ._time import local_naive, local_strings

THEMES = {
    "dark": {
        "text": "rgba(255,255,255,0.84)",
        "muted": "rgba(255,255,255,0.55)",
        "grid": "rgba(255,255,255,0.09)",
        "axis": "rgba(255,255,255,0.26)",
        "panel": "rgba(0,0,0,0)",
        "hover": "#171E55",
        "line": "rgba(255,255,255,0.9)",
        "scale": [[0, "#141A57"], [0.35, "#2563EB"], [0.72, "#8B5CF6"], [1, "#F472E6"]],
        "diverge": [[0, "#60A5FA"], [0.5, "#171E55"], [1, "#F472E6"]],
        "gray": [[0, "#1F2233"], [1, "#D1D5DB"]],
        "fade": "rgba(255,255,255,0.22)",
    },
    "light": {
        "text": "#0B0F3B",
        "muted": "#5B6075",
        "grid": "#E2E5EA",
        "axis": "#C4C8D0",
        "panel": "rgba(0,0,0,0)",
        "hover": "#FFFFFF",
        "line": "#0B0F3B",
        "scale": [[0, "#EEF2FF"], [0.35, "#60A5FA"], [0.72, "#8B5CF6"], [1, "#EC0DD9"]],
        "diverge": [[0, "#2563EB"], [0.5, "#F4F5F8"], [1, "#EC0DD9"]],
        "gray": [[0, "#F1F2F4"], [1, "#4B5563"]],
        "fade": "rgba(11,15,59,0.20)",
    },
}

FONT = "Plus Jakarta Sans, Inter, Helvetica Neue, Arial, sans-serif"


def theme_of(name: str) -> dict:
    return THEMES.get(name, THEMES["light"])


def scale_color(scale, frac: float) -> str:
    """Color at ``frac`` (0 to 1) on a Plotly colorscale of ``[[stop, "#RRGGBB"], ...]``.

    For marks Plotly cannot color from a scale itself, such as the individual lines of a network.
    """
    f = min(1.0, max(0.0, float(frac)))
    for (t0, c0), (t1, c1) in zip(scale[:-1], scale[1:]):
        if f <= t1:
            k = 0.0 if t1 == t0 else (f - t0) / (t1 - t0)
            a = [int(c0[i:i + 2], 16) for i in (1, 3, 5)]
            b = [int(c1[i:i + 2], 16) for i in (1, 3, 5)]
            return "rgb(" + ",".join(str(round(x + (y - x) * k)) for x, y in zip(a, b)) + ")"
    return scale[-1][1]


def layout(theme: str, **over) -> dict:
    t = theme_of(theme)
    base = {
        "margin": {"l": 70, "r": 24, "t": 24, "b": 56},
        "font": {"family": FONT, "size": 12, "color": t["text"]},
        "paper_bgcolor": t["panel"],
        "plot_bgcolor": t["panel"],
        "hoverlabel": {"font": {"family": FONT}, "bgcolor": t["hover"]},
        "legend": {"orientation": "h", "y": 1.08, "x": 0, "font": {"size": 12}},
        "hovermode": "closest",
    }
    base.update(over)
    return base


def axis(theme: str, **over) -> dict:
    t = theme_of(theme)
    base = {"gridcolor": t["grid"], "linecolor": t["axis"], "zerolinecolor": t["grid"],
            "tickcolor": t["axis"], "automargin": True}
    base.update(over)
    return base


def empty_figure(theme: str, message: str) -> dict:
    t = theme_of(theme)
    return {
        "data": [],
        "layout": layout(theme, xaxis={"visible": False}, yaxis={"visible": False},
                         annotations=[{"text": message, "showarrow": False, "font": {"size": 14, "color": t["muted"]},
                                       "xref": "paper", "yref": "paper", "x": 0.5, "y": 0.5}]),
    }


def break_lines(unix, ys, tz: str, gap_s: float):
    """Local-time x strings and y values with ``None`` where a line must not be drawn.

    A line is broken at any gap longer than ``gap_s`` (the tag was off or on the shelf) and wherever
    the local clock steps backward (the repeated hour when daylight saving ends). ``ys`` may be one
    array or a list of arrays that break together.
    """
    unix = np.asarray(unix, dtype="int64")
    multi = isinstance(ys, (list, tuple))
    cols = [np.asarray(y, dtype=float) for y in (ys if multi else [ys])]
    if len(unix) == 0:
        return [], ([[] for _ in cols] if multi else [])
    xs = local_strings(unix, tz)
    naive = local_naive(unix, tz).to_numpy()
    cut = np.where((np.diff(unix) > gap_s) | (np.diff(naive).astype("int64") < 0))[0] + 1
    out_x: list = []
    out_y: list[list] = [[] for _ in cols]
    prev = 0
    for c in list(cut) + [len(unix)]:
        if prev:
            out_x.append(None)
            for o in out_y:
                o.append(None)
        out_x.extend(xs[prev:c])
        for o, col in zip(out_y, cols):
            o.extend(col[prev:c].tolist())
        prev = c
    return out_x, (out_y if multi else out_y[0])


def date_axis(theme: str, **over) -> dict:
    return axis(theme, type="date", tickformat="%b %d\n%H:%M", **over)
