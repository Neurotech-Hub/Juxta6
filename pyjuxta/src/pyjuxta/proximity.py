"""BLE sightings (JXB): base-station visits, pairwise proximity, and the who-hears-whom matrix.

What a JXB row is: at the end of each ~1 s scan burst (every ``scan_interval_s``, 30 s by default) a tag
logs one row per Juxta name it heard, with the strongest RSSI over its two antennas, at most eight names per
burst. A missing row means the burst did not hear that name. It does not mean the name was far away, and a
burst that heard nothing leaves no row at all. RSSI is not a distance.
"""

from __future__ import annotations

import numpy as np
import pandas as pd

from . import figures as F
from ._time import local_strings, utc_index
from .result import Result

RSSI_NOTE = ("RSSI depends on body orientation, antenna, walls, and whether the 1 s advertise burst overlapped "
             "the other tag's 1 s scan. It is not a distance, and no row is not proof of absence.")
EPISODE_COLS = ["start", "end", "duration_s", "n_detections", "rssi_median", "rssi_max"]


# ── helpers ─────────────────────────────────────────────────────────────────────

def _windows(dev) -> np.ndarray:
    s = dev.sessions()
    return s[["start", "end"]].to_numpy(dtype="int64") if len(s) else np.zeros((0, 2), dtype="int64")


def _overlap(w1: np.ndarray, w2: np.ndarray) -> np.ndarray:
    """Intersection of two sorted lists of [start, end] intervals."""
    out, i, j = [], 0, 0
    while i < len(w1) and j < len(w2):
        lo, hi = max(w1[i, 0], w2[j, 0]), min(w1[i, 1], w2[j, 1])
        if hi > lo:
            out.append((lo, hi))
        if w1[i, 1] < w2[j, 1]:
            i += 1
        else:
            j += 1
    return np.array(out, dtype="int64").reshape(-1, 2)


def _in_windows(t: np.ndarray, w: np.ndarray) -> np.ndarray:
    if not len(w):
        return np.zeros(len(t), dtype=bool)
    idx = np.searchsorted(w[:, 0], t, side="right") - 1
    ok = idx >= 0
    return ok & (t <= w[np.clip(idx, 0, None), 1])


def _gaps(w: np.ndarray) -> list[tuple[int, int]]:
    return [(int(w[k, 1]), int(w[k + 1, 0])) for k in range(len(w) - 1)]


def build_episodes(unix, rssi, *, min_rssi: float, merge_gap_s: float, min_detections: int = 1,
                   scan_s: float = 0.0) -> pd.DataFrame:
    """Merge detections at or above ``min_rssi`` into episodes.

    Detections closer than ``merge_gap_s`` belong to the same episode. ``duration_s`` is last minus first
    detection, so a single detection has duration 0. ``duration_max_s`` adds one scan interval at each end,
    the most the episode could have lasted given that sampling.
    """
    unix = np.asarray(unix, dtype="int64")
    rssi = np.asarray(rssi, dtype="float64")
    keep = rssi >= min_rssi
    order = np.argsort(unix[keep], kind="stable")
    t, r = unix[keep][order], rssi[keep][order]
    cols = EPISODE_COLS + ["duration_max_s"]
    if not len(t):
        return pd.DataFrame({c: pd.Series(dtype="float64") for c in cols})
    cut = np.where(np.diff(t) > merge_gap_s)[0]
    first = np.r_[0, cut + 1]
    last = np.r_[cut, len(t) - 1]
    rows = []
    for a, b in zip(first, last):
        seg = r[a:b + 1]
        rows.append((int(t[a]), int(t[b]), int(t[b] - t[a]), int(b - a + 1), float(np.median(seg)), int(seg.max())))
    df = pd.DataFrame(rows, columns=EPISODE_COLS)
    df["duration_max_s"] = df.duration_s + 2 * scan_s
    return df[df.n_detections >= min_detections].reset_index(drop=True)


def _pair_obs(dep, a: str, b: str, direction: str) -> pd.DataFrame:
    """Sightings between ``a`` and ``b`` as ``unix, rssi, dir`` (``ab`` = a heard b)."""
    d = dep.detections
    parts = []
    if direction in ("either", "a_hears_b"):
        s = d[(d.observer == a) & (d.peer == b)][["unix", "rssi"]].assign(dir="ab")
        parts.append(s)
    if direction in ("either", "b_hears_a"):
        s = d[(d.observer == b) & (d.peer == a)][["unix", "rssi"]].assign(dir="ba")
        parts.append(s)
    if direction not in ("either", "a_hears_b", "b_hears_a"):
        raise ValueError("direction must be 'either', 'a_hears_b' or 'b_hears_a'")
    out = pd.concat(parts, ignore_index=True) if parts else pd.DataFrame(columns=["unix", "rssi", "dir"])
    return out.sort_values("unix", kind="stable").reset_index(drop=True)


