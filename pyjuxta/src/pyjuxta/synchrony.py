"""Activity synchrony between two devices, with explicit nulls.

Three different questions, kept apart:

* **Proximity**: were the two tags near each other? (:func:`pyjuxta.proximity.proximity`)
* **Synchrony**: do their activity series rise and fall together, minute by minute?
* **Similarity**: do they have alike daily profiles even when apart? (compare ``Deployment.diel`` results)

Two animals that are both active by day will correlate with no social link at all. So the headline
correlation is always shown next to (a) a time-shift null that keeps each series' own autocorrelation,
(b) a day-shifted null that keeps time of day, and (c) the same statistic during encounters versus matched
hours when apart.
"""

from __future__ import annotations

import numpy as np
import pandas as pd

from . import figures as F
from ._time import DAY_S, local_clock, local_strings
from .activity import MOTION_NOTE, activity_bins
from .proximity import proximity
from .result import Result

MIN_N = 10


def _rank(x: np.ndarray) -> np.ndarray:
    return pd.Series(x).rank().to_numpy()


def _corr(x: np.ndarray, y: np.ndarray, method: str, min_n: int = MIN_N) -> float:
    m = np.isfinite(x) & np.isfinite(y)
    if m.sum() < min_n:
        return float("nan")
    xx, yy = x[m], y[m]
    if method == "spearman":
        xx, yy = _rank(xx), _rank(yy)
    if xx.std() == 0 or yy.std() == 0:
        return float("nan")
    return float(np.corrcoef(xx, yy)[0, 1])


def _kappa(sa: np.ndarray, sb: np.ndarray) -> float:
    if len(sa) == 0:
        return float("nan")
    po = float(np.mean(sa == sb))
    pa, pb = float(np.mean(sa)), float(np.mean(sb))
    pe = pa * pb + (1 - pa) * (1 - pb)
    return (po - pe) / (1 - pe) if pe < 1 else float("nan")


def _shift(x: np.ndarray, k: int) -> np.ndarray:
    """Shift right by ``k`` bins with NaN fill (no wrap-around)."""
    out = np.full_like(x, np.nan)
    if k >= 0:
        out[k:] = x[: len(x) - k] if k < len(x) else out[k:]
    else:
        out[: len(x) + k] = x[-k:] if -k < len(x) else out[: len(x) + k]
    return out


def _lag_table(va: np.ndarray, vb: np.ndarray, max_lag: int, bin_s: int, method: str) -> pd.DataFrame:
    n = len(va)
    rows = []
    for k in range(-max_lag, max_lag + 1):
        if k >= 0:
            x, y = va[: n - k], vb[k:]
        else:
            x, y = va[-k:], vb[: n + k]
        m = np.isfinite(x) & np.isfinite(y)
        rows.append({"lag_bins": k, "lag_min": k * bin_s / 60, "r": _corr(x, y, method), "n": int(m.sum())})
    return pd.DataFrame(rows)


