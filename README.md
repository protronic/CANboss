# CANboss

CANopen-Werkzeugkasten von Protronic als **Zephyr-Monorepo**: ein
west-Workspace, eine gemeinsame CANopenNode-Schicht, mehrere Apps —
vom Terminal-Monitor ueber das LVGL-Touchpanel (mit Berry-Scripting)
bis zu simulierten Demo-Knoten, mit denen sich das komplette Netzwerk
ohne Hardware ueber `vcan0` testen laesst.

| App | Beschreibung | Targets |
|---|---|---|
| [apps/monitor](apps/monitor/) | Terminal-Parametermonitor (TUI, Port von CANboss-rs) + Berry-REPL | native_sim, POSIX (`make`) |
| [apps/monitor-py](apps/monitor-py/) | Komfort-Monitor in Python (Textual: Maus, Filter, Schreib-Dialog) | Python 3.10+ (`pip install -e .`) |
| [apps/touch](apps/touch/) | LVGL-Touchpanel mit EDS-generierten Screens + Berry-Scripting | native_sim, stm32h573i_dk |
| [apps/nodes/demo_io](apps/nodes/demo_io/) | Demo-Knoten 16 "IO-Modul" mit Prozesswert-Simulation | native_sim |
| [apps/nodes/demo_drive](apps/nodes/demo_drive/) | Demo-Knoten 32 "Antrieb" (CiA402-Teilmenge, Rampen-Sim) | native_sim |
| [apps/nodes/demo_sensor](apps/nodes/demo_sensor/) | Demo-Knoten 48 "Klimasensor" (Klima-Sim + Alarme) | native_sim |

Alle Apps teilen sich:

- `lib/canopen/` — CAN-Backend-Abstraktion (`can_if`), OS-Abstraktion
  (`osal`), CANopenNode-Port (`port/`) und die Stack-Anbindung
  (`co_node`: RX-/Mainline-Threads, blockierende SDO-Client-Transfers)
- `lib/od/` — generierte Objektverzeichnisse (canboss_master fuer
  Monitor+Panel, demo_* fuer die Knoten; Quelle: `eds/*.eds`)
