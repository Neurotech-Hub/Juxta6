"""Read Juxta6 daily CSV logs (``jxta-nor-csv-v10``) into a :class:`~pyjuxta.Deployment`.

Time contract (checked against ``juxta6-0-prod`` firmware 6.6.0):

* ``JXV.sec`` and ``JXB.sec`` are seconds after **UTC** midnight (``unix % 86400``).
  The UTC date is in the filename.
* ``JXS.unix`` is absolute UTC seconds.
* ``JXB.peer_id`` is ``X`` or ``B`` plus six hex digits for an advertised ``JX_`` / ``JB_`` name.

Device identity comes from the ``device_id`` column of the JXS file in the same folder, never
from the folder name alone (the folder name is used only when a folder has no JXS file).
"""

from __future__ import annotations

import calendar
import io as _io
import posixpath
import re
import zipfile
from collections import Counter
from os import PathLike
from pathlib import Path
from typing import Mapping

import numpy as np
import pandas as pd

SCHEMA = "jxta-nor-csv-v10"

_FILE_RE = re.compile(r"^(JXS|JXV|JXB)(\d{8})\.csv$", re.IGNORECASE)
_ID_RE = re.compile(r"^J[XB]_[0-9A-F]{6}$")
_ID_IN_PATH = re.compile(r"J[XB]_[0-9A-Fa-f]{6}")
_PEER_RE = re.compile(r"^([XB])([0-9A-Fa-f]{6})$")

JXV_COLUMNS = ["sec", "motion", "batt_v", "temp_c", "humidity", "lux"]
JXB_COLUMNS = ["sec", "peer_id", "rssi"]
JXS_COLUMNS = [
    "unix", "event", "device_id", "subject_id", "experiment", "fw_version",
    "scan_interval_s", "adv_interval_s", "vitals_interval_s", "ble_name", "latitude", "longitude",
]

_REQUIRED = {"JXV": ["sec", "motion", "batt_v"], "JXB": JXB_COLUMNS, "JXS": ["unix", "event", "device_id"]}


def peer_to_device_id(peer_id: str) -> str | None:
    """``XABCDEF`` -> ``JX_ABCDEF`` and ``BABCDEF`` -> ``JB_ABCDEF``. ``None`` if not a Juxta peer id."""
    s = str(peer_id).strip()
    if _ID_RE.match(s.upper()):
        return s.upper()
    m = _PEER_RE.match(s)
    if m is None:
        return None
    return f"J{m.group(1).upper()}_{m.group(2).upper()}"


# ── Collecting files ──────────────────────────────────────────────────────────

def _norm(name: str) -> str:
    return name.replace("\\", "/").lstrip("/")


def _skip(name: str) -> bool:
    parts = _norm(name).split("/")
    return "__MACOSX" in parts or parts[-1].startswith(".") or parts[-1] == ""


def _add_zip(files: dict[str, bytes], prefix: str, data: bytes) -> None:
    with zipfile.ZipFile(_io.BytesIO(data)) as zf:
        for info in zf.infolist():
            if info.is_dir() or _skip(info.filename):
                continue
            name = _norm(posixpath.join(prefix, info.filename)) if prefix else _norm(info.filename)
            blob = zf.read(info)
            if info.filename.lower().endswith(".zip"):
                _add_zip(files, name[:-4], blob)
            else:
                files[name] = blob


def _add_path(files: dict[str, bytes], path: Path, root: Path | None) -> None:
    if path.is_dir():
        for child in sorted(path.rglob("*")):
            if child.is_file() and not _skip(str(child)):
                _add_path(files, child, path.parent)
        return
    rel = _norm(str(path.relative_to(root))) if root is not None else path.name
    data = path.read_bytes()
    if path.suffix.lower() == ".zip":
        _add_zip(files, rel[:-4], data)
    else:
        files[rel] = data


def collect_files(source) -> dict[str, bytes]:
    """Normalise ``source`` into ``{relative path: bytes}``.

    ``source`` may be a folder, a ``.zip`` (including a zip of per-device zips), a single CSV,
    a list of those, or a mapping of ``{path: bytes | str}`` (used by the browser walkthrough).
    """
    files: dict[str, bytes] = {}
    if isinstance(source, Mapping):
        for name, blob in source.items():
            if _skip(name):
                continue
            data = blob.encode("utf-8") if isinstance(blob, str) else bytes(blob)
            if str(name).lower().endswith(".zip"):
                _add_zip(files, _norm(str(name))[:-4], data)
            else:
                files[_norm(str(name))] = data
    elif isinstance(source, (str, PathLike)):
        _add_path(files, Path(source).expanduser(), None)
    elif isinstance(source, (list, tuple)):
        for item in source:
            files.update(collect_files(item))
    else:
        raise TypeError(f"Cannot read Juxta logs from {type(source).__name__}")
    return files


# ── Parsing ───────────────────────────────────────────────────────────────────

