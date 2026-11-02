"""Time helpers. All analysis runs on integer UTC unix seconds; local time is derived."""

from __future__ import annotations

import numpy as np
import pandas as pd

DAY_S = 86400


def check_tz(tz: str) -> str:
    """Return ``tz`` if pandas can resolve it, otherwise raise a readable error."""
    try:
        pd.Timestamp("2026-01-01", tz=tz)
    except Exception as exc:  # zoneinfo / pytz raise different types
        raise ValueError(
            f"Unknown timezone {tz!r}. Use an IANA name such as 'America/Chicago' "
            "(pip install tzdata if your system has no timezone database)."
        ) from exc
    return tz


def utc_index(unix) -> pd.DatetimeIndex:
    return pd.to_datetime(np.asarray(unix, dtype="int64"), unit="s", utc=True)


def local_naive(unix, tz: str) -> pd.DatetimeIndex:
    """Wall-clock time in ``tz`` as naive timestamps."""
    return utc_index(unix).tz_convert(tz).tz_localize(None)


def local_strings(unix, tz: str) -> list[str]:
    """``YYYY-MM-DD HH:MM:SS`` wall-clock strings (what Plotly draws on a date axis)."""
    return list(utc_index(unix).tz_convert(tz).strftime("%Y-%m-%d %H:%M:%S"))


def local_clock(unix, tz: str) -> tuple[np.ndarray, np.ndarray]:
    """Return (local date as ``YYYY-MM-DD`` strings, seconds since local midnight)."""
    naive = local_naive(unix, tz)
    day = naive.normalize()
    sod = np.asarray((naive - day).total_seconds(), dtype=float)
    return np.asarray(day.strftime("%Y-%m-%d")), sod


def utc_strings(unix) -> list[str]:
    return list(utc_index(unix).strftime("%Y-%m-%d %H:%M:%S"))
