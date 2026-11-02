"""Synthetic Juxta logs in the real ``jxta-nor-csv-v10`` layout."""

from __future__ import annotations

import calendar
import io
import zipfile

import numpy as np
import pytest

JXS_HEADER = ("unix,event,device_id,subject_id,experiment,fw_version,scan_interval_s,adv_interval_s,"
              "vitals_interval_s,ble_name,latitude,longitude\n")


def utc(y, mo, d, h=0, mi=0, s=0) -> int:
    return calendar.timegm((y, mo, d, h, mi, s))


def device_files(dev: str, vitals: list[tuple], peers: list[tuple], events: list[tuple] | None = None,
                 folder: str | None = None) -> dict[str, bytes]:
    """``vitals``: (unix, motion, batt_v, temp, hum, lux); ``peers``: (unix, peer_id, rssi);
    ``events``: (unix, event, lat, lon). Rows are split into per-UTC-day files like the firmware does."""
    folder = folder or dev
    out: dict[str, str] = {}

    def day(unix: int) -> str:
        t = np.datetime64(unix, "s").astype("datetime64[D]").astype(str)
        return t.replace("-", "")

    for u, motion, batt, temp, hum, lux in vitals:
        name = f"{folder}/JXV{day(u)}.csv"
        out.setdefault(name, "sec,motion,batt_v,temp_c,humidity,lux\n")
        t = "" if temp is None else f"{temp:.1f}"
        h = "" if hum is None else f"{hum:.1f}"
        lx = "" if lux is None else f"{lux}"
        out[name] += f"{u % 86400},{motion},{batt:.2f},{t},{h},{lx}\n"
    for u, peer, rssi in peers:
        name = f"{folder}/JXB{day(u)}.csv"
        out.setdefault(name, "sec,peer_id,rssi\n")
        out[name] += f"{u % 86400},{peer},{rssi}\n"
    events = events or [(min(v[0] for v in vitals), "day_start", None, None)]
    for u, ev, lat, lon in events:
        name = f"{folder}/JXS{day(u)}.csv"
        out.setdefault(name, JXS_HEADER)
        loc = "" if lat is None else f"{lat:.6f}"
        lo = "" if lon is None else f"{lon:.6f}"
        out[name] += f"{u},{ev},{dev},{dev},,6.6.0,30,2,60,{dev},{loc},{lo}\n"
    return {k: v.encode() for k, v in out.items()}


def zip_of(files: dict[str, bytes]) -> bytes:
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w") as zf:
        for k, v in files.items():
            zf.writestr(k, v)
    return buf.getvalue()


@pytest.fixture
def start() -> int:
    return utc(2026, 10, 31, 6, 0, 0)  # 01:00 CDT on 31 Oct


@pytest.fixture
def small_deployment_files(start) -> dict[str, bytes]:
    """One mobile (JX_AAAAAA) that heard one base (JB_BBBBBB) every scan, base heard the mobile back."""
    t = [start + 60 * i for i in range(240)]  # four hours of vitals
    scans = [start + 30 * i for i in range(480)]  # one scan burst every 30 s
    rng = np.random.default_rng(1)
    vitals = [(u, int(rng.integers(0, 200)), 2.95, 21.5, 40.0, 3) for u in t]
    mob = device_files("JX_AAAAAA", vitals, [(u, "BBBBBBB", -60) for u in scans])
    base = device_files("JB_BBBBBB", [(u, 0, 2.9, 20.0, 45.0, 0) for u in t],
                        [(u + 5, "XAAAAAA", -62) for u in scans])
    return {**mob, **base}