def _read_csv(blob: bytes) -> pd.DataFrame:
    text = blob.decode("utf-8-sig", errors="replace")
    if not text.strip():
        return pd.DataFrame()
    try:
        df = pd.read_csv(_io.StringIO(text), dtype=str, keep_default_na=False, comment="#",
                         skip_blank_lines=True)
    except pd.errors.EmptyDataError:
        return pd.DataFrame()
    df.columns = [str(c).strip() for c in df.columns]
    return df


def _num(df: pd.DataFrame, col: str) -> pd.Series:
    if col not in df.columns:
        return pd.Series(np.nan, index=df.index, dtype=float)
    return pd.to_numeric(df[col].str.strip(), errors="coerce")


def _midnight_unix(ymd: str) -> int:
    return calendar.timegm((int(ymd[:4]), int(ymd[4:6]), int(ymd[6:8]), 0, 0, 0))


def _parse_vitals(df: pd.DataFrame, ymd: str, source: str) -> pd.DataFrame:
    sec = _num(df, "sec")
    ok = sec.notna() & (sec >= 0) & (sec < 86400)
    sec = sec[ok]
    out = pd.DataFrame({
        "unix": (_midnight_unix(ymd) + sec.to_numpy()).astype("int64"),
        "sec": sec.to_numpy().astype("int64"),
        "motion": _num(df, "motion")[ok].to_numpy(),
        "batt_v": _num(df, "batt_v")[ok].to_numpy(),
        "temp_c": _num(df, "temp_c")[ok].to_numpy(),
        "humidity": _num(df, "humidity")[ok].to_numpy(),
        "lux": _num(df, "lux")[ok].to_numpy(),
    })
    out["source"] = source
    return out


def _parse_peers(df: pd.DataFrame, ymd: str, source: str, unknown: Counter) -> pd.DataFrame:
    sec = _num(df, "sec")
    rssi = _num(df, "rssi")
    peer = df["peer_id"].str.strip().map(peer_to_device_id) if "peer_id" in df.columns else None
    ok = sec.notna() & (sec >= 0) & (sec < 86400) & rssi.notna()
    if peer is not None:
        for raw in df.loc[ok & peer.isna(), "peer_id"]:
            unknown[raw] += 1
        ok = ok & peer.notna()
    out = pd.DataFrame({
        "unix": (_midnight_unix(ymd) + sec[ok].to_numpy()).astype("int64"),
        "sec": sec[ok].to_numpy().astype("int64"),
        "peer": peer[ok].to_numpy() if peer is not None else [],
        "rssi": rssi[ok].to_numpy().astype("int64"),
    })
    out["source"] = source
    return out


def _parse_events(df: pd.DataFrame, source: str) -> pd.DataFrame:
    unix = _num(df, "unix")
    ok = unix.notna()
    out = pd.DataFrame({"unix": unix[ok].to_numpy().astype("int64")})
    for col in ("event", "device_id", "subject_id", "experiment", "fw_version", "ble_name"):
        out[col] = df[col].str.strip()[ok].to_numpy() if col in df.columns else ""
    for col in ("scan_interval_s", "adv_interval_s", "vitals_interval_s", "latitude", "longitude"):
        out[col] = _num(df, col)[ok].to_numpy()
    out["source"] = source
    return out


def _empty_vitals() -> pd.DataFrame:
    return pd.DataFrame({c: pd.Series(dtype="float64") for c in JXV_COLUMNS[1:]}).assign(
        unix=pd.Series(dtype="int64"), sec=pd.Series(dtype="int64"), source=pd.Series(dtype="object"))


def _device_id_for_group(group: str, events: pd.DataFrame, warnings: list[str]) -> str | None:
    ids = Counter()
    for col in ("device_id", "ble_name"):
        if col in events.columns:
            for v in events[col]:
                v = str(v).strip().upper()
                if _ID_RE.match(v):
                    ids[v] += 1
    if len(ids) > 1:
        top = [i for i, _ in ids.most_common(2)]
        warnings.append(
            f"Folder '{group or '.'}' has JXS rows for more than one device ({', '.join(sorted(ids))}); "
            f"using {top[0]}."
        )
    if ids:
        return ids.most_common(1)[0][0]
    m = _ID_IN_PATH.findall(group)
    if m:
        dev = m[-1].upper()
        warnings.append(f"Folder '{group}' has no JXS identity; assumed {dev} from the folder name.")
        return dev
    return None