def _fmt_ep(df: pd.DataFrame, tz: str) -> pd.DataFrame:
    df = df.copy()
    if len(df):
        df.insert(1, "start_local", local_strings(df.start, tz))
        df.insert(3, "end_local", local_strings(df.end, tz))
        df["duration_min"] = (df.duration_s / 60).round(1)
    return df


def _ctx(dep, ids) -> dict:
    return {"tz": dep.tz, "colors": {i: dep.color(i) for i in dep.devices},
            "labels": {i: dep.label(i) for i in dep.devices}, "roles": {i: dep.role(i) for i in dep.devices},
            "ids": list(ids)}


# ── base visits ─────────────────────────────────────────────────────────────────

def visits(dep, mobile, *, bases=None, min_rssi: float = -85, merge_gap_s: float = 180, min_detections: int = 2,
           direction: str = "either", nearest_bin_s: int = 300, min_margin_db: float = 3.0, agg: str = "max") -> Result:
    """Which base stations detected a mobile tag, and which was strongest.

    For each base the sightings in both directions (the mobile heard the base, and the base heard the mobile)
    are merged into *episodes*. Separately, each ``nearest_bin_s`` window gets a **strongest base**: the base
    with the highest RSSI in that window. A window is *ambiguous* when the runner-up is within ``min_margin_db``.

    Parameters
    ----------
    min_rssi
        Ignore sightings weaker than this (dBm).
    merge_gap_s
        Sightings closer together than this belong to one episode.
    min_detections
        Episodes with fewer sightings are dropped.
    direction
        ``"either"``, ``"mobile_hears_base"``, or ``"base_hears_mobile"``.
    agg
        How a window summarises one base's RSSI: ``"max"`` or ``"median"``.

    Presence at a base is inferred from detection. It is not a position, and two bases can detect the same tag
    at once, so episode times can overlap.
    """
    m = dep.device(mobile).id
    base_ids = dep.resolve_ids(bases) if bases is not None else [b for b in dep.bases if b != m]
    dmap = {"either": "either", "mobile_hears_base": "a_hears_b", "base_hears_mobile": "b_hears_a"}
    if direction not in dmap:
        raise ValueError("direction must be 'either', 'mobile_hears_base' or 'base_hears_mobile'")
    if agg not in ("max", "median"):
        raise ValueError("agg must be 'max' or 'median'")
    mdev = dep.devices[m]
    mw = _windows(mdev)
    recorded_s = float((mw[:, 1] - mw[:, 0]).sum()) if len(mw) else 0.0
    eps, obs_all = [], []
    by_base = []
    for b in base_ids:
        obs = _pair_obs(dep, m, b, dmap[direction])
        obs = obs[_in_windows(obs.unix.to_numpy(), mw)] if len(obs) else obs
        e = build_episodes(obs.unix, obs.rssi, min_rssi=min_rssi, merge_gap_s=merge_gap_s,
                           min_detections=min_detections, scan_s=mdev.scan_interval_s)
        e.insert(0, "base", b)
        e.insert(1, "label", dep.label(b))
        eps.append(e)
        strong = obs[obs.rssi >= min_rssi]
        if len(strong):
            t = strong.assign(bin_unix=(strong.unix // nearest_bin_s) * nearest_bin_s, base=b)
            obs_all.append(t[["bin_unix", "base", "rssi"]])
        by_base.append({"base": b, "label": dep.label(b), "episodes": int(len(e)),
                        "episode_hours": round(float(e.duration_s.sum()) / 3600, 2) if len(e) else 0.0,
                        "detections": int(len(strong)),
                        "rssi_median": float(strong.rssi.median()) if len(strong) else np.nan})
    episodes = pd.concat(eps, ignore_index=True) if eps else pd.DataFrame(columns=["base", "label"] + EPISODE_COLS)
    episodes = _fmt_ep(episodes, dep.tz)
    # strongest base per window
    if obs_all:
        allobs = pd.concat(obs_all, ignore_index=True)
        g = allobs.groupby(["bin_unix", "base"]).rssi.agg(agg).reset_index().sort_values(
            ["bin_unix", "rssi", "base"], ascending=[True, False, True], kind="stable")
        g["rank"] = g.groupby("bin_unix").cumcount()
        best = g[g["rank"] == 0].set_index("bin_unix")
        second = g[g["rank"] == 1].set_index("bin_unix").rssi
        nearest = pd.DataFrame({"base": best.base, "rssi": best.rssi, "runner_up_rssi": second}).reset_index()
        nearest["margin_db"] = nearest.rssi - nearest.runner_up_rssi
        nearest["ambiguous"] = nearest.margin_db < min_margin_db
        nearest.insert(1, "label", nearest.base.map(dep.label))
    else:
        nearest = pd.DataFrame(columns=["bin_unix", "label", "base", "rssi", "runner_up_rssi", "margin_db", "ambiguous"])
    n_bins_rec = max(int(np.ceil(recorded_s / nearest_bin_s)), 1) if recorded_s else 0
    bb = pd.DataFrame(by_base)
    if len(bb):
        counts = nearest.groupby("base").size() if len(nearest) else pd.Series(dtype=int)
        bb["strongest_bins"] = bb.base.map(counts).fillna(0).astype(int)
        total = int(len(nearest))
        bb["share_of_located"] = (bb.strongest_bins / total).round(3) if total else np.nan
        bb["share_of_recorded"] = (bb.strongest_bins / n_bins_rec).round(3) if n_bins_rec else np.nan
    top = bb.sort_values("strongest_bins", ascending=False).iloc[0] if len(bb) and len(nearest) else None
    summary = {
        "mobile": m, "recorded_h": round(recorded_s / 3600, 1),
        "episodes": int(len(episodes)),
        "located_share": round(len(nearest) / n_bins_rec, 3) if n_bins_rec else None,
        "ambiguous_share": round(float(nearest.ambiguous.mean()), 3) if len(nearest) else None,
        "top_base": top.label if top is not None else None,
        "top_base_share_of_located": float(top.share_of_located) if top is not None else None,
    }
    quality = {"coverage": summary["located_share"], "n_observations": int(bb.detections.sum()) if len(bb) else 0}
    notes = [RSSI_NOTE, "Episodes can overlap: several bases may detect the same tag at once. "
             "The strongest base per window is a hint about where the tag was, not a location.",
             "Grey bands: the tag was not recording."]
    params = {"mobile": m, "min_rssi": min_rssi, "merge_gap_s": merge_gap_s, "min_detections": min_detections,
              "direction": direction, "nearest_bin_s": nearest_bin_s, "min_margin_db": min_margin_db, "agg": agg}
    ctx = _ctx(dep, base_ids)
    ctx["off"] = _gaps(mw)
    ctx["span"] = (int(mw[0, 0]), int(mw[-1, 1])) if len(mw) else None
    return Result("visits", episodes, params, quality, summary, {"by_base": bb, "nearest": nearest}, notes,
                  _fig_visits, ctx)


def _off_shapes(off, tz, theme) -> list[dict]:
    t = F.theme_of(theme)
    shapes = []
    for a, b in off:
        x0, x1 = local_strings([a, b], tz)
        shapes.append({"type": "rect", "xref": "x", "yref": "paper", "x0": x0, "x1": x1, "y0": 0, "y1": 1,
                       "fillcolor": t["fade"], "opacity": 0.5, "line": {"width": 0}, "layer": "below"})
    return shapes


def _fig_visits(r: Result, theme: str) -> dict:
    ctx, p, t = r._context, r.parameters, F.theme_of(theme)
    ids = ctx["ids"]
    labels = [ctx["labels"][i] for i in ids]
    traces = []
    for b in ids:
        e = r.data[r.data.base == b] if len(r.data) else r.data
        xs, ys = [], []
        for row in e.itertuples(index=False):
            xs += [row.start_local, row.end_local, None]
            ys += [ctx["labels"][b], ctx["labels"][b], None]
        if xs:
            traces.append({"type": "scatter", "mode": "lines+markers", "x": xs, "y": ys, "name": ctx["labels"][b],
                           "line": {"width": 16, "color": ctx["colors"][b]}, "opacity": 0.55, "connectgaps": False,
                           "marker": {"symbol": "square", "size": 12, "color": ctx["colors"][b]},
                           "hoverinfo": "skip", "showlegend": False})
    near = r.tables["nearest"]
    if len(near):
        for amb, name, fill in ((False, "Strongest base", t["line"]), (True, "Ambiguous (runner-up within margin)", "rgba(0,0,0,0)")):
            s = near[near.ambiguous == amb]
            if not len(s):
                continue
            traces.append({"type": "scatter", "mode": "markers", "x": local_strings(s.bin_unix + p["nearest_bin_s"] // 2, ctx["tz"]),
                           "y": s.label.tolist(), "name": name, "customdata": s.rssi.tolist(),
                           "marker": {"size": 6, "color": fill, "line": {"width": 1, "color": t["line"]}},
                           "hovertemplate": "%{y}: %{customdata:.0f} dBm<br>%{x}<extra></extra>"})
    if not traces:
        return F.empty_figure(theme, "No sightings above the RSSI threshold")
    return {"data": traces, "layout": F.layout(
        theme, height=70 * len(ids) + 140, margin={"l": 100, "r": 24, "t": 40, "b": 56},
        shapes=_off_shapes(ctx["off"], ctx["tz"], theme),
        xaxis=F.date_axis(theme, title={"text": f"Local time ({ctx['tz']})"}),
        yaxis=F.axis(theme, categoryorder="array", categoryarray=labels[::-1], showgrid=True))}


# ── pair proximity ──────────────────────────────────────────────────────────────

def proximity(dep, a, b, *, min_rssi: float = -75, merge_gap_s: float = 90, min_detections: int = 2,
              direction: str = "either", window_s: int = 120) -> Result:
    """Encounters between two devices from their sightings of each other.

    Sightings at or above ``min_rssi`` that are closer than ``merge_gap_s`` are merged into *episodes*.
    Only time when **both** devices were recording can contain an episode, so ``coverage`` is that overlap.

    Parameters
    ----------
    min_rssi
        Weakest sighting that counts (dBm). The same pair gives different episodes at -60 and -90.
    merge_gap_s
        Longest silence inside one episode. With a 30 s scan cadence, 90 s tolerates two missed scans.
    min_detections
        Episodes with fewer sightings are dropped.
    direction
        ``"either"`` (default), ``"a_hears_b"``, or ``"b_hears_a"``.
    window_s
        Window for the reciprocity statistic.

    Reciprocity asks how often a sighting in one direction has a sighting in the other direction in the same
    window. Both tags need to be scanning and advertising at overlapping seconds, so it is below 1 even for
    tags that sat side by side.
    """
    A, B = dep.device(a).id, dep.device(b).id
    da, db = dep.devices[A], dep.devices[B]
    ov = _overlap(_windows(da), _windows(db))
    overlap_s = float((ov[:, 1] - ov[:, 0]).sum()) if len(ov) else 0.0
    obs = _pair_obs(dep, A, B, direction)
    obs = obs[_in_windows(obs.unix.to_numpy(), ov)] if len(obs) else obs
    eps = build_episodes(obs.unix, obs.rssi, min_rssi=min_rssi, merge_gap_s=merge_gap_s,
                         min_detections=min_detections, scan_s=max(da.scan_interval_s, db.scan_interval_s))
    eps = _fmt_ep(eps, dep.tz)
    strong = obs[obs.rssi >= min_rssi]
    ab, ba = strong[strong["dir"] == "ab"], strong[strong["dir"] == "ba"]
    exp_a = overlap_s / da.scan_interval_s if da.scan_interval_s else 0.0
    exp_b = overlap_s / db.scan_interval_s if db.scan_interval_s else 0.0
    wa = set((ab.unix // window_s).tolist())
    wb = set((ba.unix // window_s).tolist())
    both, either = wa & wb, wa | wb
    ep_time = float(eps.duration_s.sum()) if len(eps) else 0.0
    summary = {
        "a": A, "b": B, "overlap_h": round(overlap_s / 3600, 2),
        "episodes": int(len(eps)),
        "together_h": round(ep_time / 3600, 2),
        "together_share_of_overlap": round(ep_time / overlap_s, 3) if overlap_s else None,
        "median_episode_min": round(float(eps.duration_s.median()) / 60, 1) if len(eps) else None,
        "longest_episode_min": round(float(eps.duration_s.max()) / 60, 1) if len(eps) else None,
        "rate_a_hears_b": round(len(ab) / exp_a, 3) if exp_a else None,
        "rate_b_hears_a": round(len(ba) / exp_b, 3) if exp_b else None,
        "reciprocity_jaccard": round(len(both) / len(either), 3) if either else None,
        "p_b_hears_a_given_a_hears_b": round(len(both) / len(wa), 3) if wa else None,
        "median_rssi": float(strong.rssi.median()) if len(strong) else None,
    }
    quality = {"coverage": round(overlap_s / max(min(_dur(da), _dur(db)), 1), 3), "overlap_h": summary["overlap_h"],
               "n_observations": int(len(obs))}
    notes = [RSSI_NOTE,
             "Episode length is first to last sighting. Add up to one scan interval at each end for the most it "
             "could have lasted (the duration_max_s column).",
             "Grey bands: at least one of the two tags was not recording."]
    params = {"a": A, "b": B, "min_rssi": min_rssi, "merge_gap_s": merge_gap_s, "min_detections": min_detections,
              "direction": direction, "window_s": window_s}
    ctx = _ctx(dep, [A, B])
    ctx["off"] = _gaps(ov)
    ctx["obs"] = obs
    return Result("proximity", eps, params, quality, summary, {}, notes, _fig_proximity, ctx)


def _dur(dev) -> float:
    s = dev.sessions()
    return float(s.duration_s.sum()) if len(s) else 0.0


def _fig_proximity(r: Result, theme: str) -> dict:
    ctx, p, t = r._context, r.parameters, F.theme_of(theme)
    A, B = ctx["ids"]
    obs = ctx["obs"]
    if not len(obs):
        return F.empty_figure(theme, "These two devices never heard each other")
    traces = []
    for key, src, dst in (("ab", A, B), ("ba", B, A)):
        s = obs[obs["dir"] == key]
        if len(s):
            traces.append({"type": "scatter", "mode": "markers", "x": local_strings(s.unix, ctx["tz"]),
                           "y": s.rssi.tolist(), "xaxis": "x", "yaxis": "y",
                           "name": f"{ctx['labels'][src]} hears {ctx['labels'][dst]}",
                           "marker": {"size": 4, "color": ctx["colors"][src], "opacity": 0.65},
                           "hovertemplate": "%{y} dBm<br>%{x}<extra>" + f"{ctx['labels'][src]} hears {ctx['labels'][dst]}" + "</extra>"})
    xs, ys = [], []
    for row in r.data.itertuples(index=False):
        xs += [row.start_local, row.end_local, None]
        ys += ["Together", "Together", None]
    traces.append({"type": "scatter", "x": xs, "y": ys, "xaxis": "x", "yaxis": "y2",
                   "line": {"width": 18, "color": "#A78BFA"}, "name": "Encounter episodes", "connectgaps": False,
                   "mode": "lines+markers", "marker": {"symbol": "square", "size": 14, "color": "#A78BFA"},
                   "hovertemplate": "%{x}<extra>episode</extra>"})
    shapes = _off_shapes(ctx["off"], ctx["tz"], theme)
    shapes.append({"type": "line", "xref": "paper", "x0": 0, "x1": 1, "yref": "y", "y0": p["min_rssi"], "y1": p["min_rssi"],
                   "line": {"color": t["muted"], "width": 1, "dash": "dot"}})
    return {"data": traces, "layout": F.layout(
        theme, height=420, shapes=shapes, margin={"l": 80, "r": 24, "t": 40, "b": 56},
        xaxis=F.date_axis(theme, anchor="y2", title={"text": f"Local time ({ctx['tz']})"}),
        yaxis=F.axis(theme, domain=[0.22, 1], title={"text": "RSSI (dBm)"}),
        yaxis2=F.axis(theme, domain=[0, 0.14], type="category", showgrid=False, anchor="x"))}


# ── who hears whom ──────────────────────────────────────────────────────────────

def proximity_matrix(dep, *, min_rssi: float = -90, metric: str = "rate", devices=None) -> Result:
    """For every ordered pair (observer, peer): how often the observer heard the peer.

    ``metric="rate"`` is the share of the observer's scan bursts, while both were recording, in which the peer
    was heard at or above ``min_rssi``. ``"median_rssi"`` is the median RSSI of those sightings. Rows are who
    listened, columns are who was heard, so the matrix is not symmetric.
    """
    if metric not in ("rate", "median_rssi"):
        raise ValueError("metric must be 'rate' or 'median_rssi'")
    ids = dep.resolve_ids(devices) if devices is not None else dep.mobiles + dep.bases
    det = dep.detections
    rows = []
    for o in ids:
        od = dep.devices[o]
        ow = _windows(od)
        for q in ids:
            if o == q:
                continue
            ov = _overlap(ow, _windows(dep.devices[q]))
            ov_s = float((ov[:, 1] - ov[:, 0]).sum()) if len(ov) else 0.0
            s = det[(det.observer == o) & (det.peer == q) & (det.rssi >= min_rssi)]
            s = s[_in_windows(s.unix.to_numpy(), ov)] if len(s) else s
            exp = ov_s / od.scan_interval_s if od.scan_interval_s else 0.0
            rows.append({"observer": o, "peer": q, "observer_label": dep.label(o), "peer_label": dep.label(q),
                         "rate": (len(s) / exp) if exp else np.nan, "detections": int(len(s)),
                         "median_rssi": float(s.rssi.median()) if len(s) else np.nan,
                         "overlap_h": round(ov_s / 3600, 2)})
    data = pd.DataFrame(rows)
    summary = {}
    if len(data) and data.rate.notna().any():
        hi = data.loc[data.rate.idxmax()]
        summary = {"strongest_pair": f"{hi.observer_label} hears {hi.peer_label}", "strongest_rate": float(hi.rate)}
    # The cell to point at is the strongest link between a mobile and a base (in the chosen metric): that is the
    # one that says where a worn tag was. Base-to-base and mobile-to-mobile cells are context.
    if len(data):
        roles = {i: dep.role(i) for i in ids}
        one_each = np.array([{roles[o], roles[q]} == {"mobile", "base"} for o, q in zip(data.observer, data.peer)])
        cand = data[one_each & data[metric].notna().to_numpy()]
        if len(cand):
            best = cand.loc[cand[metric].idxmax()]
            summary.update({"best_pair": f"{best.observer_label} hears {best.peer_label}",
                            "best_observer": best.observer, "best_peer": best.peer,
                            "best_value": float(best[metric]), "best_metric": metric})
    notes = [RSSI_NOTE, "Rows listen, columns are heard. A low rate can mean far apart, shielded, or simply that "
             "the two tags' short scan and advertise bursts rarely overlapped."]
    if any(dep.role(i) == "mobile" for i in ids) and sum(dep.role(i) == "base" for i in ids) > 1:
        notes.append("Cells between two base stations are drawn in grey: they show how the house hangs together, "
                     "but the mobiles are the question.")
    if "best_pair" in summary:
        notes.append("The outlined cell is the strongest link between a mobile and a base station in the chosen measure.")
    params = {"min_rssi": min_rssi, "metric": metric}
    return Result("proximity_matrix", data, params, {"n_observations": int(data.detections.sum()) if len(data) else 0},
                  summary, {}, notes, _fig_matrix, _ctx(dep, ids))


def _fig_matrix(r: Result, theme: str) -> dict:
    ctx, p, t = r._context, r.parameters, F.theme_of(theme)
    ids = ctx["ids"]
    labels = [ctx["labels"][i] for i in ids]
    col = p["metric"]
    z = np.full((len(ids), len(ids)), np.nan)
    ix = {i: k for k, i in enumerate(ids)}
    for row in r.data.itertuples(index=False):
        z[ix[row.observer], ix[row.peer]] = getattr(row, col)
    pct = col == "rate"
    # Brighter always means more: a higher share heard, or a stronger (less negative) signal. Cells with a
    # mobile on either side keep the colour scale; base-to-base cells use the same values in grey.
    mob = np.array([ctx["roles"][i] == "mobile" for i in ids])
    keep = (mob[:, None] | mob[None, :]) if mob.any() else np.ones((len(ids), len(ids)), dtype=bool)
    finite = z[np.isfinite(z)]
    if pct:
        zmin, zmax = 0.0, (float(finite.max()) if len(finite) and finite.max() > 0 else 1.0)
    else:
        zmin, zmax = (float(finite.min()), float(finite.max())) if len(finite) else (-100.0, -40.0)
        if zmax <= zmin:
            zmax = zmin + 1.0
    hover = "%{y} hears %{x}<br>" + ("%{z:.0%}" if pct else "%{z:.0f} dBm") + "<extra></extra>"

    def layer(mask: np.ndarray, **over) -> dict:
        zz = np.where(mask, z, np.nan)
        text = [["" if np.isnan(v) else (f"{v:.0%}" if pct else f"{v:.0f}") for v in row] for row in zz]
        return {"type": "heatmap", "x": labels, "y": labels, "z": zz.tolist(), "text": text,
                "texttemplate": "%{text}", "hoverongaps": False, "zmin": zmin, "zmax": zmax,
                "hovertemplate": hover, **over}

    data = []
    if not keep.all():
        data.append(layer(~keep, colorscale=t["gray"], showscale=False))
    data.append(layer(keep, colorscale=t["scale"],
                      colorbar={"title": {"text": "heard" if pct else "dBm"}, "tickformat": ".0%" if pct else None,
                                "thickness": 12, "outlinewidth": 0}))
    shapes = []
    s = r.summary
    if "best_pair" in s:  # category axes take index positions, so a cell is [k - 0.5, k + 0.5]
        i, j = ix[s["best_observer"]], ix[s["best_peer"]]
        shapes.append({"type": "rect", "xref": "x", "yref": "y", "x0": j - 0.5, "x1": j + 0.5, "y0": i - 0.5,
                       "y1": i + 0.5, "fillcolor": "rgba(0,0,0,0)", "layer": "above",
                       "line": {"color": t["line"], "width": 3}})
    return {"data": data, "layout": F.layout(
        theme, height=60 * len(ids) + 130, margin={"l": 100, "r": 24, "t": 30, "b": 80}, shapes=shapes,
        xaxis=F.axis(theme, side="bottom", title={"text": "Heard"}, showgrid=False),
        yaxis=F.axis(theme, autorange="reversed", title={"text": "Listener"}, showgrid=False))}


# ── one tag and what it heard ───────────────────────────────────────────────────

NETWORK_MAX_PEERS = 40
NETWORK_RSSI_NEAR, NETWORK_RSSI_FAR = -50.0, -95.0   # inner and outer ring
NETWORK_COLOR_MIN, NETWORK_COLOR_MAX = -95.0, -55.0  # same limits as the plotter's peer colours


def proximity_network(dep, center, *, min_rssi: float = -90, devices=None) -> Result:
    """One tag at the centre and every other tag it heard around it: the matrix's row, drawn as a graph.

    Same numbers as one row of :func:`proximity_matrix`: ``rate`` is the share of the centre tag's scan bursts,
    while both were recording, in which that peer was heard at or above ``min_rssi``, and ``median_rssi`` is the
    median of those sightings. Distance from the centre and colour show the median signal (nearer and warmer is
    stronger); edge width and node size show the share of scans. Tags that were not heard are listed in
    ``data`` with zero detections and left off the figure. Only what ``center`` heard is drawn: for the other
    direction, make the peer the centre.
    """
    c = dep.device(center).id
    ids = list(dep.resolve_ids(devices)) if devices is not None else dep.mobiles + dep.bases
    if c not in ids:
        ids.append(c)
    m = proximity_matrix(dep, min_rssi=min_rssi, metric="rate", devices=ids)
    data = m.data[m.data.observer == c].copy()
    data["role"] = [dep.role(q) for q in data.peer]
    data = data.sort_values(["rate", "detections", "peer_label"], ascending=[False, False, True],
                            na_position="last").reset_index(drop=True)
    heard = data[data.detections > 0]
    role = dep.role(c)
    other = "base" if role == "mobile" else "mobile"
    summary = {"center": c, "center_label": dep.label(c), "center_role": role,
               "n_peers": int(len(data)), "n_heard": int(len(heard))}
    # As in the matrix, the link worth naming joins a mobile to a base.
    cand = heard[heard.role == other]
    if len(cand):
        best = cand.iloc[0]  # sorted by share of scans
        summary.update({"best_pair": f"{best.observer_label} hears {best.peer_label}", "best_observer": best.observer,
                        "best_peer": best.peer, "best_peer_label": best.peer_label, "best_value": float(best.rate),
                        "best_metric": "rate", "best_detections": int(best.detections),
                        "best_median_rssi": float(best.median_rssi)})
    notes = [RSSI_NOTE, "Only what the centre tag heard is drawn. A peer's own view of the centre can differ: "
             "make that peer the centre to see it."]
    if len(heard) > NETWORK_MAX_PEERS:
        notes.append(f"Showing the {NETWORK_MAX_PEERS} peers heard most often of {len(heard)}.")
    silent = data[data.detections == 0].peer_label.tolist()
    if silent:
        notes.append("Not heard at this threshold: " + ", ".join(silent) + ".")
    if "best_pair" in summary:
        notes.append(f"The outlined peer is the strongest link between the centre and a {other}, by share of scans.")
    return Result("proximity_network", data, {"center": c, "min_rssi": min_rssi}, {"n_observations": int(data.detections.sum())},
                  summary, {}, notes, _fig_network, _ctx(dep, ids))


def _network_radius(rssi: float) -> float:
    span = NETWORK_RSSI_NEAR - NETWORK_RSSI_FAR
    return 1.0 + 2.0 * min(1.0, max(0.0, (NETWORK_RSSI_NEAR - rssi) / span))


def _label_position(angle: float) -> str:
    c, s = np.cos(angle), np.sin(angle)
    if c > 0.35:
        return "middle right"
    if c < -0.35:
        return "middle left"
    return "top center" if s > 0 else "bottom center"


def _fig_network(r: Result, theme: str) -> dict:
    ctx, p, t, s = r._context, r.parameters, F.theme_of(theme), r.summary
    heard = r.data[r.data.detections > 0].head(NETWORK_MAX_PEERS).reset_index(drop=True)
    plain = {"visible": False, "fixedrange": True}
    if not len(heard):
        return F.empty_figure(theme, f"{s['center_label']} heard no other tag at or above {p['min_rssi']:.0f} dBm")
    n = len(heard)
    top = float(heard.rate.max()) or 1.0
    step = 2 * np.pi / n
    ang = np.pi / 2 - np.arange(n) * step                 # clockwise from the top, most-heard first
    rad = np.array([_network_radius(v) for v in heard.median_rssi])
    x, y = rad * np.cos(ang), rad * np.sin(ang)
    size = 14 + 22 * np.sqrt(heard.rate.to_numpy() / top)
    span = NETWORK_COLOR_MAX - NETWORK_COLOR_MIN

    data = [{"type": "scatter", "mode": "lines", "x": [0, float(x[k])], "y": [0, float(y[k])], "hoverinfo": "skip",
             "showlegend": False, "opacity": 0.8,
             "line": {"width": 1.5 + 8 * float(np.sqrt(heard.rate[k] / top)),
                      "color": F.scale_color(t["scale"], (heard.median_rssi[k] - NETWORK_COLOR_MIN) / span)}}
            for k in range(n)]
    best = [q == s.get("best_peer") for q in heard.peer]
    hover = [f"{row.peer_label} ({row.role})<br>heard in {row.rate:.0%} of {s['center_label']}'s scans"
             f"<br>{row.detections} sighting{'' if row.detections == 1 else 's'}, median {row.median_rssi:.0f} dBm"
             for row in heard.itertuples(index=False)]
    data.append({"type": "scatter", "mode": "markers+text", "x": x.tolist(), "y": y.tolist(),
                 "text": [f"{a} ({b:.0%})" for a, b in zip(heard.peer_label, heard.rate)],
                 "textposition": [_label_position(a) for a in ang], "textfont": {"size": 11, "color": t["text"]},
                 "hovertext": hover, "hoverinfo": "text", "showlegend": False,
                 "marker": {"size": size.tolist(), "symbol": ["diamond" if q == "mobile" else "circle" for q in heard.role],
                            "color": heard.median_rssi.tolist(), "colorscale": t["scale"], "cmin": NETWORK_COLOR_MIN,
                            "cmax": NETWORK_COLOR_MAX, "opacity": 1, "showscale": True,
                            "colorbar": {"title": {"text": "median signal (dBm)", "side": "top"}, "orientation": "h", "thickness": 10,
                                         "len": 0.5, "x": 0.5, "xanchor": "center", "y": -0.02, "yanchor": "top",
                                         "outlinewidth": 0},
                            "line": {"color": t["line"], "width": [3.5 if b else 1.2 for b in best]}}})
    data.append({"type": "scatter", "mode": "markers+text", "x": [0], "y": [0], "text": [s["center_label"]],
                 "textposition": "bottom center", "textfont": {"size": 12, "color": t["text"]}, "showlegend": False,
                 "hovertext": [f"{s['center_label']} ({s['center_role']})<br>heard {s['n_heard']} of {s['n_peers']} tags"],
                 "hoverinfo": "text",
                 "marker": {"size": 38, "color": "#FFFFFF", "opacity": 1, "line": {"color": "#8B5CF6", "width": 4}}})

    rings = (1.0, 2.0, 3.0)
    shapes = [{"type": "circle", "xref": "x", "yref": "y", "x0": -q, "x1": q, "y0": -q, "y1": q, "layer": "below",
               "line": {"color": t["grid"], "width": 1, "dash": "dot"}} for q in rings]
    at = np.pi / 2 - step / 2                             # between two nodes
    notes = [{"x": q * float(np.cos(at)), "y": q * float(np.sin(at)), "xref": "x", "yref": "y", "showarrow": False,
              "text": f"{NETWORK_RSSI_NEAR - (q - 1) / 2 * (NETWORK_RSSI_NEAR - NETWORK_RSSI_FAR):.0f} dBm",
              "font": {"size": 9, "color": t["muted"]}} for q in rings]
    # Wider than tall: labels sit beside the nodes, and the colour bar goes underneath.
    return {"data": data, "layout": F.layout(
        theme, height=540, margin={"l": 20, "r": 20, "t": 10, "b": 70}, shapes=shapes, annotations=notes, showlegend=False,
        xaxis={**plain, "range": [-5.8, 5.8]},
        yaxis={**plain, "range": [-3.9, 3.9], "scaleanchor": "x", "scaleratio": 1})}
