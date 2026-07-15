# CANboss Monitor (Python/Textual)

Komfortable CANopen-Parameter-TUI auf Basis von
[Textual](https://textual.textualize.io/), [python-can](https://python-can.readthedocs.io/)
und [canopen](https://canopen.readthedocs.io/) — die "grosse" Alternative zum
C-Terminal-Monitor ([apps/monitor](../monitor/)): Maus-Bedienung, Filter,
Zebra-Tabellen, Schreib-Dialog, Online-Status per Heartbeat.

Die Netzwerkbeschreibung kommt unveraendert aus [`eds/network.json`](../../eds/)
+ EDS-Dateien — **keine Codegenerierung**, die EDS werden zur Laufzeit ueber
`canopen.import_od` geladen.

## Installation

Auf Distributionen mit PEP-668-Schutz (Arch, Debian 12+, Ubuntu 23.04+)
verweigert das System-`pip` die Installation ("externally-managed-environment")
— venv oder pipx verwenden, **kein sudo noetig**:

```sh
cd CANboss/apps/monitor-py

# Variante A: virtuelles Environment
python -m venv .venv
.venv/bin/pip install -e .          # installiert textual, python-can, canopen
.venv/bin/canboss-monitor           # oder: source .venv/bin/activate

# Variante B: pipx (Arch: pacman -S python-pipx)
pipx install --editable .
canboss-monitor
```

## Start

```sh
canboss-monitor                          # Default: socketcan, vcan0, eds/ im Repo
canboss-monitor --channel can0           # echte Hardware
canboss-monitor --interface serial --channel /dev/ttyUSB0 --bitrate 115200
canboss-monitor --offline                # nur EDS ansehen, ohne CAN
canboss-monitor --eds-dir /pfad/zu/eds   # andere Netzwerkbeschreibung
```

Als `--interface` funktioniert jedes python-can-Interface (socketcan, serial,
slcan, pcan, kvaser, virtual, ...) — damit ist auch die serielle
CAN-Anbindung abgedeckt.

## Bedienung

| Taste | Funktion |
|---|---|
| `Enter` | Knoten oeffnen / (im Knoten) lesen bzw. Schreib-Dialog |
| `r` / `R` | Datenpunkt lesen / alle lesen |
| `w` | Schreib-Dialog (Enter schreibt, Esc bricht ab) |
| `m` | Auto-Refresh ein/aus (Round-Robin wie im C-Monitor) |
| `/` | Filter (Name oder Index), Esc zurueck zur Tabelle |
| `q` / `Esc` | zurueck / beenden |

Die Statuszeile zeigt Kanal, Heartbeat-Status des Knotens und SDO-Fehler
(Abort/Timeout) im Klartext.

## Tests

Laufen komplett ohne CAN-Hardware (python-can `virtual`-Bus, `canopen.LocalNode`
als SDO-Server):

```sh
pip install pytest
python -m pytest tests/
```
