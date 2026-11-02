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
                dep.proximity_matrix(), dep.synchrony("Human", "Dog", n_null=100)):
        for theme in ("dark", "light"):
            fig = res.figure(theme)
            assert "data" in fig and "layout" in fig