def load_frames(source) -> tuple[dict[str, dict[str, pd.DataFrame]], list[str], dict]:
    """Parse ``source`` into ``{device_id: {"vitals", "peers", "events"}}`` plus warnings and info."""
    files = collect_files(source)
    warnings: list[str] = []
    groups: dict[str, dict[str, list[tuple[str, str, str, bytes]]]] = {}
    ignored = 0
    for name, blob in files.items():
        base = posixpath.basename(name)
        m = _FILE_RE.match(base)
        if m is None:
            ignored += 1
            continue
        kind, ymd = m.group(1).upper(), m.group(2)
        groups.setdefault(posixpath.dirname(name), {"JXS": [], "JXV": [], "JXB": []})[kind].append(
            (ymd, base, name, blob))

    out: dict[str, dict[str, list[pd.DataFrame]]] = {}
    unknown_peers: Counter = Counter()
    bad_files = 0
    for group, kinds in sorted(groups.items()):
        events = []
        for ymd, base, _name, blob in sorted(kinds["JXS"]):
            df = _read_csv(blob)
            if df.empty:
                continue
            if any(c not in df.columns for c in _REQUIRED["JXS"]):
                warnings.append(f"{base}: unexpected JXS columns; skipped.")
                bad_files += 1
                continue
            events.append(_parse_events(df, base))
        ev = pd.concat(events, ignore_index=True) if events else pd.DataFrame(columns=JXS_COLUMNS)
        device = _device_id_for_group(group, ev, warnings)
        if device is None:
            warnings.append(f"Could not identify the device for folder '{group or '.'}'; its files were skipped.")
            continue
        slot = out.setdefault(device, {"vitals": [], "peers": [], "events": []})
        if len(ev):
            slot["events"].append(ev)
        for ymd, base, _name, blob in sorted(kinds["JXV"]):
            df = _read_csv(blob)
            if df.empty:
                continue
            if any(c not in df.columns for c in _REQUIRED["JXV"]):
                warnings.append(f"{base}: unexpected JXV columns; skipped.")
                bad_files += 1
                continue
            slot["vitals"].append(_parse_vitals(df, ymd, base))
        for ymd, base, _name, blob in sorted(kinds["JXB"]):
            df = _read_csv(blob)
            if df.empty:
                continue
            if any(c not in df.columns for c in _REQUIRED["JXB"]):
                warnings.append(f"{base}: unexpected JXB columns; skipped.")
                bad_files += 1
                continue
            slot["peers"].append(_parse_peers(df, ymd, base, unknown_peers))

    frames: dict[str, dict[str, pd.DataFrame]] = {}
    for device, slot in out.items():
        vitals = pd.concat(slot["vitals"], ignore_index=True) if slot["vitals"] else _empty_vitals()
        peers = (pd.concat(slot["peers"], ignore_index=True) if slot["peers"] else
                 pd.DataFrame({"unix": pd.Series(dtype="int64"), "sec": pd.Series(dtype="int64"),
                               "peer": pd.Series(dtype="object"), "rssi": pd.Series(dtype="int64"),
                               "source": pd.Series(dtype="object")}))
        events = (pd.concat(slot["events"], ignore_index=True) if slot["events"] else
                  pd.DataFrame(columns=JXS_COLUMNS + ["source"]))
        n_v, n_b, n_s = len(vitals), len(peers), len(events)
        vitals = vitals.drop_duplicates(subset=[c for c in vitals.columns if c != "source"])
        peers = peers.drop_duplicates(subset=["unix", "peer", "rssi"])
        events = events.drop_duplicates(subset=[c for c in events.columns if c != "source"])
        dropped = (n_v - len(vitals)) + (n_b - len(peers)) + (n_s - len(events))
        if dropped:
            warnings.append(f"{device}: dropped {dropped} duplicate rows (the same day was provided more than once).")
        frames[device] = {
            "vitals": vitals.sort_values("unix", kind="stable").reset_index(drop=True),
            "peers": peers.sort_values(["unix", "peer"], kind="stable").reset_index(drop=True),
            "events": events.sort_values("unix", kind="stable").reset_index(drop=True),
        }
    if unknown_peers:
        shown = ", ".join(f"{k} ({v})" for k, v in unknown_peers.most_common(3))
        warnings.append(f"{sum(unknown_peers.values())} JXB rows had peer ids that are not Juxta names: {shown}.")
    info = {"files_read": len(files) - ignored, "files_ignored": ignored, "files_skipped": bad_files}
    return frames, warnings, info


def load_deployment(source, *, timezone: str = "UTC", labels: Mapping[str, str] | None = None):
    """Load every device found in ``source`` and return a :class:`~pyjuxta.Deployment`.

    Parameters
    ----------
    source
        Folder of device folders, a ``.zip``, a folder of ``DEVICE_ID.zip`` files (what the iOS app
        shares), a list of those, or ``{path: bytes}``.
    timezone
        IANA name used for local-clock analyses (diel profiles, axis labels). Raw times stay UTC.
    labels
        Optional ``{device_id: "Name"}``. Use :meth:`Deployment.set_labels` later to change them.
    """
    from .model import Deployment  # local import: model imports this module's helpers

    frames, warnings, info = load_frames(source)
    dep = Deployment.from_frames(frames, warnings=warnings, timezone=timezone, info=info)
    if labels:
        dep.set_labels(labels)
    return dep
