"""The v10 firmware contract: what the files mean, and what the loader must not invent."""

from __future__ import annotations

import numpy as np
import pandas as pd
import pytest

import pyjuxta as jx
from conftest import device_files, utc, zip_of


def test_sec_is_utc_seconds_after_midnight(small_deployment_files, start):
    dep = jx.load_deployment(small_deployment_files)
    v = dep.device("JX_AAAAAA").vitals
    assert (v.unix % 86400 == v.sec).all()
    assert v.unix.iloc[0] == start


def test_peer_ids_map_back_to_advertised_names():
    assert jx.peer_to_device_id("X54F7A1") == "JX_54F7A1"
    assert jx.peer_to_device_id("B5c0848") == "JB_5C0848"
    assert jx.peer_to_device_id("JX_54F7A1") == "JX_54F7A1"
    assert jx.peer_to_device_id("Q54F7A1") is None
    assert jx.peer_to_device_id("X54F7") is None


def test_empty_environment_cells_stay_missing(start):
    vit = [(start, 3, 2.9, None, None, None), (start + 60, 4, 2.9, 20.0, 40.0, 0)]
    dep = jx.load_deployment(device_files("JB_BBBBBB", vit, []))
    v = dep.device("JB_BBBBBB").vitals
    assert v.temp_c.isna().tolist() == [True, False]
    assert v.lux.isna().tolist() == [True, False]
    assert v.lux.iloc[1] == 0  # a real zero lux is kept


def test_header_only_files_are_fine(small_deployment_files):
    files = dict(small_deployment_files)
    files["JX_AAAAAA/JXV20261102.csv"] = b"sec,motion,batt_v,temp_c,humidity,lux\n"
    files["JX_AAAAAA/JXB20261102.csv"] = b"sec,peer_id,rssi\n"
    dep = jx.load_deployment(files)
    assert len(dep.device("JX_AAAAAA").vitals) == 240


def test_identity_comes_from_jxs_not_the_folder_name(start):
    files = device_files("JX_AAAAAA", [(start, 1, 2.9, 20.0, 40.0, 0)], [], folder="my copy (2)")
    dep = jx.load_deployment(files)
    assert list(dep.devices) == ["JX_AAAAAA"]
    assert dep.warnings == []


def test_folder_name_used_only_without_jxs(start):
    files = {"JB_0A3660/JXV20261031.csv": b"sec,motion,batt_v,temp_c,humidity,lux\n21600,0,2.90,20.0,40.0,0\n"}
    dep = jx.load_deployment(files)
    assert list(dep.devices) == ["JB_0A3660"]
    assert any("assumed" in w for w in dep.warnings)


def test_zip_of_device_zips_and_duplicates(small_deployment_files):
    mob = {k: v for k, v in small_deployment_files.items() if k.startswith("JX_")}
    base = {k: v for k, v in small_deployment_files.items() if k.startswith("JB_")}
    # iOS shares one zip per device; here the mobile is also copied in loose
    bundle = {"JX_AAAAAA.zip": zip_of(mob), "JB_BBBBBB.zip": zip_of(base), **{"copy/" + k: v for k, v in mob.items()}}
    dep = jx.load_deployment(bundle)
    assert sorted(dep.devices) == ["JB_BBBBBB", "JX_AAAAAA"]
    assert len(dep.device("JX_AAAAAA").vitals) == 240
    assert any("duplicate" in w for w in dep.warnings)


def test_unknown_peers_are_reported(small_deployment_files):
    files = dict(small_deployment_files)
    files["JX_AAAAAA/JXB20261031.csv"] += b"21700,XCCCCCC,-70\n"
    dep = jx.load_deployment(files)
    assert dep.unknown_peers == ["JX_CCCCCC"]
    assert any("no files provided" in w for w in dep.check())


def test_roles_and_labels(small_deployment_files):
    dep = jx.load_deployment(small_deployment_files)
    assert dep.mobiles == ["JX_AAAAAA"] and dep.bases == ["JB_BBBBBB"]
    dep.set_labels({"JX_AAAAAA": "Human"}).set_roles({"JB_BBBBBB": "mobile"})
    assert dep.device("human").id == "JX_AAAAAA"
    assert dep.mobiles == ["JB_BBBBBB", "JX_AAAAAA"] or set(dep.mobiles) == {"JB_BBBBBB", "JX_AAAAAA"}
    with pytest.raises(KeyError):
        dep.set_labels({"JX_ZZZZZZ": "nobody"})


def test_sessions_split_at_gaps(start):
    vit = [(start + 60 * i, 1, 2.9, 20.0, 40.0, 0) for i in range(10)]
    vit += [(start + 3600 + 60 * i, 1, 2.9, 20.0, 40.0, 0) for i in range(10)]
    dev = jx.load_deployment(device_files("JB_BBBBBB", vit, [])).device("JB_BBBBBB")
    s = dev.sessions()
    assert len(s) == 2 and s.n_samples.tolist() == [10, 10]
    assert dev.vitals_flagged().after_gap.sum() == 2


def test_result_is_json_safe(small_deployment_files):
    dep = jx.load_deployment(small_deployment_files, timezone="America/Chicago")
    for res in (dep.availability(), dep.battery(), dep.activity("JX_AAAAAA"), dep.environment("lux"),
                dep.proximity("JX_AAAAAA", "JB_BBBBBB"), dep.visits("JX_AAAAAA"), dep.proximity_matrix()):
        res.to_json()  # raises on NaN or non-JSON values