- `eds/` — network.json + EDS-Dateien des Demo-Netzwerks (eine Quelle)
- `modules/` — Submodule: [CANopenNode](https://github.com/protronic/CANopenNode)
  (protronic-Fork) und [berry](https://github.com/berry-lang/berry)
- `tools/` — Generatoren: `eds2tui.py` (Monitor-Tabellen),
  `eds2lvgl.py` (LVGL-Screens), `poc2lvgl.py` (PoC-Hallenlicht)

## Workspace einrichten

```bash
pip3 install west          # oder: pacman -S python-west / paru -S python-west
mkdir canboss-workspace && cd canboss-workspace
west init -m https://github.com/protronic/CANboss
west update
git -C CANboss submodule update --init
```

Bereits geklont? Workspace um den Klon herum anlegen (`west build`
gibt es erst innerhalb eines Workspace):

```bash
cd <verzeichnis-ueber-dem-klon>
west init -l CANboss && west update
git -C CANboss submodule update --init
```

### Python-Abhaengigkeiten (Zephyr 4.4)

Zephyr braucht u.a. `jsonschema`. Unter Arch/CachyOS (PEP 668):

```bash
sudo pacman -S --needed python-jsonschema python-pyelftools python-pykwalify
# alternativ Workspace-venv (west nutzt dann WEST_PYTHON):
python -m venv .venv
.venv/bin/pip install -r zephyr/scripts/requirements-base.txt west
export WEST_PYTHON=$PWD/.venv/bin/python
```

## Bauen

### native_sim (Host-gcc, kein SDK)

Fuer das Touch-Panel-Fenster braucht der Host SDL2
(`sdl2` unter Arch, `libsdl2-dev` unter Debian/Ubuntu):

```bash
export ZEPHYR_TOOLCHAIN_VARIANT=host

west build -b native_sim/native/64 CANboss/apps/monitor          -d build-monitor
west build -b native_sim/native/64 CANboss/apps/touch            -d build-touch
west build -b native_sim/native/64 CANboss/apps/nodes/demo_io    -d build-io
west build -b native_sim/native/64 CANboss/apps/nodes/demo_drive -d build-drive
west build -b native_sim/native/64 CANboss/apps/nodes/demo_sensor -d build-sensor
```

### Hardware (STM32H573I-DK)

Ab Zephyr **4.4** braucht das Hardware-Target das Zephyr-SDK **1.0**
(aeltere SDKs 0.16/0.17 sind inkompatibel). Unter Arch/CachyOS:

```bash
paru -S zephyr-sdk                 # installiert nach /opt/zephyr-sdk
export ZEPHYR_SDK_INSTALL_DIR=/opt/zephyr-sdk
unset ZEPHYR_TOOLCHAIN_VARIANT     # Default = zephyr/gnu aus dem SDK

west build -b stm32h573i_dk CANboss/apps/touch -d build
# Arch-Paket legt nur stm32_programmer_cli (klein) in PATH; Zephyr
# erwartet STM32_Programmer_CLI — deshalb das SDK-bin-Verzeichnis vorne:
export PATH=/opt/stm32cubeprog/bin:$PATH
west flash
# oder einmalig: west flash --cli=/opt/stm32cubeprog/bin/STM32_Programmer_CLI
```

Ohne SDK (System-Crosscompiler):

```bash
# Arch: arm-none-eabi-gcc (+ optional paru -S arm-none-eabi-picolibc)
ZEPHYR_TOOLCHAIN_VARIANT=gnuarmemb GNUARMEMB_TOOLCHAIN_PATH=/usr \
  west build -b stm32h573i_dk CANboss/apps/touch -- -DTOOLCHAIN_HAS_PICOLIBC=ON
```

Details zum Board-Overlay, Flash und Berry: siehe
[apps/touch/README-zephyr.md](apps/touch/README-zephyr.md).
POSIX-Build des Monitors: [apps/monitor/README.md](apps/monitor/README.md).

### ODs zur Buildzeit aus den EDS-Dateien generieren

Die Objektverzeichnisse unter `lib/od/` werden mit dem EDSSharp-CLI aus
dem [CANopenEditor](https://github.com/protronic/CANopenEditor) erzeugt
(Release `cli-v4.2.3-protronic.1`, Workflow "EDSSharp CLI Release").
Liegt das Tool vor, generieren die CMake-Builds die ODs bei jeder
EDS-Aenderung automatisch neu (siehe `lib/od/od_codegen.cmake`):

```bash
CANboss/tools/get-edssharp.sh     # laedt das Release-Binary nach tools/edssharp/
west build ...                    # "OD-Codegen aus eds/*.eds mit ..." im Log
```

Alternativ zeigt die Umgebungsvariable `EDSSHARP` auf ein beliebiges
EDSSharp-Binary (z.B. lokaler dotnet-Build). Ohne Tool bauen die
eingecheckten Dateien aus `lib/od/` — der Fallback haelt CI und
Container ohne .NET am Laufen. Nach EDS-Aenderungen die eingecheckten
Dateien mit `--export-project` aktualisieren:

```bash
tools/edssharp/EDSSharp --export-project --infile eds/demo_io.eds \
  --outdir lib/od/demo_io --od demo_io --canopennode v4
```

## Das komplette Netzwerk ueber vcan testen

```bash
sudo ip link add dev vcan0 type vcan && sudo ip link set vcan0 up

# Terminals 1-3: die Demo-Knoten (haengen per Default an vcan0)
./build-io/zephyr/zephyr.exe
./build-drive/zephyr/zephyr.exe
./build-sensor/zephyr/zephyr.exe

# Terminal 4: Monitor-TUI (Default vcan0) - Knoten waehlen, r/R liest per SDO
./build-monitor/zephyr/zephyr.exe
# ... oder der komfortablere Python-Monitor (Textual); auf PEP-668-Systemen
# (Arch, Ubuntu 23.04+) per venv oder pipx, siehe apps/monitor-py/README.md:
python -m venv .venv && .venv/bin/pip install -e CANboss/apps/monitor-py
.venv/bin/canboss-monitor

# Terminal 5 (optional): Touch-Panel auf denselben Bus
# (Default ist Loopback -> vcan-Overlay dazubauen)
west build -b native_sim/native/64 CANboss/apps/touch -d build-touch -- \
  -DEXTRA_DTC_OVERLAY_FILE=overlays/vcan.overlay
./build-touch/zephyr/zephyr.exe
```

Was dabei zu sehen ist:

- Die Knoten senden **Heartbeats** und ihre **TPDOs** (Event-Timer
  200 ms) mit simulierten Prozesswerten: `candump vcan0` zeigt den
  Verkehr.
- Monitor/Panel lesen und schreiben Datenpunkte per **SDO** — z.B.
  Sollwert (0x2101) des IO-Moduls setzen: die simulierte Temperatur
  (0x2100) zieht nach; Ziel-Drehzahl (0x6042) + Controlword (0x6040)
  des Antriebs setzen: die Ist-Drehzahl (0x6044) faehrt eine Rampe.
- Im Monitor oeffnet `s` die **Berry-REPL** (help() zeigt Beispiele);
  im Panel landen die PDOs der Knoten im Master-OD (0x2110-0x2132) —
  per **Berry** skriptbar, z.B.:
  `berry od_readf(48, 0x2300, 1)` (Temperatur des Klimasensors per SDO)
  oder `berry od_localf(0x2130, 1)` (dieselbe Groesse aus dem RPDO).
- Alarmtest: `berry "od_write(48, 0x2304, 1, 22.5)"` senkt die
  Temperaturgrenze des Sensors — Alarm (0x2302/0x2305) schlaegt an.

Hinweise:

- Laufen Monitor **und** Panel gleichzeitig, dem Monitor eine andere
  Node-ID geben (beide sind per Default Node 127):
  `CONFIG_CANBOSS_NODE_ID=126` bzw. POSIX `--node-id 126`. Gleichzeitige
  SDO-Zugriffe beider Master auf denselben Knoten koennen sich nach
  CANopen-Regeln in die Quere kommen (gleiche SDO-COB-IDs) — fuer
  Tests nacheinander zugreifen.
- Ohne Kernel-CAN (CI/Container): jede App hat ein Loopback-Overlay
  (`overlays/loopback.overlay` bzw. Default beim Panel).

## Projektlayout

```
west.yml                west-Manifest (Zephyr v4.4.2, LVGL, STM32-HAL)
eds/                    network.json + EDS-Dateien (eine Quelle fuer alles)
tools/                  eds2tui.py, eds2lvgl.py, poc2lvgl.py, get-edssharp.sh
modules/                Submodule: CANopenNode (protronic-Fork), berry
lib/
  canopen/              gemeinsame Schicht: can_if, osal, co_node, CO-Port
  berry_od/             Berry-VM + od_*-Bindings + REPL (Monitor 's', Touch-Shell)
  od/                   generierte ODs: canboss_master, demo_io/drive/sensor
apps/
  monitor/              Terminal-TUI (Zephyr + POSIX-make + Selbsttest)
  monitor-py/           Python-Komfort-Monitor (Textual + canopen, pip)
  touch/                LVGL-Panel + Berry (native_sim, stm32h573i_dk)
  nodes/                Demo-Knoten mit Simulation (common/ + je App)
```

Historie: Der Terminal-Monitor ist der C-Port von
[CANboss-rs](https://github.com/protronic/CANboss-rs); das Touch-Panel
wurde mitsamt Git-Historie aus dem CANbossTouch-Repo importiert
(FreeRTOS/STM32-HAL-Vorgaenger dort in der Historie).
