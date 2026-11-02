"""pyJuxta: analysis of Juxta6 daily logs (``jxta-nor-csv-v10``).

    import pyjuxta as jx

    dep = jx.load_deployment("data/", timezone="America/Chicago")
    dep.set_labels({"JX_54F7A1": "Human"})
    dep.summary()
    dep.activity("Human", bin_s=300).figure()

Every analysis returns a :class:`Result` (tables, parameters, quality, notes, and a Plotly figure spec).
"""

from .io import SCHEMA, load_deployment, peer_to_device_id
from .model import Deployment, Device
from .result import Result

__version__ = "0.1.0"

__all__ = ["Deployment", "Device", "Result", "SCHEMA", "load_deployment", "peer_to_device_id", "__version__"]
