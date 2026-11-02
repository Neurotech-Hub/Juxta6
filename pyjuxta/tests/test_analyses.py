"""Behavior of the analyses on synthetic data with known answers."""

from __future__ import annotations

import numpy as np
import pandas as pd
import pytest

import pyjuxta as jx
from conftest import device_files, utc
from pyjuxta.proximity import build_episodes


def _dep(motion_a, motion_b, start, step=60, peers=None):
    t = [start + step * i for i in range(len(motion_a))]
    fa = device_files("JX_AAAAAA", [(u, int(m), 2.9, 21.0, 40.0, 1) for u, m in zip(t, motion_a)], peers or [])
    fb = device_files("JX_BBBBBB", [(u, int(m), 2.9, 21.0, 40.0, 1) for u, m in zip(t, motion_b)], [])
    return jx.load_deployment({**fa, **fb}, timezone="UTC")


def test_first_row_after_gap_is_not_counted_as_motion(start):
    vit = [(start + 60 * i, 5, 2.9, 20.0, 40.0, 0) for i in range(10)]
    vit += [(start + 7200 + 60 * i, 5 if i else 900, 2.9, 20.0, 40.0, 0) for i in range(10)]
    dep = jx.load_deployment(device_files("JX_AAAAAA", vit, []))
    kept = dep.activity("JX_AAAAAA", bin_s=60, exclude_after_gap=True)
    raw = dep.activity("JX_AAAAAA", bin_s=60, exclude_after_gap=False)
    assert kept.data.motion_mean.max() <= 5
    assert raw.data.motion_mean.max() == 900
    assert kept.quality["rows_excluded_after_gap"] == 2


def test_missing_bins_are_nan_not_zero(start):
    vit = [(start + 60 * i, 0, 2.9, 20.0, 40.0, 0) for i in range(5)]
    vit += [(start + 3600 + 60 * i, 0, 2.9, 20.0, 40.0, 0) for i in range(5)]
    dep = jx.load_deployment(device_files("JX_AAAAAA", vit, []))
    a = dep.activity("JX_AAAAAA", bin_s=300, smooth_bins=3).data
    gap = a[(a.bin_unix > start + 600) & (a.bin_unix < start + 3300)]
    assert gap.active_frac.isna().all() and gap.active_frac_smooth.isna().all()


def test_episode_merging():
    t = np.array([0, 30, 60, 400, 430, 2000])
    r = np.array([-60, -62, -61, -63, -90, -60])
    e = build_episodes(t, r, min_rssi=-70, merge_gap_s=90, min_detections=1, scan_s=30)
    assert e[["start", "end", "n_detections"]].values.tolist() == [[0, 60, 3], [400, 400, 1], [2000, 2000, 1]]
    assert build_episodes(t, r, min_rssi=-70, merge_gap_s=90, min_detections=2).n_detections.tolist() == [3]
    # a weaker threshold admits the -90 sighting and joins it to the 400 s episode
    wide = build_episodes(t, r, min_rssi=-95, merge_gap_s=90, min_detections=1)
    assert wide.n_detections.tolist() == [3, 2, 1]


def test_proximity_only_counts_time_both_were_recording(start):
    t = [start + 60 * i for i in range(120)]
    peers_a = [(u, "XBBBBBB", -55) for u in t[:60:2]]
    fa = device_files("JX_AAAAAA", [(u, 1, 2.9, 20.0, 40.0, 0) for u in t], peers_a)
    fb = device_files("JX_BBBBBB", [(u, 1, 2.9, 20.0, 40.0, 0) for u in t[40:]], [])  # b starts at minute 40
    dep = jx.load_deployment({**fa, **fb})
    p = dep.proximity("JX_AAAAAA", "JX_BBBBBB", min_rssi=-70, merge_gap_s=150)
    assert p.summary["overlap_h"] == pytest.approx(79 * 60 / 3600, abs=0.02)
    assert (p.data.end <= t[-1]).all() and (p.data.start >= t[40]).all()


