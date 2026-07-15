"""Tests des CANboss-Monitors ohne echte CAN-Hardware.

python-can "virtual"-Bus verbindet alle Teilnehmer im Prozess; ein
canopen.LocalNode dient als SDO-Server (simuliertes IO-Modul, Node 16).
"""

from __future__ import annotations

from pathlib import Path

import canopen
import pytest

from canboss_monitor.net import MonitorNet, format_value

EDS_DIR = Path(__file__).resolve().parents[3] / "eds"


@pytest.fixture
def net() -> MonitorNet:
    return MonitorNet(EDS_DIR)


@pytest.fixture
def server():
    """LocalNode 16 (IO-Modul) am virtuellen Bus 'test'."""
    network = canopen.Network()
    network.connect(interface="virtual", channel="test", receive_own_messages=True)
    node = network.add_node(canopen.LocalNode(16, str(EDS_DIR / "demo_io.eds")))
    yield node
    network.disconnect()


def test_network_json_geladen(net: MonitorNet) -> None:
    assert [n.node_id for n in net.nodes] == [16, 32, 48]
    io = net.nodes[0]
    assert io.name == "IO-Modul"
    assert io.datapoints, "IO-Modul muss Datenpunkte haben"
    # Kommunikationsobjekte ausserhalb der include-Bereiche sind gefiltert
    assert all(not (0x1002 <= dp.index <= 0x1007) for dp in io.datapoints)
    # sub0 (highest sub-index) von Records/Arrays wird nicht angezeigt
    assert all(dp.sub != 0 for dp in io.datapoints
               if not isinstance(dp.var.parent, canopen.ObjectDictionary))


def test_datapoint_eigenschaften(net: MonitorNet) -> None:
    io = net.nodes[0]
    by_key = {dp.key: dp for dp in io.datapoints}
    filterzeit = by_key.get("2103:00")
    assert filterzeit is not None
    assert filterzeit.readable and filterzeit.writable
    assert filterzeit.type_name == "U16"
    eingang = by_key.get("2001:01")
    assert eingang is not None
    assert eingang.readable and not eingang.writable
    assert eingang.type_name == "I16"


def test_format_value() -> None:
    assert format_value(True) == "EIN"
    assert format_value(False) == "AUS"
    assert format_value(1.5) == "1.500"
    assert format_value(b"\x01\xff") == "01 FF"
    assert format_value(42) == "42"


def test_parse_typen(net: MonitorNet) -> None:
    io = net.nodes[0]
    by_key = {dp.key: dp for dp in io.datapoints}
    ft = by_key["2103:00"]
    assert MonitorNet._parse(ft, "1000") == 1000
    assert MonitorNet._parse(ft, "0x10") == 16
    with pytest.raises(ValueError):
        MonitorNet._parse(ft, "keine-zahl")
    schalter = by_key["2000:01"]
    assert MonitorNet._parse(schalter, "ein") is True
    assert MonitorNet._parse(schalter, "0") is False


def test_sdo_read_write(net: MonitorNet, server: canopen.LocalNode) -> None:
    err = net.connect(interface="virtual", channel="test", receive_own_messages=True)
    assert err is None, err
    try:
        io = net.nodes[0]
        by_key = {dp.key: dp for dp in io.datapoints}
        ft = by_key["2103:00"]

        server.sdo[0x2103].raw = 750
        assert net.sdo_read(16, ft) == "750"
        assert ft.value == "750"

        net.sdo_write(16, ft, "1250")
        assert server.sdo[0x2103].raw == 1250
        assert net.sdo_read(16, ft) == "1250"

        # Record-Subindex (Digitaler Ausgang 1, BOOL)
        aus1 = by_key["2000:01"]
        net.sdo_write(16, aus1, "ein")
        assert server.sdo[0x2000][1].raw is True
        assert net.sdo_read(16, aus1) == "EIN"
    finally:
        net.disconnect()


def test_sdo_abort_wird_gemeldet(net: MonitorNet, server: canopen.LocalNode) -> None:
    err = net.connect(interface="virtual", channel="test", receive_own_messages=True)
    assert err is None, err
    try:
        io = net.nodes[0]
        dp = io.datapoints[0]
        # Anfrage an einen Knoten, der nicht antwortet -> Timeout/Abort
        with pytest.raises(Exception):
            net.sdo_read(48, dp)
    finally:
        net.disconnect()


def test_offline_verbindung_meldet_fehler(net: MonitorNet) -> None:
    err = net.connect(interface="socketcan", channel="gibt-es-nicht-99")
    assert err is not None
    assert not net.connected
