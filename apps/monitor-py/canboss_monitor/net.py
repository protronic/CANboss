"""CANopen-Zugriffsschicht des Monitors.

Laedt das Netzwerk aus eds/network.json + EDS-Dateien (direkt ueber die
canopen-Bibliothek, keine Codegenerierung) und kapselt Verbindung,
SDO-Transfers und den Online-Status (Heartbeat) der Knoten.
"""

from __future__ import annotations

import json
import threading
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

import canopen
from canopen.objectdictionary import ODArray, ODRecord, ODVariable

#: EDS-AccessTypes, die lesbar bzw. schreibbar sind
_READABLE = {"ro", "rw", "rww", "rwr", "const"}
_WRITABLE = {"wo", "rw", "rww", "rwr"}

#: Heartbeat-Timeout, ab dem ein Knoten als offline gilt
ONLINE_TIMEOUT_S = 3.0

#: Standard-Indexbereiche fuer die Anzeige (wie eds2tui/eds2lvgl)
_DEFAULT_INCLUDE = ((0x1000, 0x1001), (0x1008, 0x100A), (0x2000, 0x9FFF))


@dataclass
class Datapoint:
    """Ein (Index, Subindex)-Blatt aus der EDS."""

    index: int
    sub: int
    name: str
    var: ODVariable
    value: str = ""  # zuletzt gelesener/geschriebener Anzeigewert

    @property
    def key(self) -> str:
        return f"{self.index:04X}:{self.sub:02X}"

    @property
    def access(self) -> str:
        return (self.var.access_type or "ro").lower()

    @property
    def readable(self) -> bool:
        return self.access in _READABLE

    @property
    def writable(self) -> bool:
        return self.access in _WRITABLE

    @property
    def type_name(self) -> str:
        from canopen.objectdictionary import datatypes

        return {
            datatypes.BOOLEAN: "BOOL",
            datatypes.INTEGER8: "I8",
            datatypes.INTEGER16: "I16",
            datatypes.INTEGER32: "I32",
            datatypes.INTEGER64: "I64",
            datatypes.UNSIGNED8: "U8",
            datatypes.UNSIGNED16: "U16",
            datatypes.UNSIGNED32: "U32",
            datatypes.UNSIGNED64: "U64",
            datatypes.REAL32: "F32",
            datatypes.REAL64: "F64",
            datatypes.VISIBLE_STRING: "STR",
            datatypes.OCTET_STRING: "OCTET",
        }.get(self.var.data_type, f"0x{self.var.data_type:02X}" if self.var.data_type else "?")

    @property
    def limits(self) -> str:
        lo, hi = self.var.min, self.var.max
        if lo is None and hi is None:
            return ""
        return f"{lo if lo is not None else ''}..{hi if hi is not None else ''}"


@dataclass
class NodeDef:
    """Ein Knoten aus eds/network.json."""

    node_id: int
    name: str
    eds_file: str
    od: canopen.ObjectDictionary
    datapoints: list[Datapoint] = field(default_factory=list)


def _in_ranges(index: int, ranges) -> bool:
    return any(lo <= index <= hi for lo, hi in ranges)


def _collect_datapoints(od: canopen.ObjectDictionary, include=_DEFAULT_INCLUDE) -> list[Datapoint]:
    dps: list[Datapoint] = []
    for obj in od.values():
        if not _in_ranges(obj.index, include):
            continue
        if isinstance(obj, ODVariable):
            dps.append(Datapoint(obj.index, obj.subindex, obj.name, obj))
        elif isinstance(obj, (ODRecord, ODArray)):
            for var in obj.values():
                if var.subindex == 0:  # "Highest sub-index supported" ausblenden
                    continue
                dps.append(Datapoint(obj.index, var.subindex, f"{obj.name}: {var.name}", var))
    dps.sort(key=lambda d: (d.index, d.sub))
    return dps


def format_value(value: Any) -> str:
    if isinstance(value, bool):
        return "EIN" if value else "AUS"
    if isinstance(value, float):
        return f"{value:.3f}"
    if isinstance(value, (bytes, bytearray)):
        return value.hex(" ").upper()
    return str(value)