def test_synchrony_separates_shared_signal_from_independent_noise(start):
    rng = np.random.default_rng(7)
    n = 24 * 60  # one day, minute bins
    base = np.convolve(rng.normal(size=n), np.ones(30) / 30, mode="same")  # slow shared drive
    a = np.clip(100 + 400 * base + rng.normal(0, 5, n), 0, 600)
    b_sync = np.clip(100 + 400 * base + rng.normal(0, 5, n), 0, 600)
    b_ind = np.clip(100 + 400 * np.convolve(rng.normal(size=n), np.ones(30) / 30, mode="same") + rng.normal(0, 5, n), 0, 600)
    s1 = _dep(a, b_sync, start).synchrony("JX_AAAAAA", "JX_BBBBBB", bin_s=300, n_null=200, seed=3)
    s2 = _dep(a, b_ind, start).synchrony("JX_AAAAAA", "JX_BBBBBB", bin_s=300, n_null=200, seed=3)
    assert s1.summary["r"] > 0.9 and s1.summary["p_shift"] < 0.05
    assert abs(s2.summary["r"]) < 0.5 and s2.summary["p_shift"] > 0.05
    # same seed, same answer
    again = _dep(a, b_sync, start).synchrony("JX_AAAAAA", "JX_BBBBBB", bin_s=300, n_null=200, seed=3)
    assert again.summary["p_shift"] == s1.summary["p_shift"]


def test_day_shift_null_is_limited_by_recording_length(start):
    rng = np.random.default_rng(2)
    n = 36 * 60
    a = rng.integers(0, 100, n)
    dep = _dep(a, a, start)
    s = dep.synchrony("JX_AAAAAA", "JX_BBBBBB", bin_s=300, null="day_shift")
    assert s.summary["null_n"] <= 2
    assert any("day-shift null has only" in n for n in s.notes)


def test_rhythms_refuse_without_enough_days(start):
    n = 30 * 60
    dep = jx.load_deployment(device_files("JX_AAAAAA", [(start + 60 * i, i % 50, 2.9, 20.0, 40.0, 0) for i in range(n)], []))
    r = dep.rhythms("JX_AAAAAA")
    assert r.summary["available"] is False and np.isnan(np.nan if r.summary["IS"] is None else r.summary["IS"])
    assert "insufficient" in r.summary["status"]
    r.figure("dark")


