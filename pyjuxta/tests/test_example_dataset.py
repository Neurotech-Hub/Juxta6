"""The checked-in example deployment (docs/utilities/pyjuxta/data). Skipped when it is not present."""

from __future__ import annotations

from pathlib import Path

import pytest

import pyjuxta as jx

DATA = Path(__file__).resolve().parents[2] / "docs" / "utilities" / "pyjuxta" / "data"
pytestmark = pytest.mark.skipif(not DATA.is_dir(), reason="example data not in this checkout")


@pytest.fixture(scope="module")
def dep():
    d = jx.load_deployment(DATA, timezone="America/Chicago")
    d.set_labels({"JX_54F7A1": "Human", "JX_5C0848": "Dog", "JB_83FA0F": "Garage", "JB_6A9D3A": "Bedroom",
                  "JB_24563F": "Work desk", "JB_0A3660": "Kitchen"})
    return d


def test_six_devices_two_mobile(dep):
    assert sorted(dep.mobiles) == ["JX_54F7A1", "JX_5C0848"]
    assert len(dep.bases) == 4
    assert dep.warnings == [] and dep.unknown_peers == []


def test_human_was_off_for_part_of_the_time(dep):
    s = dep.summary().set_index("device_id")
    assert s.loc["JX_54F7A1", "sessions"] > 1
    assert s.loc["JX_54F7A1", "coverage"] < 0.95
    assert s.loc["JX_5C0848", "sessions"] == 1


def test_dog_is_a_mobile_everywhere(dep):
    # the dog tag was configured as a base during the deployment; the shipped copy is rewritten to mobile
    assert "JB_5C0848" not in dep.detections.peer.unique()
    assert "JX_5C0848" in dep.detections.peer.unique()


def test_rhythms_gate_on_the_example(dep):
    assert not dep.rhythms("Dog").summary["available"]


def test_every_figure_builds(dep):
    for res in (dep.availability(), dep.battery(), dep.activity(), dep.diel("Human"), dep.rhythms("Dog"),
                dep.environment("temp_c"), dep.visits("Human"), dep.proximity("Human", "Dog"),
                dep.proximity_matrix(), dep.proximity_network("Dog"), dep.synchrony("Human", "Dog", n_null=100)):
        for theme in ("dark", "light"):
            fig = res.figure(theme)
            assert "data" in fig and "layout" in fig


def test_default_inpaint_closes_the_humans_download_gap(dep):
    # 31 Oct has a ~3 h pause in the Human's log. Without painting it is 21 of 24 hours; with the default it is whole.
    plain = dep.rhythms("Human", min_days=1, inpaint=0)
    painted = dep.rhythms("Human", min_days=1)
    assert plain.summary["complete_days"] == 0 and painted.summary["complete_days"] == 1
    assert painted.summary["filled_hours"] > 0
    # the start and end of the recording are never extended, so only one local day can be complete
    assert not dep.rhythms("Human").summary["available"]


def test_battery_lowest_reading_is_on_the_figure(dep):
    b = dep.battery()
    assert b.summary["lowest_v"] == 2.54
    assert any(t["name"] == "Lowest reading" for t in b.figure("dark")["data"])


def test_dog_kitchen_is_the_best_link_in_both_views(dep):
    m, n = dep.proximity_matrix(min_rssi=-90).summary, dep.proximity_network("Dog", min_rssi=-90).summary
    assert m["best_pair"] == n["best_pair"] == "Dog hears Kitchen"
    assert m["best_value"] == n["best_value"]
