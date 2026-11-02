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
