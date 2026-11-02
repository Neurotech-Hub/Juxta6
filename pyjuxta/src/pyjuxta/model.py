"""Deployment and Device: the in-memory form of a set of Juxta daily logs."""

from __future__ import annotations

from collections import Counter
from functools import cached_property
from typing import Iterable, Mapping

import numpy as np
import pandas as pd

from ._time import check_tz, utc_index
from .io import _ID_RE

DEFAULT_SCAN_S = 30
DEFAULT_ADV_S = 2
DEFAULT_VITALS_S = 60

# Mobiles get warm hues and bases cool ones, so a figure reads at a glance.
_MOBILE_COLORS = ["#F472E6", "#FBBF24", "#FB7185", "#F97316", "#FDE047", "#F0ABFC"]
_BASE_COLORS = ["#60A5FA", "#A78BFA", "#34D399", "#22D3EE", "#818CF8", "#2DD4BF", "#93C5FD", "#C4B5FD"]


def role_from_id(device_id: str) -> str:
    """``JX_`` is a mobile tag, ``JB_`` a fixed base station."""
    return "base" if device_id.startswith("JB_") else "mobile"


class Device:
    """One logger's vitals (JXV), peer sightings (JXB) and events (JXS), all on UTC unix seconds."""

    def __init__(self, device_id: str, vitals: pd.DataFrame, peers: pd.DataFrame, events: pd.DataFrame):
        self.id = device_id
        self.vitals = vitals
        self.peers = peers
        self.events = events
        self.deployment: "Deployment | None" = None

    # identity ----------------------------------------------------------------
    @property
    def role(self) -> str:
        if self.deployment is not None:
            return self.deployment.role(self.id)
        return role_from_id(self.id)

    @property
    def label(self) -> str:
        return self.deployment.label(self.id) if self.deployment is not None else self.id

    # settings ----------------------------------------------------------------
    def _setting(self, col: str, default: int) -> int:
        ev = self.events
        if col in ev.columns and len(ev):
            vals = ev[col].dropna()
            vals = vals[vals > 0]
            if len(vals):
                return int(vals.iloc[-1])
        return default

    @property
    def scan_interval_s(self) -> int:
        """Seconds between ~1 s scan bursts, from the last JXS settings row (0 means scanning was off)."""
        ev = self.events
        if "scan_interval_s" in ev.columns and ev["scan_interval_s"].notna().any():
            return int(ev["scan_interval_s"].dropna().iloc[-1])
        return DEFAULT_SCAN_S

    @property
    def adv_interval_s(self) -> int:
        return self._setting("adv_interval_s", DEFAULT_ADV_S)

    @property
    def vitals_interval_s(self) -> int:
        return self._setting("vitals_interval_s", DEFAULT_VITALS_S)

    def settings_changes(self) -> pd.DataFrame:
        """Distinct (scan, adv, vitals) settings seen in JXS, with the first time each appeared."""
        cols = ["scan_interval_s", "adv_interval_s", "vitals_interval_s"]
        if not len(self.events):
            return pd.DataFrame(columns=["unix", *cols])
        ev = self.events[["unix", *cols]].dropna()
        return ev.loc[~ev[cols].duplicated()].reset_index(drop=True)

    # time ---------------------------------------------------------------------
    @property
    def span(self) -> tuple[int, int] | None:
        ts = []
        if len(self.vitals):
            ts += [int(self.vitals.unix.min()), int(self.vitals.unix.max())]
        if len(self.peers):
            ts += [int(self.peers.unix.min()), int(self.peers.unix.max())]
        return (min(ts), max(ts)) if ts else None

    def max_gap_s(self, max_gap_s: float | None = None) -> float:
        """Largest silence still counted as one recording session (default 3 vitals intervals, at least 3 min)."""
        return float(max_gap_s) if max_gap_s is not None else float(max(180, 3 * self.vitals_interval_s))

    def sessions(self, max_gap_s: float | None = None) -> pd.DataFrame:
        """Contiguous stretches with vitals rows. A gap longer than ``max_gap_s`` ends a session.

        Between sessions the tag was on the shelf, off, out of memory, or rebooting; that time is
        *missing*, not quiet.
        """
        cols = ["start", "end", "duration_s", "n_samples"]
        t = self.vitals.unix.to_numpy()
        if len(t) == 0:
            return pd.DataFrame({c: pd.Series(dtype="int64") for c in cols})
        gap = self.max_gap_s(max_gap_s)
        cut = np.where(np.diff(t) > gap)[0]
        first = np.r_[0, cut + 1]
        last = np.r_[cut, len(t) - 1]
        return pd.DataFrame({
            "start": t[first], "end": t[last], "duration_s": t[last] - t[first],
            "n_samples": last - first + 1,
        })

    def vitals_flagged(self, max_gap_s: float | None = None) -> pd.DataFrame:
        """Vitals plus ``dt_s`` (seconds since the previous row) and ``after_gap``.

        ``after_gap`` marks the first row of each session. The firmware clears its motion counter only
        when it writes a vitals row, so that first row also counts motion since boot or the last sync.
        """
        v = self.vitals.copy()
        gap = self.max_gap_s(max_gap_s)
        dt = v.unix.diff()
        v["dt_s"] = dt.to_numpy()
        v["after_gap"] = ((dt > gap) | dt.isna()).to_numpy()
        return v

    def expected_scans(self, windows: pd.DataFrame | None = None) -> float:
        """Scan bursts the tag should have run inside ``windows`` (default: its own sessions)."""
        s = self.scan_interval_s
        if s <= 0:
            return 0.0
        w = self.sessions() if windows is None else windows
        if not len(w):
            return 0.0
        dur = (w["end"] - w["start"]).to_numpy(dtype=float)
        return float(dur.sum() / s)

    def __repr__(self) -> str:
        return (f"Device({self.id}, role={self.role}, vitals={len(self.vitals)}, "
                f"peers={len(self.peers)}, events={len(self.events)})")