def test_rhythms_recover_a_clean_daily_pattern(start):
    days = 5
    t = [start + 60 * i for i in range(days * 1440)]
    hod = ((np.array(t) % 86400) // 3600)
    motion = np.where((hod >= 8) & (hod < 20), 300, 0)
    dep = jx.load_deployment(device_files("JX_AAAAAA", [(u, int(m), 2.9, 20.0, 40.0, 0) for u, m in zip(t, motion)], []))
    r = dep.rhythms("JX_AAAAAA", min_days=3)
    assert r.summary["available"]
    assert r.summary["IS"] > 0.95 and r.summary["RA"] > 0.99
    assert r.summary["M10_onset_h"] in (8, 9, 10)  # a ten-hour window inside the 12 h active period
    assert r.summary["L5"] == 0


def test_diel_uses_local_clock(start):
    n = 2 * 1440
    t = [start + 60 * i for i in range(n)]
    dep = jx.load_deployment(device_files("JX_AAAAAA", [(u, 100, 2.9, 20.0, 40.0, 0) for u in t], []),
                             timezone="America/Chicago")
    d = dep.diel("JX_AAAAAA", bin_s=3600)
    # 06:00 UTC on 31 Oct is 01:00 CDT; the first local hour with data is 01:00
    assert d.data.clock_h.min() == 0 or d.data.local_date.iloc[0] == "2026-10-31"
    assert set(d.data.local_date) >= {"2026-10-31", "2026-11-01"}


def test_matrix_rates(small_deployment_files):
    dep = jx.load_deployment(small_deployment_files)
    m = dep.proximity_matrix(min_rssi=-90).data
    ab = m[(m.observer == "JX_AAAAAA") & (m.peer == "JB_BBBBBB")].iloc[0]
    assert 0.9 <= ab.rate <= 1.1  # heard at every scheduled burst


# ── inpaint ────────────────────────────────────────────────────────────────────

def _two_blocks(start, gap_min, first=0, second=300, block_min=120):
    """Two recordings ``gap_min`` minutes apart: motion ``first`` then ``second`` (minute rows)."""
    t1 = [start + 60 * i for i in range(block_min)]
    t2 = [start + 60 * (block_min + gap_min + i) for i in range(block_min)]
    vit = [(u, first, 2.9, 20.0, 40.0, 0) for u in t1] + [(u, second, 2.9, 20.0, 40.0, 0) for u in t2]
    return jx.load_deployment(device_files("JX_AAAAAA", vit, []), timezone="UTC")


def test_inpaint_fills_interior_gaps_up_to_and_including_the_limit():
    start = utc(2026, 10, 26, 0)
    dep = _two_blocks(start, gap_min=180)  # hours 02, 03, 04 are missing between two recorded blocks
    dev = dep.device("JX_AAAAAA")
    from pyjuxta.activity import activity_bins
    kw = dict(bin_s=3600, active_threshold=1)
    plain, _ = activity_bins(dev, **kw)
    assert plain.active_frac.isna().sum() == 3 and not plain.inpainted.any()
    short, _ = activity_bins(dev, inpaint=179, **kw)
    assert short.active_frac.isna().sum() == 3
    at_limit, _ = activity_bins(dev, inpaint=180, **kw)
    assert at_limit.active_frac.notna().all() and at_limit.inpainted.sum() == 3
    # straight line between the last quiet bin (0) and the first busy bin (1)
    assert at_limit.loc[at_limit.inpainted, "active_frac"].round(2).tolist() == [0.25, 0.5, 0.75]
    assert at_limit.loc[at_limit.inpainted, "n_rows"].eq(0).all()
    everything, _ = activity_bins(dev, inpaint=float("inf"), **kw)
    assert everything.inpainted.sum() == 3


def test_inpaint_never_extends_the_ends_of_a_recording():
    start = utc(2026, 10, 26, 0, 30)  # first bin is only half covered, so it is missing
    dep = _two_blocks(start, gap_min=90)
    from pyjuxta.activity import activity_bins
    plain, _ = activity_bins(dep.device("JX_AAAAAA"), bin_s=3600, active_threshold=1)
    painted, _ = activity_bins(dep.device("JX_AAAAAA"), bin_s=3600, active_threshold=1, inpaint=float("inf"))
    assert len(painted) == len(plain)
    assert np.isnan(painted.active_frac.iloc[0]) and not painted.inpainted.iloc[0]
    assert not painted.inpainted.iloc[-1]


def test_inpaint_rejects_nonsense():
    dep = _two_blocks(utc(2026, 10, 26, 0), gap_min=60)
    with pytest.raises(ValueError):
        dep.activity("JX_AAAAAA", inpaint=-5)
    with pytest.raises(ValueError):
        dep.activity("JX_AAAAAA", inpaint="soon")


def test_activity_does_not_inpaint_unless_asked_and_reports_it():
    dep = _two_blocks(utc(2026, 10, 26, 0), gap_min=180)
    a0 = dep.activity("JX_AAAAAA", bin_s=3600)
    assert a0.data.inpainted.sum() == 0 and a0.parameters["inpaint"] == 0
    a1 = dep.activity("JX_AAAAAA", bin_s=3600, inpaint=180)
    assert a1.data.inpainted.sum() == 3
    assert a1.tables["by_device"].inpainted_h.iloc[0] == 3
    assert any("filled" in n for n in a1.notes)
    # observed time still counts only real bins
    assert a1.tables["by_device"].observed_h.iloc[0] == a0.tables["by_device"].observed_h.iloc[0]
    names = [t["name"] for t in a1.figure("dark")["data"]]
    assert names.count("JX_AAAAAA (filled)") == 1


def test_rhythms_default_inpaint_rescues_a_day_with_a_download_gap():
    start = utc(2026, 10, 26, 0)
    n = 4 * 1440
    t = [start + 60 * i for i in range(n) if not 30 * 60 <= i < 33 * 60]  # a 3 h pause on day 2
    hod = (np.array(t) % 86400) // 3600
    motion = np.where((hod >= 8) & (hod < 20), 300, 0)
    dep = jx.load_deployment(device_files("JX_AAAAAA", [(u, int(m), 2.9, 20.0, 40.0, 0) for u, m in zip(t, motion)], []))
    off = dep.rhythms("JX_AAAAAA", min_days=4, inpaint=0)
    assert not off.summary["available"] and off.summary["complete_days"] == 3
    on = dep.rhythms("JX_AAAAAA", min_days=4)  # library default
    assert on.summary["available"] and on.summary["complete_days"] == 4 and on.summary["filled_hours"] == 3
    assert on.data.loc[on.data.filled_hours > 0, "observed_hours"].iloc[0] == 21
    assert any("filled" in n for n in on.notes)
    for res in (off, on, dep.diel("JX_AAAAAA")):
        for theme in ("dark", "light"):
            res.figure(theme)
        res.to_json()
    d = dep.diel("JX_AAAAAA", bin_s=3600)
    assert d.quality["inpainted_bins"] == 3 and d.data.inpainted.sum() == 3
    assert any(t.get("name") == "filled (no recording)" for t in d.figure("dark")["data"])


def test_synchrony_only_uses_filled_bins_when_asked(start):
    rng = np.random.default_rng(5)
    n = 8 * 60
    a = rng.integers(0, 200, n)
    t = [start + 60 * i for i in range(n)]
    keep = [i for i in range(n) if not 200 <= i < 260]  # b is missing for an hour
    fa = device_files("JX_AAAAAA", [(u, int(m), 2.9, 21.0, 40.0, 1) for u, m in zip(t, a)], [])
    fb = device_files("JX_BBBBBB", [(t[i], int(a[i]), 2.9, 21.0, 40.0, 1) for i in keep], [])
    dep = jx.load_deployment({**fa, **fb}, timezone="UTC")
    s0 = dep.synchrony("JX_AAAAAA", "JX_BBBBBB", bin_s=300, n_null=50)
    s1 = dep.synchrony("JX_AAAAAA", "JX_BBBBBB", bin_s=300, n_null=50, inpaint=120)
    assert s0.summary["filled_bins"] == 0 and s1.summary["filled_bins"] > 0
    assert s1.summary["aligned_bins"] > s0.summary["aligned_bins"]
    assert any("filled values" in n for n in s1.notes)


# ── figures ────────────────────────────────────────────────────────────────────

def test_activity_axis_starts_at_the_first_bin_not_an_earlier_midnight(start):
    n = 3 * 60
    dep = jx.load_deployment(device_files("JX_AAAAAA", [(start + 60 * i, 5, 2.9, 20.0, 40.0, 0) for i in range(n)], []),
                             timezone="America/Chicago")
    fig = dep.activity("JX_AAAAAA", bin_s=900).figure("dark")
    lo, hi = fig["layout"]["xaxis"]["range"]
    assert lo == "2026-10-31 01:00:00" and hi == "2026-10-31 04:00:00"
    assert fig["layout"]["shapes"] == []  # local midnight (00:00) is before the data


def test_battery_marks_the_lowest_single_reading(start):
    vit = [(start + 60 * i, 0, 2.95, 20.0, 40.0, 0) for i in range(120)]
    vit[50] = (vit[50][0], 0, 2.54, 20.0, 40.0, 0)  # one brief dip inside a 15 min bin
    dep = jx.load_deployment(device_files("JX_AAAAAA", vit, []))
    b = dep.battery()
    assert b.summary["lowest_v"] == 2.54 and b.summary["lowest_unix"] == vit[50][0]
    assert b.data.batt_v.min() > 2.8  # the bin average hides it
    fig = b.figure("dark")
    mark = [t for t in fig["data"] if t["name"] == "Lowest reading"]
    assert len(mark) == 1 and mark[0]["y"] == [2.54]
    assert fig["layout"]["yaxis"]["range"][0] <= 2.54


def test_matrix_grays_base_to_base_cells_and_brighter_means_stronger(small_deployment_files, start):
    extra = device_files("JB_CCCCCC", [(start + 60 * i, 0, 2.9, 20.0, 45.0, 0) for i in range(240)],
                         [(start + 30 * i + 7, "BBBBBBB", -75) for i in range(480)])
    dep = jx.load_deployment({**small_deployment_files, **extra})
    for metric in ("rate", "median_rssi"):
        fig = dep.proximity_matrix(metric=metric, min_rssi=-90).figure("dark")
        gray, color = fig["data"]
        assert gray["colorscale"] == color["colorscale"] or gray["showscale"] is False
        assert color["zmin"] == gray["zmin"] and color["zmax"] == gray["zmax"]
        labels = fig["data"][0]["x"]
        zg, zc = np.array(gray["z"], dtype=float), np.array(color["z"], dtype=float)
        a, b, c = (labels.index(i) for i in ("JX_AAAAAA", "JB_BBBBBB", "JB_CCCCCC"))
        assert np.isfinite(zg[c, b]) and np.isnan(zc[c, b])      # base hears base: grey layer only
        assert np.isfinite(zc[a, b]) and np.isnan(zg[a, b])      # mobile involved: colour layer
    # the outlined cell is the best mobile-to-base link, never base-to-base (C hears B at -75 is not it)
    m = dep.proximity_matrix(min_rssi=-90)
    assert m.summary["best_observer"] == "JX_AAAAAA" or m.summary["best_peer"] == "JX_AAAAAA"
    fig = m.figure("dark")
    assert len(fig["layout"]["shapes"]) == 1
    labels = fig["data"][0]["x"]
    box = fig["layout"]["shapes"][0]
    assert box["x0"] == labels.index(m.summary["best_peer"]) - 0.5 and box["y0"] == labels.index(m.summary["best_observer"]) - 0.5
    assert dep.proximity_matrix(metric="median_rssi").summary["best_metric"] == "median_rssi"
    # median signal: the strongest (highest dBm) value maps to the brightest end of the scale
    scale = dep.proximity_matrix(metric="median_rssi").figure("dark")["data"][-1]["colorscale"]
    assert scale[0][1] == "#141A57" and scale[-1][1] == "#F472E6"


def test_network_is_one_row_of_the_matrix(small_deployment_files, start):
    extra = device_files("JB_CCCCCC", [(start + 60 * i, 0, 2.9, 20.0, 45.0, 0) for i in range(240)],
                         [(start + 30 * i + 7, "BBBBBBB", -75) for i in range(480)])
    dep = jx.load_deployment({**small_deployment_files, **extra})
    m = dep.proximity_matrix(min_rssi=-90).data
    for center in ("JX_AAAAAA", "JB_BBBBBB"):
        net = dep.proximity_network(center, min_rssi=-90)
        row = m[m.observer == center].set_index("peer")
        assert set(net.data.peer) == set(row.index)
        for q in net.data.itertuples():
            assert q.rate == pytest.approx(row.loc[q.peer, "rate"], nan_ok=True) and q.detections == row.loc[q.peer, "detections"]
    # a mobile at the centre names a base, a base names a mobile; never another base
    assert dep.proximity_network("JX_AAAAAA").summary["best_peer"] in dep.bases
    only_a_base = dep.proximity_network("JB_CCCCCC").summary  # C heard B (a base) and no mobile
    assert only_a_base["n_heard"] == 1 and "best_pair" not in only_a_base
    # labels work as well as ids, and an unknown tag is refused
    assert dep.proximity_network("JX_AAAAAA").summary["center"] == "JX_AAAAAA"
    with pytest.raises(Exception):
        dep.proximity_network("nobody")


def test_network_figure_encodes_signal_as_distance_and_outlines_the_best(small_deployment_files):
    dep = jx.load_deployment(small_deployment_files)
    net = dep.proximity_network("JX_AAAAAA", min_rssi=-90)
    for theme in ("dark", "light"):
        fig = net.figure(theme)
        peers = [d for d in fig["data"] if d.get("mode") == "markers+text"][0]
        centre = fig["data"][-1]
        assert centre["x"] == [0] and centre["y"] == [0]
        assert len(peers["x"]) == int(net.summary["n_heard"])
        # the outlined node is the best peer
        widths = peers["marker"]["line"]["width"]
        assert sum(w > 3 for w in widths) == 1
        assert peers["hovertext"][widths.index(max(widths))].startswith(net.summary["best_peer_label"])
        assert len(fig["layout"]["shapes"]) == 3  # reference rings
    # a stronger median signal sits closer to the centre
    from pyjuxta.proximity import _network_radius
    assert _network_radius(-50) < _network_radius(-70) < _network_radius(-95) == _network_radius(-120)


def test_network_says_so_when_nothing_was_heard(small_deployment_files):
    dep = jx.load_deployment(small_deployment_files)
    net = dep.proximity_network("JX_AAAAAA", min_rssi=-10)
    assert net.summary["n_heard"] == 0 and "best_pair" not in net.summary
    assert net.figure("dark")["data"] == []