class MonitorNet:
    """Netzwerkdefinition + optionale CAN-Verbindung."""

    def __init__(self, eds_dir: Path, network_file: Path | None = None) -> None:
        self.eds_dir = Path(eds_dir)
        self.network_file = Path(network_file) if network_file else self.eds_dir / "network.json"
        self.nodes: list[NodeDef] = []
        self.network: canopen.Network | None = None
        self.channel = ""
        self._remote: dict[int, canopen.RemoteNode] = {}
        self._sdo_lock = threading.Lock()
        self._load()

    def _load(self) -> None:
        spec = json.loads(self.network_file.read_text(encoding="utf-8"))
        for entry in spec["nodes"]:
            eds_path = self.eds_dir / entry["eds"]
            od = canopen.import_od(str(eds_path), node_id=entry["node_id"])
            node = NodeDef(entry["node_id"], entry["name"], entry["eds"], od)
            node.datapoints = _collect_datapoints(od)
            self.nodes.append(node)

    # ------------------------------------------------------------- Verbindung

    def connect(self, interface: str = "socketcan", channel: str = "vcan0", **kwargs) -> str | None:
        """CAN verbinden; Rueckgabe None bei Erfolg, sonst Fehlertext."""
        try:
            self.network = canopen.Network()
            self.network.connect(interface=interface, channel=channel, **kwargs)
        except Exception as exc:  # noqa: BLE001 - alles als Offline-Grund melden
            self.network = None
            return f"{type(exc).__name__}: {exc}"
        self.channel = channel
        for node in self.nodes:
            remote = self.network.add_node(node.node_id, node.od)
            remote.sdo.RESPONSE_TIMEOUT = 0.5
            self._remote[node.node_id] = remote
        return None

    def disconnect(self) -> None:
        if self.network is not None:
            try:
                self.network.disconnect()
            except Exception:  # noqa: BLE001
                pass
            self.network = None
            self._remote.clear()

    @property
    def connected(self) -> bool:
        return self.network is not None

    def node_online(self, node_id: int) -> bool | None:
        """True/False nach Heartbeat, None wenn offline/unbekannt."""
        remote = self._remote.get(node_id)
        if remote is None:
            return None
        ts = remote.nmt.timestamp  # letzte Heartbeat-Ankunft (monotonic)
        if ts is None:
            return None
        return (time.monotonic() - ts) < ONLINE_TIMEOUT_S

    # ------------------------------------------------------------------- SDO

    def _sdo_entry(self, node_id: int, dp: Datapoint):
        remote = self._remote[node_id]
        if dp.var.parent is not None and not isinstance(dp.var.parent, canopen.ObjectDictionary):
            return remote.sdo[dp.index][dp.sub]
        return remote.sdo[dp.index]

    def sdo_read(self, node_id: int, dp: Datapoint) -> str:
        """Liest den Datenpunkt; Rueckgabe formatierter Wert.

        Wirft Exception mit Klartext bei Abort/Timeout.
        """
        with self._sdo_lock:
            value = self._sdo_entry(node_id, dp).raw
        dp.value = format_value(value)
        return dp.value

    def sdo_write(self, node_id: int, dp: Datapoint, text: str) -> str:
        """Parst text gemaess Datentyp und schreibt per SDO."""
        value = self._parse(dp, text)
        with self._sdo_lock:
            self._sdo_entry(node_id, dp).raw = value
        dp.value = format_value(value)
        return dp.value

    @staticmethod
    def _parse(dp: Datapoint, text: str) -> Any:
        from canopen.objectdictionary import datatypes

        text = text.strip()
        dt = dp.var.data_type
        if dt == datatypes.VISIBLE_STRING:
            return text
        if dt == datatypes.OCTET_STRING:
            return bytes.fromhex(text.replace(" ", ""))
        if dt in (datatypes.REAL32, datatypes.REAL64):
            return float(text.replace(",", "."))
        if dt == datatypes.BOOLEAN:
            if text.lower() in ("1", "true", "ein", "an", "on"):
                return True
            if text.lower() in ("0", "false", "aus", "off"):
                return False
            raise ValueError(f"'{text}' ist kein BOOL (0/1/ein/aus)")
        return int(text, 0)  # dezimal oder 0x-Hex
