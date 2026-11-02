"""Result object returned by every pyJuxta analysis."""

from __future__ import annotations

import json
import math
from dataclasses import dataclass, field
from typing import Any, Callable

import numpy as np
import pandas as pd


def jsonable(obj: Any) -> Any:
    """Convert numpy / pandas values to plain JSON types (NaN and inf become ``None``)."""
    if obj is None or isinstance(obj, (str, bool)):
        return obj
    if isinstance(obj, (int,)) and not isinstance(obj, bool):
        return int(obj)
    if isinstance(obj, (float, np.floating)):
        f = float(obj)
        return f if math.isfinite(f) else None
    if isinstance(obj, np.integer):
        return int(obj)
    if isinstance(obj, np.bool_):
        return bool(obj)
    if isinstance(obj, (pd.Timestamp,)):
        return None if pd.isna(obj) else obj.isoformat()
    if isinstance(obj, dict):
        return {str(k): jsonable(v) for k, v in obj.items()}
    if isinstance(obj, (list, tuple, set)):
        return [jsonable(v) for v in obj]
    if isinstance(obj, np.ndarray):
        return [jsonable(v) for v in obj.tolist()]
    if isinstance(obj, pd.DataFrame):
        return table_dict(obj)
    if isinstance(obj, pd.Series):
        return [jsonable(v) for v in obj.tolist()]
    if obj is pd.NaT:
        return None
    return str(obj)


def table_dict(df: pd.DataFrame, max_rows: int | None = None) -> dict:
    """``{"columns": [...], "rows": [[...]], "n_rows": N}`` for a DataFrame, optionally truncated."""
    n = len(df)
    view = df if max_rows is None else df.head(max_rows)
    rows = [[jsonable(v) for v in row] for row in view.itertuples(index=False, name=None)]
    return {"columns": [str(c) for c in view.columns], "rows": rows, "n_rows": n}


@dataclass
class Result:
    """What an analysis returns: tables first, a figure on request.

    Attributes
    ----------
    name
        Analysis name, e.g. ``"proximity"``.
    data
        The primary table (one row per bin, episode, device, ...).
    parameters
        Every setting that shaped the result, so it can be reported with the numbers.
    quality
        Coverage and sample counts: how much of the deployment this result is based on.
    summary
        Headline numbers.
    tables
        Extra named tables.
    notes
        Plain-language caveats that belong next to this result.
    """

    name: str
    data: pd.DataFrame
    parameters: dict = field(default_factory=dict)
    quality: dict = field(default_factory=dict)
    summary: dict = field(default_factory=dict)
    tables: dict = field(default_factory=dict)
    notes: list = field(default_factory=list)
    _figure: Callable[["Result", str], dict] | None = field(default=None, repr=False)
    _context: dict = field(default_factory=dict, repr=False)

    def figure(self, theme: str = "light") -> dict:
        """Plotly figure as ``{"data": [...], "layout": {...}}``.

        ``theme`` is ``"light"`` (default; notebooks) or ``"dark"`` (the walkthrough page).
        Use ``plotly.graph_objects.Figure(result.figure())`` or :meth:`show`.
        """
        if self._figure is None:
            raise ValueError(f"{self.name} has no figure")
        return jsonable(self._figure(self, theme))

    def show(self, theme: str = "light"):
        """Display the figure with Plotly (``pip install plotly``)."""
        try:
            import plotly.graph_objects as go
        except ImportError as exc:  # pragma: no cover
            raise ImportError("Result.show() needs plotly: pip install plotly") from exc
        fig = go.Figure(self.figure(theme))
        fig.show()
        return fig

    def to_dict(self, theme: str = "dark", max_rows: int = 200) -> dict:
        """Everything the walkthrough needs, as JSON-safe values."""
        return {
            "name": self.name,
            "parameters": jsonable(self.parameters),
            "quality": jsonable(self.quality),
            "summary": jsonable(self.summary),
            "notes": list(self.notes),
            "tables": {k: table_dict(v, max_rows) for k, v in self.tables.items()},
            "data": table_dict(self.data, max_rows),
            "figure": self.figure(theme) if self._figure is not None else None,
        }

    def to_json(self, theme: str = "dark", max_rows: int = 200) -> str:
        return json.dumps(self.to_dict(theme, max_rows), allow_nan=False)

    def __repr__(self) -> str:
        return f"Result({self.name!r}, rows={len(self.data)}, summary={self.summary})"
