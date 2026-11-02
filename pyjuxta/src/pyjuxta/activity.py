"""Activity from JXV ``motion``: binned series, diel heatmap, and nonparametric rhythm metrics.

``motion`` is the number of 100 ms accelerometer samples, since the previous vitals row, in which any axis
moved by more than 0.5 m/s^2. At the default 60 s vitals interval it runs 0-600. It is an activity proxy,
not acceleration, and a zero is a quiet minute, not sleep.
"""

from __future__ import annotations

import numpy as np
import pandas as pd

from . import figures as F
from ._time import DAY_S, local_clock, local_strings, utc_index
from .result import Result

MOTION_NOTE = ("motion counts 100 ms samples (per vitals row) in which the tag moved. "
               "It is an activity proxy, not acceleration, and a quiet stretch is not proof of sleep.")


def activity_bins(dev, *, bin_s: int, active_threshold: float, exclude_after_gap: bool = True,
                  min_bin_coverage: float = 0.5, max_gap_s: float | None = None):
    """Bin a device's motion onto a regular UTC grid. Returns ``(DataFrame, n_rows_excluded)``.

    Bins with too few vitals rows are NaN (missing), never zero.
    """
    v = dev.vitals_flagged(max_gap_s)
    n_excl = int(v.after_gap.sum()) if exclude_after_gap else 0
    if exclude_after_gap:
        v = v[~v.after_gap]
    v = v[v.motion.notna()]
    cols = ["bin_unix", "motion_mean", "active_frac", "n_rows", "coverage"]
    if v.empty:
        return pd.DataFrame({c: pd.Series(dtype="float64") for c in cols}), n_excl
    b = (v.unix // bin_s) * bin_s
    tmp = pd.DataFrame({"bin_unix": b.to_numpy(), "motion": v.motion.to_numpy(),
                        "act": (v.motion >= active_threshold).astype(float).to_numpy()})
    g = tmp.groupby("bin_unix").agg(motion_mean=("motion", "mean"), active_frac=("act", "mean"),
                                    n_rows=("motion", "size"))
    grid = np.arange(g.index.min(), g.index.max() + bin_s, bin_s, dtype="int64")
    g = g.reindex(grid)
    g.index.name = "bin_unix"
    g["n_rows"] = g["n_rows"].fillna(0).astype(int)
    g["coverage"] = (g.n_rows / (bin_s / max(dev.vitals_interval_s, 1))).clip(upper=1.0)
    low = g.coverage < min_bin_coverage
    g.loc[low, ["motion_mean", "active_frac"]] = np.nan
    return g.reset_index(), n_excl


def _smooth(s: pd.Series, bins: int) -> pd.Series:
    """Centered moving average that never fills a bin that had no data."""
    if bins <= 1:
        return s
    return s.rolling(int(bins), center=True, min_periods=1).mean().where(s.notna())


def activity(dep, devices=None, *, bin_s: int = 300, active_threshold: float = 1, smooth_bins: int = 1,
             signal: str = "active_frac", exclude_after_gap: bool = True, min_bin_coverage: float = 0.5,
             max_gap_s: float | None = None) -> Result:
    """Binned activity for one or more devices.

    Parameters
    ----------
    bin_s
        Bin width in seconds (UTC-aligned).
    active_threshold
        A minute counts as *active* when ``motion >= active_threshold``. This is a documented choice, not
        a validated cut-off; try several.
    smooth_bins
        Centered moving average over this many bins (1 = none). Smoothing never fills missing bins.
    signal
        ``"active_frac"`` (share of vitals rows that were active) or ``"motion_mean"`` (mean motion count).
    exclude_after_gap
        Drop the first vitals row after each gap. The firmware clears its motion counter only when it writes
        a vitals row, so that row also counts motion since boot or the last sync.
    min_bin_coverage
        A bin needs at least this fraction of its expected vitals rows, otherwise it is missing.
    """
    if signal not in ("active_frac", "motion_mean"):
        raise ValueError("signal must be 'active_frac' or 'motion_mean'")
    ids = dep.resolve_ids(devices if devices is not None else dep.mobiles)
    parts, per = [], []
    for dev_id in ids:
        d = dep.devices[dev_id]
        g, n_excl = activity_bins(d, bin_s=bin_s, active_threshold=active_threshold,
                                  exclude_after_gap=exclude_after_gap, min_bin_coverage=min_bin_coverage,
                                  max_gap_s=max_gap_s)
        v = d.vitals
        rows_ok = v.motion.dropna()
        g = g.copy()
        g["motion_smooth"] = _smooth(g.motion_mean, smooth_bins) if len(g) else g.get("motion_mean")
        g["active_frac_smooth"] = _smooth(g.active_frac, smooth_bins) if len(g) else g.get("active_frac")
        g["active"] = np.where(g.active_frac.isna(), np.nan, (g.active_frac >= 0.5).astype(float)) if len(g) else []
        g.insert(0, "device", dev_id)
        g.insert(1, "label", dep.label(dev_id))
        parts.append(g)
        obs = g.active_frac.notna() if len(g) else pd.Series(dtype=bool)
        per.append({
            "device": dev_id, "label": dep.label(dev_id), "role": dep.role(dev_id),
            "observed_h": round(float(obs.sum()) * bin_s / 3600, 2),
            "bins_observed": int(obs.sum()), "bins_total": int(len(g)),
            "rows_used": int(g.n_rows.sum()) if len(g) else 0,
            "rows_excluded_after_gap": n_excl,
            "active_share": round(float((rows_ok >= active_threshold).mean()), 3) if len(rows_ok) else np.nan,
            "zero_share": round(float((rows_ok == 0).mean()), 3) if len(rows_ok) else np.nan,
            "motion_mean": round(float(rows_ok.mean()), 1) if len(rows_ok) else np.nan,
            "motion_median_when_active": (round(float(rows_ok[rows_ok >= active_threshold].median()), 1)
                                          if (rows_ok >= active_threshold).any() else np.nan),
        })
    data = pd.concat(parts, ignore_index=True) if parts else pd.DataFrame()
    by_dev = pd.DataFrame(per)
    tot = int(by_dev.bins_total.sum()) if len(by_dev) else 0
    quality = {
        "coverage": round(float(by_dev.bins_observed.sum()) / tot, 3) if tot else 0.0,
        "n_observations": int(by_dev.rows_used.sum()) if len(by_dev) else 0,
        "rows_excluded_after_gap": int(by_dev.rows_excluded_after_gap.sum()) if len(by_dev) else 0,
    }
    params = {"bin_s": bin_s, "active_threshold": active_threshold, "smooth_bins": smooth_bins,
              "signal": signal, "exclude_after_gap": exclude_after_gap, "min_bin_coverage": min_bin_coverage}
    notes = [MOTION_NOTE]
    if quality["rows_excluded_after_gap"]:
        notes.append(f"{quality['rows_excluded_after_gap']} vitals row(s) after a gap were left out: "
                     "they also count motion since the tag booted.")
    ctx = {"tz": dep.tz, "colors": {i: dep.color(i) for i in ids}, "labels": {i: dep.label(i) for i in ids},
           "order": ids}
    summary = {"n_devices": len(ids), "bin_s": bin_s, "signal": signal}
    return Result("activity", data, params, quality, summary, {"by_device": by_dev}, notes, _fig_activity, ctx)


def _midnight_shapes(unix_min: int, unix_max: int, tz: str, theme: str) -> list[dict]:
    t = F.theme_of(theme)
    days = pd.date_range(local_strings([unix_min], tz)[0][:10], local_strings([unix_max], tz)[0][:10], freq="D")
    return [{"type": "line", "xref": "x", "yref": "paper", "x0": f"{d:%Y-%m-%d} 00:00:00",
             "x1": f"{d:%Y-%m-%d} 00:00:00", "y0": 0, "y1": 1, "line": {"color": t["grid"], "width": 1.5}}
            for d in days]


def _fig_activity(r: Result, theme: str) -> dict:
    ctx, p = r._context, r.parameters
    if not len(r.data) or not r.data.active_frac.notna().any():
        return F.empty_figure(theme, "No motion data in range")
    smooth = p["smooth_bins"] > 1
    col = ("active_frac_smooth" if smooth else "active_frac") if p["signal"] == "active_frac" else \
          ("motion_smooth" if smooth else "motion_mean")
    other = "motion_mean" if p["signal"] == "active_frac" else "active_frac"
    traces = []
    for dev in ctx["order"]:
        s = r.data[(r.data.device == dev) & r.data[col].notna()]
        if not len(s):
            continue
        x, (y, o) = F.break_lines(s.bin_unix.to_numpy(), [s[col].to_numpy(), s[other].to_numpy()],
                                  ctx["tz"], p["bin_s"] * 1.5)
        traces.append({"type": "scatter", "mode": "lines", "x": x, "y": y, "customdata": o,
                       "name": ctx["labels"][dev], "connectgaps": False,
                       "line": {"width": 1.6, "color": ctx["colors"][dev], "shape": "hv"},
                       "hovertemplate": ("%{y:.0%}" if p["signal"] == "active_frac" else "%{y:.0f}") +
                                        "<br>%{x}<extra>" + ctx["labels"][dev] + "</extra>"})
    y_title = ("Share of minutes with movement" if p["signal"] == "active_frac" else "Mean motion count per minute")
    ymax = 1.02 if p["signal"] == "active_frac" else None
    t0, t1 = int(r.data.bin_unix.min()), int(r.data.bin_unix.max())
    return {"data": traces, "layout": F.layout(
        theme, height=330, shapes=_midnight_shapes(t0, t1, ctx["tz"], theme),
        xaxis=F.date_axis(theme, title={"text": f"Local time ({ctx['tz']}); lines mark local midnight"}),
        yaxis=F.axis(theme, title={"text": y_title}, rangemode="tozero",
                     **({"range": [0, ymax], "tickformat": ".0%"} if ymax else {})))}


# ── diel ───────────────────────────────────────────────────────────────────────

def diel(dep, device, *, bin_s: int = 1800, active_threshold: float = 1, signal: str = "active_frac",
         exclude_after_gap: bool = True, min_bin_coverage: float = 0.5) -> Result:
    """Activity on the local 24-hour clock: one row per local date, one column per clock bin.

    A diel profile describes activity across the day. It does not show that a circadian clock drives it.
    """
    if DAY_S % bin_s:
        raise ValueError("bin_s must divide 86400 (for example 900, 1800, 3600)")
    dev = dep.device(device)
    g, n_excl = activity_bins(dev, bin_s=bin_s, active_threshold=active_threshold,
                              exclude_after_gap=exclude_after_gap, min_bin_coverage=min_bin_coverage)
    col = signal
    if col not in ("active_frac", "motion_mean"):
        raise ValueError("signal must be 'active_frac' or 'motion_mean'")
    obs = g[g[col].notna()] if len(g) else g
    nb = DAY_S // bin_s
    if len(obs):
        day, sod = local_clock(obs.bin_unix.to_numpy(), dep.tz)
        tod = np.floor(sod / bin_s).astype(int).clip(0, nb - 1)
        long = pd.DataFrame({"local_date": day, "tod_bin": tod, "value": obs[col].to_numpy(),
                             "n_rows": obs.n_rows.to_numpy()})
        long = long.groupby(["local_date", "tod_bin"], as_index=False).agg(value=("value", "mean"),
                                                                           n_rows=("n_rows", "sum"))
        long["clock_h"] = long.tod_bin * bin_s / 3600
    else:
        long = pd.DataFrame(columns=["local_date", "tod_bin", "value", "n_rows", "clock_h"])
    days = sorted(long.local_date.unique()) if len(long) else []
    per_day = long.groupby("local_date").tod_bin.nunique() if len(long) else pd.Series(dtype=int)
    complete = [d for d in days if per_day[d] >= 0.9 * nb]
    prof = (long.groupby("tod_bin").agg(mean=("value", "mean"), n_days=("local_date", "nunique"))
            .reindex(range(nb)).reset_index() if len(long) else pd.DataFrame({"tod_bin": range(nb), "mean": np.nan, "n_days": 0}))
    prof["clock_h"] = prof.tod_bin * bin_s / 3600
    peak = prof.loc[prof["mean"].idxmax()] if prof["mean"].notna().any() else None
    trough = prof.loc[prof["mean"].idxmin()] if prof["mean"].notna().any() else None
    summary = {"local_days_with_data": len(days), "complete_days": len(complete),
               "peak_clock_h": float(peak.clock_h) if peak is not None else None,
               "peak_value": float(peak["mean"]) if peak is not None else None,
               "quietest_clock_h": float(trough.clock_h) if trough is not None else None,
               "quietest_value": float(trough["mean"]) if trough is not None else None}
    quality = {"coverage": round(len(long) / (len(days) * nb), 3) if days else 0.0,
               "n_observations": int(long.n_rows.sum()) if len(long) else 0}
    notes = [MOTION_NOTE,
             "Each row is a local calendar day. Partial days (the recording started or stopped mid-day) are "
             "kept but counted separately from complete days.",
             "A profile averaged over only a few days says little about a stable rhythm."]
    if len(days) and len(days) < 3:
        notes.append(f"Only {len(days)} local day(s) have data; the average profile is a sketch.")
    ctx = {"tz": dep.tz, "label": dep.label(dev.id), "color": dep.color(dev.id), "days": days, "nb": nb}
    params = {"device": dev.id, "bin_s": bin_s, "active_threshold": active_threshold, "signal": signal,
              "exclude_after_gap": exclude_after_gap}
    return Result("diel", long, params, quality, summary, {"profile": prof}, notes, _fig_diel, ctx)


def _fig_diel(r: Result, theme: str) -> dict:
    ctx, p, t = r._context, r.parameters, F.theme_of(theme)
    days, nb = ctx["days"], ctx["nb"]
    if not days:
        return F.empty_figure(theme, "No motion data in range")
    bin_h = p["bin_s"] / 3600
    z = np.full((len(days), nb), np.nan)
    di = {d: i for i, d in enumerate(days)}
    for row in r.data.itertuples(index=False):
        z[di[row.local_date], int(row.tod_bin)] = row.value
    xh = [i * bin_h + bin_h / 2 for i in range(nb)]
    prof = r.tables["profile"]
    pct = p["signal"] == "active_frac"
    heat = {"type": "heatmap", "x": xh, "y": days, "z": z.tolist(), "xaxis": "x", "yaxis": "y",
            "colorscale": t["scale"], "zmin": 0, "zmax": 1 if pct else None, "hoverongaps": False,
            "colorbar": {"title": {"text": "active" if pct else "motion"}, "tickformat": ".0%" if pct else None,
                         "len": 0.62, "y": 0.78, "thickness": 12, "outlinewidth": 0},
            "hovertemplate": "%{y} %{x:.1f} h<br>%{z:.0%}<extra></extra>" if pct else
                             "%{y} %{x:.1f} h<br>%{z:.0f}<extra></extra>"}
    if not pct:
        heat.pop("zmax")
    line = {"type": "scatter", "mode": "lines", "x": xh, "y": prof["mean"].tolist(), "xaxis": "x2", "yaxis": "y2",
            "line": {"width": 2, "color": ctx["color"], "shape": "hvh"}, "connectgaps": False,
            "name": "Average", "showlegend": False,
            "hovertemplate": "%{x:.1f} h<br>" + ("%{y:.0%}" if pct else "%{y:.0f}") + "<extra>average</extra>"}
    xa = dict(range=[0, 24], tickvals=list(range(0, 25, 3)), ticktext=[f"{h:02d}:00" for h in range(0, 25, 3)])
    return {"data": [heat, line], "layout": F.layout(
        theme, height=150 + 34 * len(days) + 150, margin={"l": 90, "r": 24, "t": 16, "b": 50},
        xaxis=F.axis(theme, domain=[0, 1], anchor="y", showticklabels=False, **{k: v for k, v in xa.items() if k == "range"}),
        yaxis=F.axis(theme, domain=[0.36, 1], type="category", autorange="reversed", title={"text": "Local date"}),
        xaxis2=F.axis(theme, domain=[0, 1], anchor="y2", title={"text": f"Local clock ({ctx['tz']})"}, **xa),
        yaxis2=F.axis(theme, domain=[0, 0.26], anchor="x2", rangemode="tozero",
                      title={"text": "Average"}, **({"tickformat": ".0%"} if pct else {})))}


# ── rhythms ────────────────────────────────────────────────────────────────────

def _circular_window(profile: np.ndarray, width: int, kind: str) -> tuple[float, int]:
    n = len(profile)
    ext = np.r_[profile, profile[: width - 1]]
    means = np.array([ext[s: s + width].mean() for s in range(n)])
    s = int(np.argmin(means) if kind == "min" else np.argmax(means))
    return float(means[s]), s


def rhythms(dep, device, *, min_days: int = 3, min_day_coverage: float = 0.9, active_threshold: float = 1,
            exclude_after_gap: bool = True, min_bin_coverage: float = 0.5) -> Result:
    """Nonparametric rest-activity metrics on hourly mean motion, behind a quality gate.

    * **IS** interdaily stability: how closely each day repeats the average 24 h pattern (0-1).
    * **IV** intradaily variability: how fragmented activity is hour to hour (about 0-2).
    * **L5 / M10** mean activity in the least-active five and most-active ten consecutive hours of the
      average day; **RA** = (M10 - L5) / (M10 + L5).

    The metrics are returned only when at least ``min_days`` local days are nearly complete
    (``min_day_coverage`` of their hourly bins observed). Otherwise the result says why, and the metrics are
    NaN. ``min_days = 3`` is an engineering safeguard, not a published threshold; lowering it is allowed and
    is noted on the result.
    """
    dev = dep.device(device)
    g, _ = activity_bins(dev, bin_s=3600, active_threshold=active_threshold, exclude_after_gap=exclude_after_gap,
                         min_bin_coverage=min_bin_coverage)
    obs = g[g.motion_mean.notna()] if len(g) else g
    if len(obs):
        day, sod = local_clock(obs.bin_unix.to_numpy(), dep.tz)
        hours = pd.DataFrame({"bin_unix": obs.bin_unix.to_numpy(), "local_date": day,
                              "clock_h": np.floor(sod / 3600).astype(int).clip(0, 23),
                              "x": obs.motion_mean.to_numpy()})
        per_day = hours.groupby("local_date").agg(observed_hours=("x", "size")).reset_index()
    else:
        hours = pd.DataFrame(columns=["bin_unix", "local_date", "clock_h", "x"])
        per_day = pd.DataFrame({"local_date": [], "observed_hours": []})
    per_day["complete"] = per_day.observed_hours >= 24 * min_day_coverage
    complete = set(per_day.local_date[per_day.complete])
    n_complete = len(complete)
    available = n_complete >= max(1, min_days)
    metrics = {"IS": np.nan, "IV": np.nan, "L5": np.nan, "L5_onset_h": np.nan, "M10": np.nan,
               "M10_onset_h": np.nan, "RA": np.nan}
    profile = pd.DataFrame({"clock_h": range(24), "mean": np.nan})
    if available:
        use = hours[hours.local_date.isin(complete)].sort_values("bin_unix")
        x = use.x.to_numpy()
        m = x.mean()
        ss = ((x - m) ** 2).sum()
        grp = use.groupby("clock_h").x.agg(["mean", "size"]).reindex(range(24))
        profile = grp["mean"].rename("mean").reset_index().rename(columns={"index": "clock_h"})
        if ss > 0 and grp["mean"].notna().all():
            if n_complete >= 2:  # with one day every hour is its own average, so IS is 1 by construction
                metrics["IS"] = float((grp["size"] * (grp["mean"] - m) ** 2).sum() / ss)
            t = use.bin_unix.to_numpy()
            pair = np.diff(t) == 3600
            d2 = (np.diff(x)[pair] ** 2).sum()
            metrics["IV"] = float((d2 / max(pair.sum(), 1)) / (ss / len(x)))
            p = grp["mean"].to_numpy()
            l5, ls = _circular_window(p, 5, "min")
            m10, ms = _circular_window(p, 10, "max")
            metrics.update({"L5": l5, "L5_onset_h": ls, "M10": m10, "M10_onset_h": ms,
                            "RA": (m10 - l5) / (m10 + l5) if (m10 + l5) > 0 else np.nan})
    if available:
        status = "computed"
        reason = f"{n_complete} nearly complete local day(s) (gate: {min_days})."
    else:
        status = "insufficient data"
        reason = (f"{n_complete} nearly complete local day(s) found; {min_days} required. "
                  "A rest-activity rhythm needs repeated whole days.")
    notes = [MOTION_NOTE,
             "Computed on hourly mean motion with local clock hours (a nonparametric, nparACT-style implementation).",
             reason]
    if min_days < 3:
        notes.append("min_days is below the suggested 3: treat any value as a description of these days only.")
    if available and n_complete < 2:
        notes.append("IS needs at least 2 complete days to compare days with each other; it is left empty.")
    summary = {"status": status, "available": bool(available), "complete_days": n_complete,
               "min_days": min_days, **{k: (None if pd.isna(v) else float(v)) for k, v in metrics.items()}}
    quality = {"n_observations": int(len(hours)), "complete_days": n_complete}
    ctx = {"tz": dep.tz, "label": dep.label(dev.id), "color": dep.color(dev.id), "profile": profile,
           "min_days": min_days}
    params = {"device": dev.id, "min_days": min_days, "min_day_coverage": min_day_coverage,
              "active_threshold": active_threshold}
    return Result("rhythms", per_day, params, quality, summary, {"profile": profile}, notes, _fig_rhythms, ctx)


def _fig_rhythms(r: Result, theme: str) -> dict:
    ctx, t = r._context, F.theme_of(theme)
    if not r.summary["available"]:
        d = r.data
        if not len(d):
            return F.empty_figure(theme, "No motion data in range")
        bars = {"type": "bar", "x": d.local_date.tolist(), "y": d.observed_hours.tolist(),
                "marker": {"color": [ctx["color"] if c else t["fade"] for c in d.complete]},
                "hovertemplate": "%{x}<br>%{y} of 24 hourly bins<extra></extra>"}
        return {"data": [bars], "layout": F.layout(
            theme, height=300, annotations=[{
                "text": f"Not computed: {r.summary['complete_days']} complete day(s), {r.summary['min_days']} required",
                "xref": "paper", "yref": "paper", "x": 0.5, "y": 1.1, "showarrow": False,
                "font": {"size": 13, "color": t["muted"]}}],
            xaxis=F.axis(theme, type="category", title={"text": "Local date"}),
            yaxis=F.axis(theme, title={"text": "Hours with data"}, range=[0, 25]))}
    prof = ctx["profile"]
    s = r.summary
    shapes = []

    def _fmt(key: str, label: str, digits: int) -> str:
        v = s.get(key)
        return f"{label} n/a" if v is None else f"{label} {v:.{digits}f}"

    for key, width, color in (("L5_onset_h", 5, t["fade"]), ("M10_onset_h", 10, "rgba(139,92,246,0.18)")):
        if s.get(key) is None:
            continue
        start = int(s[key])
        segs = [(start, min(start + width, 24))] + ([(0, start + width - 24)] if start + width > 24 else [])
        for a, b in segs:
            shapes.append({"type": "rect", "xref": "x", "yref": "paper", "x0": a, "x1": b, "y0": 0, "y1": 1,
                           "fillcolor": color, "line": {"width": 0}, "layer": "below"})
    bars = {"type": "bar", "x": [h + 0.5 for h in prof.clock_h], "y": prof["mean"].tolist(),
            "marker": {"color": ctx["color"]}, "hovertemplate": "%{x:.0f} h<br>%{y:.0f}<extra></extra>"}
    return {"data": [bars], "layout": F.layout(
        theme, height=300, shapes=shapes,
        xaxis=F.axis(theme, range=[0, 24], tickvals=list(range(0, 25, 3)), title={"text": f"Local clock ({ctx['tz']})"}),
        yaxis=F.axis(theme, title={"text": "Mean motion per minute"}, rangemode="tozero"),
        annotations=[{"text": "  ·  ".join(
            [_fmt("IS", "IS", 2), _fmt("IV", "IV", 2), _fmt("L5", "L5", 0), _fmt("M10", "M10", 0), _fmt("RA", "RA", 2)]),
                      "xref": "paper", "yref": "paper", "x": 0, "y": 1.1, "showarrow": False, "xanchor": "left",
                      "font": {"size": 12, "color": t["muted"]}}])}