class Deployment:
    """A set of devices recorded together."""

    def __init__(self, devices: Mapping[str, Device], warnings: Iterable[str] = (),
                 timezone: str = "UTC", info: dict | None = None):
        self.devices: dict[str, Device] = dict(sorted(devices.items()))
        for d in self.devices.values():
            d.deployment = self
        self.warnings: list[str] = list(warnings)
        self.info = info or {}
        self.tz = check_tz(timezone)
        self._labels: dict[str, str] = {}
        self._roles: dict[str, str] = {}

    @classmethod
    def from_frames(cls, frames: Mapping[str, Mapping[str, pd.DataFrame]], warnings=(), timezone="UTC", info=None):
        devices = {k: Device(k, v["vitals"], v["peers"], v["events"]) for k, v in frames.items()}
        return cls(devices, warnings, timezone, info)

    # configuration ------------------------------------------------------------
    def set_timezone(self, tz: str) -> "Deployment":
        """IANA timezone for local-clock analyses. Stored times stay UTC."""
        self.tz = check_tz(tz)
        return self

    def set_labels(self, labels: Mapping[str, str]) -> "Deployment":
        """Human names for devices, e.g. ``{"JX_54F7A1": "Human"}``. An empty name removes the label."""
        for k, v in labels.items():
            key = self._resolve(k)
            if key is None:
                raise KeyError(f"No device {k!r} in this deployment")
            name = "" if v is None else str(v).strip()
            if name:
                self._labels[key] = name
            else:
                self._labels.pop(key, None)
        return self

    def set_roles(self, roles: Mapping[str, str]) -> "Deployment":
        """Override a role (``"mobile"`` or ``"base"``) when a tag was configured the other way."""
        for k, v in roles.items():
            if v not in ("mobile", "base"):
                raise ValueError("role must be 'mobile' or 'base'")
            key = self._resolve(k)
            if key is None:
                raise KeyError(f"No device {k!r} in this deployment")
            self._roles[key] = v
        return self

    def label(self, device_id: str) -> str:
        return self._labels.get(device_id, device_id)

    def role(self, device_id: str) -> str:
        return self._roles.get(device_id, role_from_id(device_id))

    def color(self, device_id: str) -> str:
        """Stable per-device color: warm for mobiles, cool for bases."""
        role = self.role(device_id)
        same = [i for i in self.devices if self.role(i) == role]
        i = same.index(device_id) if device_id in same else 0
        pal = _MOBILE_COLORS if role == "mobile" else _BASE_COLORS
        return pal[i % len(pal)]

    # lookup --------------------------------------------------------------------
    def _resolve(self, key: str) -> str | None:
        if key in self.devices:
            return key
        for dev_id, name in self._labels.items():
            if name.lower() == str(key).lower():
                return dev_id
        up = str(key).upper()
        if up in self.devices:
            return up
        for dev_id in self.devices:
            if dev_id.endswith(up) and len(up) >= 6:
                return dev_id
        return None

    def device(self, key: str) -> Device:
        """Find a device by id (``JX_54F7A1``), label, or six-hex suffix."""
        found = self._resolve(key)
        if found is None:
            raise KeyError(f"No device {key!r}. Devices: {', '.join(self.devices)}")
        return self.devices[found]

    def resolve_ids(self, keys) -> list[str]:
        if keys is None:
            return list(self.devices)
        if isinstance(keys, str):
            keys = [keys]
        return [self.device(k).id for k in keys]

    @property
    def mobiles(self) -> list[str]:
        return [i for i in self.devices if self.role(i) == "mobile"]

    @property
    def bases(self) -> list[str]:
        return [i for i in self.devices if self.role(i) == "base"]

    def __len__(self) -> int:
        return len(self.devices)

    def __repr__(self) -> str:
        return f"Deployment({len(self.devices)} devices: {', '.join(self.devices)})"

    # joined data ----------------------------------------------------------------
    @cached_property
    def detections(self) -> pd.DataFrame:
        """Every logged sighting: ``unix, observer, peer, rssi`` (observer heard peer)."""
        parts = []
        for dev_id, dev in self.devices.items():
            if len(dev.peers):
                p = dev.peers[["unix", "peer", "rssi"]].copy()
                p.insert(1, "observer", dev_id)
                parts.append(p)
        if not parts:
            return pd.DataFrame({"unix": pd.Series(dtype="int64"), "observer": pd.Series(dtype="object"),
                                 "peer": pd.Series(dtype="object"), "rssi": pd.Series(dtype="int64")})
        return pd.concat(parts, ignore_index=True).sort_values("unix", kind="stable").reset_index(drop=True)

    @property
    def unknown_peers(self) -> list[str]:
        """Peer ids that were heard but have no files in this deployment."""
        seen = set(self.detections.peer.unique()) if len(self.detections) else set()
        return sorted(seen - set(self.devices))

    @property
    def span(self) -> tuple[int, int] | None:
        spans = [d.span for d in self.devices.values() if d.span]
        return (min(s[0] for s in spans), max(s[1] for s in spans)) if spans else None

    def summary(self) -> pd.DataFrame:
        """One row per device: role, recording span, how much of it has data, and settings."""
        rows = []
        for dev_id, d in self.devices.items():
            s = d.sessions()
            span = d.span
            span_s = (span[1] - span[0]) if span else 0
            observed_s = float(s.duration_s.sum()) if len(s) else 0.0
            ev = d.events
            n_sync = int((ev.event == "time_set").sum()) if len(ev) else 0
            v = d.vitals
            rows.append({
                "device_id": dev_id,
                "label": self.label(dev_id),
                "role": self.role(dev_id),
                "first_utc": utc_index([span[0]])[0] if span else pd.NaT,
                "last_utc": utc_index([span[1]])[0] if span else pd.NaT,
                "span_h": round(span_s / 3600, 2),
                "recorded_h": round(observed_s / 3600, 2),
                "coverage": round(observed_s / span_s, 3) if span_s else np.nan,
                "sessions": int(len(s)),
                "syncs": n_sync,
                "vitals_rows": int(len(v)),
                "peer_rows": int(len(d.peers)),
                "scan_interval_s": d.scan_interval_s,
                "adv_interval_s": d.adv_interval_s,
                "vitals_interval_s": d.vitals_interval_s,
                "fw_version": str(ev.fw_version.iloc[-1]) if len(ev) and "fw_version" in ev else "",
                "batt_start_v": float(v.batt_v.iloc[0]) if len(v) else np.nan,
                "batt_end_v": float(v.batt_v.iloc[-1]) if len(v) else np.nan,
            })
        return pd.DataFrame(rows)

    def check(self) -> list[str]:
        """Plain-language data-quality findings (also available as ``warnings``)."""
        out = list(self.warnings)
        for dev_id, d in self.devices.items():
            if not len(d.vitals):
                out.append(f"{dev_id}: no vitals rows; activity and coverage cannot be computed.")
            if d.scan_interval_s == 0:
                out.append(f"{dev_id}: scanning was off (scan_interval_s = 0), so it logged no peers.")
            ch = d.settings_changes()
            if len(ch) > 1:
                out.append(f"{dev_id}: scan/advertise/vitals settings changed {len(ch) - 1} time(s); rates are approximate across changes.")
            if not _ID_RE.match(dev_id):
                out.append(f"{dev_id}: not a JX_/JB_ identity.")
        unknown = self.unknown_peers
        if unknown:
            out.append("Heard but no files provided for: " + ", ".join(unknown) + ".")
        return out

    # analyses (implemented in their own modules) -----------------------------
    def availability(self, **kw):
        from .health import availability
        return availability(self, **kw)

    def battery(self, **kw):
        from .health import battery
        return battery(self, **kw)

    def activity(self, devices=None, **kw):
        from .activity import activity
        return activity(self, devices, **kw)

    def diel(self, device, **kw):
        from .activity import diel
        return diel(self, device, **kw)

    def rhythms(self, device, **kw):
        from .activity import rhythms
        return rhythms(self, device, **kw)

    def environment(self, variable="temp_c", devices=None, **kw):
        from .environment import environment
        return environment(self, variable, devices, **kw)

    def visits(self, mobile, **kw):
        from .proximity import visits
        return visits(self, mobile, **kw)

    def proximity(self, a, b, **kw):
        from .proximity import proximity
        return proximity(self, a, b, **kw)

    def proximity_matrix(self, **kw):
        from .proximity import proximity_matrix
        return proximity_matrix(self, **kw)

    def proximity_network(self, center, **kw):
        from .proximity import proximity_network
        return proximity_network(self, center, **kw)

    def synchrony(self, a, b, **kw):
        from .synchrony import synchrony
        return synchrony(self, a, b, **kw)