def synchrony(dep, a, b, *, bin_s: int = 300, active_threshold: float = 1, signal: str = "motion_mean",
              method: str = "spearman", transform: str = "none", max_lag_bins: int = 12, null: str = "circular",
              n_null: int = 500, min_shift_h: float = 2.0, seed: int = 0, active_bin_frac: float = 0.5,
              min_rssi: float = -75, merge_gap_s: float = 90, min_detections: int = 2,
              exclude_after_gap: bool = True, min_bin_coverage: float = 0.5) -> Result:
    """How closely two devices' activity lines up, and how that compares with chance.

    Parameters
    ----------
    bin_s
        Bin width. Only bins where **both** devices have enough data are used; nothing is interpolated.
    active_threshold, active_bin_frac
        A vitals row is active when ``motion >= active_threshold``. A bin is active when at least
        ``active_bin_frac`` of its rows are. Used for agreement and kappa.
    signal
        ``"motion_mean"`` or ``"active_frac"``: the series that is correlated.
    method, transform
        ``"spearman"`` (default, robust to zero-heavy series) or ``"pearson"``, optionally on
        ``"log1p"`` values.
    max_lag_bins
        Lags from ``-max_lag_bins`` to ``+max_lag_bins`` bins. A positive lag means ``b`` follows ``a``.
        The peak is descriptive; it does not show who leads.
    null
        ``"circular"``: shift ``b`` by random amounts (at least ``min_shift_h`` hours) around a loop. It keeps
        each series' autocorrelation and breaks their alignment. ``"day_shift"``: shift by whole days. It also
        keeps time of day, which is the harder test, but it needs several days to yield any shifts.
    n_null, seed
        Number of shifts for ``"circular"`` and the random seed (also used for the matched-hours draw).
    min_rssi, merge_gap_s, min_detections
        Encounter definition, as in :func:`pyjuxta.proximity.proximity`. It splits the bins into *together* and
        *apart*.

    The result separates the raw correlation, the excess over the null, and the encounter comparison.
    """
    if signal not in ("motion_mean", "active_frac"):
        raise ValueError("signal must be 'motion_mean' or 'active_frac'")
    if method not in ("spearman", "pearson"):
        raise ValueError("method must be 'spearman' or 'pearson'")
    if transform not in ("none", "log1p"):
        raise ValueError("transform must be 'none' or 'log1p'")
    if null not in ("circular", "day_shift"):
        raise ValueError("null must be 'circular' or 'day_shift'")
    A, B = dep.device(a).id, dep.device(b).id
    da, db = dep.devices[A], dep.devices[B]
    kw = dict(bin_s=bin_s, active_threshold=active_threshold, exclude_after_gap=exclude_after_gap,
              min_bin_coverage=min_bin_coverage)
    ga, _ = activity_bins(da, **kw)
    gb, _ = activity_bins(db, **kw)
    ctx = {"tz": dep.tz, "ids": [A, B], "labels": {A: dep.label(A), B: dep.label(B)},
           "colors": {A: dep.color(A), B: dep.color(B)}, "bin_s": bin_s}
    if ga.empty or gb.empty:
        return Result("synchrony", pd.DataFrame(), {}, {}, {"aligned_bins": 0}, {}, ["One of the devices has no motion data."],
                      lambda r, th: F.empty_figure(th, "One of the devices has no motion data"), ctx)
    lo = int(min(ga.bin_unix.min(), gb.bin_unix.min()))
    hi = int(max(ga.bin_unix.max(), gb.bin_unix.max()))
    grid = np.arange(lo, hi + bin_s, bin_s, dtype="int64")

    def on_grid(g: pd.DataFrame, col: str) -> np.ndarray:
        return g.set_index("bin_unix")[col].reindex(grid).to_numpy(dtype=float)

    ra, rb = on_grid(ga, signal), on_grid(gb, signal)
    fa, fb = on_grid(ga, "active_frac"), on_grid(gb, "active_frac")
    va = np.log1p(ra) if transform == "log1p" else ra
    vb = np.log1p(rb) if transform == "log1p" else rb
    aligned = np.isfinite(va) & np.isfinite(vb)
    n_al = int(aligned.sum())
    n_grid = len(grid)
    _, sod = local_clock(grid, dep.tz)
    hour = np.floor(sod / 3600).astype(int)

    sa = np.where(np.isfinite(fa), fa >= active_bin_frac, False)
    sb = np.where(np.isfinite(fb), fb >= active_bin_frac, False)

    # encounters -> together mask on the grid
    prox = proximity(dep, A, B, min_rssi=min_rssi, merge_gap_s=merge_gap_s, min_detections=min_detections)
    together = np.zeros(n_grid, dtype=bool)
    for e in prox.data.itertuples(index=False):
        i0 = max(int((e.start - lo) // bin_s), 0)
        i1 = min(int((e.end - lo) // bin_s), n_grid - 1)
        together[i0:i1 + 1] = True

    r_obs = _corr(va, vb, method)
    agree = float(np.mean(sa[aligned] == sb[aligned])) if n_al else float("nan")
    kappa = _kappa(sa[aligned], sb[aligned]) if n_al else float("nan")
    lags = _lag_table(va, vb, max_lag_bins, bin_s, method)
    lag_ok = lags[lags.r.notna()]
    peak = lag_ok.loc[lag_ok.r.idxmax()] if len(lag_ok) else None

    # residual (time-of-day removed): needs several days to be meaningful
    day_str, _ = local_clock(grid, dep.tz)
    n_days = len(set(day_str[aligned])) if n_al else 0
    r_resid = float("nan")
    if n_days >= 3 and n_al:
        def deseason(v: np.ndarray) -> np.ndarray:
            out = v.copy()
            for h in range(24):
                m = aligned & (hour == h)
                if m.any():
                    out[m] = v[m] - v[m].mean()
            out[~aligned] = np.nan
            return out
        r_resid = _corr(deseason(va), deseason(vb), method)

    rng = np.random.default_rng(seed)
    # shift null for the overall statistic
    day_bins = DAY_S // bin_s if DAY_S % bin_s == 0 else None
    if null == "circular":
        min_k = int(np.ceil(min_shift_h * 3600 / bin_s))
        choices = np.arange(min_k, n_grid - min_k) if n_grid - 2 * min_k > 0 else np.array([], dtype=int)
        ks = rng.choice(choices, size=min(n_null, len(choices)), replace=False) if len(choices) else np.array([], dtype=int)
        null_r = np.array([_corr(va, np.roll(vb, int(k)), method) for k in ks])
    else:
        if not day_bins:
            raise ValueError("bin_s must divide 86400 for the day_shift null")
        maxd = n_grid // day_bins
        ks = [k * day_bins * s for k in range(1, maxd + 1) for s in (1, -1)]
        null_r = np.array([_corr(va, _shift(vb, int(k)), method) for k in ks])
    null_r = null_r[np.isfinite(null_r)]
    p_shift = float((1 + (null_r >= r_obs).sum()) / (1 + len(null_r))) if len(null_r) and np.isfinite(r_obs) else float("nan")
    null_mean = float(null_r.mean()) if len(null_r) else float("nan")
    null_sd = float(null_r.std(ddof=1)) if len(null_r) > 1 else float("nan")

    # encounter-conditioned
    tog = aligned & together
    apart = aligned & ~together
    n_tog, n_apart = int(tog.sum()), int(apart.sum())
    r_tog = _corr(va[tog], vb[tog], method, min_n=MIN_N) if n_tog >= MIN_N else float("nan")
    r_apart = _corr(va[apart], vb[apart], method, min_n=MIN_N) if n_apart >= MIN_N else float("nan")
    matched = np.array([])
    p_matched = float("nan")
    by_hour: list[tuple[int, np.ndarray]] = []
    if n_tog >= 20 and n_apart >= 20:
        for h in range(24):
            k = int((tog & (hour == h)).sum())
            cand = np.where(apart & (hour == h))[0]
            if k and len(cand):
                by_hour.append((min(k, len(cand)), cand))
        draws = []
        for _ in range(n_null):
            idx = np.concatenate([rng.choice(c, size=k, replace=False) for k, c in by_hour])
            draws.append(_corr(va[idx], vb[idx], method))
        matched = np.array([d for d in draws if np.isfinite(d)])
        if len(matched) and np.isfinite(r_tog):
            p_matched = float((1 + (matched >= r_tog).sum()) / (1 + len(matched)))
    cond = pd.DataFrame([
        {"windows": "together (encounter)", "n_bins": n_tog, "r": r_tog,
         "agreement": float(np.mean(sa[tog] == sb[tog])) if n_tog else np.nan, "kappa": _kappa(sa[tog], sb[tog]) if n_tog else np.nan},
        {"windows": "apart", "n_bins": n_apart, "r": r_apart,
         "agreement": float(np.mean(sa[apart] == sb[apart])) if n_apart else np.nan, "kappa": _kappa(sa[apart], sb[apart]) if n_apart else np.nan},
        {"windows": "apart, matched to together hours (mean of draws)", "n_bins": int(sum(k for k, _ in by_hour)),
         "r": float(matched.mean()) if len(matched) else np.nan, "agreement": np.nan, "kappa": np.nan},
    ])

    data = pd.DataFrame({
        "bin_unix": grid, "time_local": local_strings(grid, dep.tz), "a": ra, "b": rb,
        "active_a": np.where(np.isfinite(fa), sa, np.nan), "active_b": np.where(np.isfinite(fb), sb, np.nan),
        "aligned": aligned, "together": together, "local_hour": hour,
    })
    summary = {
        "a": A, "b": B, "aligned_bins": n_al, "aligned_h": round(n_al * bin_s / 3600, 1),
        "r": r_obs, "r_at_zero_lag": float(lags.loc[lags.lag_bins == 0, "r"].iloc[0]),
        "agreement": agree, "kappa": kappa,
        "peak_lag_min": float(peak.lag_min) if peak is not None else None,
        "peak_lag_r": float(peak.r) if peak is not None else None,
        "null": null, "null_n": int(len(null_r)), "null_mean": null_mean, "null_sd": null_sd,
        "excess_r": (r_obs - null_mean) if np.isfinite(r_obs) and np.isfinite(null_mean) else None,
        "p_shift": p_shift,
        "r_time_of_day_removed": r_resid, "local_days_aligned": n_days,
        "n_together": n_tog, "n_apart": n_apart, "r_together": r_tog, "r_apart": r_apart,
        "matched_apart_mean": float(matched.mean()) if len(matched) else None, "p_together_vs_matched": p_matched,
        "episodes": summary_ep(prox),
    }
    quality = {"coverage": round(n_al / n_grid, 3) if n_grid else 0.0, "n_observations": n_al}
    notes = [MOTION_NOTE,
             "Correlation measures whether two series rise and fall together, not whether the animals interact. "
             "Two animals that are both busy by day correlate without any social link."]
    if n_al < 100:
        notes.append(f"Only {n_al} aligned bins: treat every number here as rough.")
    if null == "day_shift" and len(null_r) < 20:
        notes.append(f"The day-shift null has only {len(null_r)} shift(s) because the recording spans few days. "
                     "Its p-value cannot go below 1/(1+n); use the circular null or record more days.")
    if n_days < 3:
        notes.append("Time-of-day-removed correlation needs at least 3 local days with data; it is left empty.")
    if n_tog < 20:
        notes.append("Too few aligned bins during encounters for the together versus apart comparison.")
    notes.append("Lag: a positive lag means b's activity follows a's. A peak away from zero is descriptive, "
                 "not evidence that one animal leads.")
    params = {"a": A, "b": B, "bin_s": bin_s, "active_threshold": active_threshold, "signal": signal,
              "method": method, "transform": transform, "max_lag_bins": max_lag_bins, "null": null,
              "n_null": n_null, "min_shift_h": min_shift_h, "seed": seed, "active_bin_frac": active_bin_frac,
              "min_rssi": min_rssi, "merge_gap_s": merge_gap_s, "min_detections": min_detections}
    ctx.update({"lags": lags, "null_r": null_r, "matched": matched, "together": together, "grid": grid})
    return Result("synchrony", data, params, quality, summary,
                  {"lags": lags, "conditioned": cond, "null": pd.DataFrame({"null_r": null_r})}, notes,
                  _fig_synchrony, ctx)


def summary_ep(prox: Result) -> dict:
    s = prox.summary
    return {"n": s["episodes"], "together_h": s["together_h"], "median_min": s["median_episode_min"]}


def _fig_synchrony(r: Result, theme: str) -> dict:
    ctx, p, t = r._context, r.parameters, F.theme_of(theme)
    A, B = ctx["ids"]
    d = r.data
    if not len(d) or not r.summary["aligned_bins"]:
        return F.empty_figure(theme, "No bins where both devices have motion data")
    bin_s = ctx["bin_s"]
    gap = bin_s * 1.5
    traces = []
    for dev, col, ax, color in ((A, "a", "", ctx["colors"][A]), (B, "b", "2", ctx["colors"][B])):
        s = d[d[col].notna()]
        x, y = F.break_lines(s.bin_unix.to_numpy(), s[col].to_numpy(), ctx["tz"], gap)
        traces.append({"type": "scatter", "mode": "lines", "x": x, "y": y, "xaxis": f"x{ax}", "yaxis": f"y{ax}",
                       "line": {"width": 1.4, "color": color, "shape": "hv"}, "name": ctx["labels"][dev],
                       "connectgaps": False, "hovertemplate": "%{y:.0f}<br>%{x}<extra>" + ctx["labels"][dev] + "</extra>"})
    # bands where encounters were detected
    shapes = []
    tog = ctx["together"]
    i = 0
    while i < len(tog):
        if tog[i]:
            j = i
            while j + 1 < len(tog) and tog[j + 1]:
                j += 1
            x0, x1 = local_strings([ctx["grid"][i], ctx["grid"][j] + bin_s], ctx["tz"])
            shapes.append({"type": "rect", "xref": "x", "yref": "paper", "x0": x0, "x1": x1, "y0": 0.54, "y1": 1,
                           "fillcolor": "rgba(167,139,250,0.20)", "line": {"width": 0}, "layer": "below"})
            i = j + 1
        else:
            i += 1
    lags = ctx["lags"]
    traces.append({"type": "bar", "x": lags.lag_min.tolist(), "y": lags.r.tolist(), "xaxis": "x3", "yaxis": "y3",
                   "marker": {"color": [("#F472E6" if (v or 0) >= 0 else "#60A5FA") for v in lags.r.fillna(0)]},
                   "hovertemplate": "lag %{x:.0f} min<br>r = %{y:.2f}<extra></extra>", "showlegend": False})
    null_r = ctx["null_r"]
    if len(null_r):
        traces.append({"type": "histogram", "x": null_r.tolist(), "xaxis": "x4", "yaxis": "y4", "nbinsx": 24,
                       "marker": {"color": t["fade"], "line": {"width": 0}}, "showlegend": False,
                       "hovertemplate": "r %{x:.2f}<br>%{y} shifts<extra></extra>"})
    r_obs = r.summary["r"]
    ann = [
        {"text": "Activity (bands = encounters)", "xref": "paper", "yref": "paper", "x": 0, "y": 1.06, "showarrow": False,
         "xanchor": "left", "font": {"size": 12, "color": t["muted"]}},
        {"text": "Correlation by lag (b after a →)", "xref": "paper", "yref": "paper", "x": 0, "y": 0.40, "showarrow": False,
         "xanchor": "left", "font": {"size": 12, "color": t["muted"]}},
        {"text": f"Observed vs {r.summary['null_n']} {p['null'].replace('_', '-')} shifts", "xref": "paper", "yref": "paper",
         "x": 0.56, "y": 0.40, "showarrow": False, "xanchor": "left", "font": {"size": 12, "color": t["muted"]}},
    ]
    if r_obs is not None and np.isfinite(r_obs):
        shapes.append({"type": "line", "xref": "x4", "yref": "paper", "x0": r_obs, "x1": r_obs, "y0": 0, "y1": 0.34,
                       "line": {"color": "#F472E6", "width": 2.5}})
    return {"data": traces, "layout": F.layout(
        theme, height=640, shapes=shapes, annotations=ann, bargap=0.15, margin={"l": 70, "r": 24, "t": 44, "b": 56},
        showlegend=True, legend={"orientation": "h", "y": 1.12, "x": 1, "xanchor": "right"},
        xaxis=F.date_axis(theme, domain=[0, 1], anchor="y", showticklabels=False),
        yaxis=F.axis(theme, domain=[0.78, 1], rangemode="tozero", title={"text": ctx["labels"][A]}),
        xaxis2=F.date_axis(theme, domain=[0, 1], anchor="y2", matches="x", title={"text": f"Local time ({ctx['tz']})"}),
        yaxis2=F.axis(theme, domain=[0.54, 0.74], rangemode="tozero", title={"text": ctx["labels"][B]}),
        xaxis3=F.axis(theme, domain=[0, 0.46], anchor="y3", title={"text": "Lag (minutes)"}, zeroline=True),
        yaxis3=F.axis(theme, domain=[0, 0.34], anchor="x3", title={"text": f"{p['method'].title()} r"}, range=[-1, 1]),
        xaxis4=F.axis(theme, domain=[0.56, 1], anchor="y4", title={"text": f"{p['method'].title()} r"}, range=[-1, 1]),
        yaxis4=F.axis(theme, domain=[0, 0.34], anchor="x4", title={"text": "Shifts"}))}
