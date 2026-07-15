# CANboss

Terminal-basierter CANopen-Parametermonitor — C-Port von
[CANboss-rs](https://github.com/protronic/CANboss-rs) auf Basis von
[CANopenNode](https://github.com/protronic/CANopenNode) (protronic-Fork,
git-Submodul).

CANboss laedt Geraeteparameter-Definitionen aus den EDS-Dateien des
Netzwerks (einkompiliert ueber `tools/eds2tui.py`), zeigt sie in einer
Terminal-UI und liest/schreibt Live-Werte per SDO direkt ueber den
CAN-Bus — zunaechst ueber **SocketCAN**; eine **serielle** CAN-Anbindung
ist als zweites Backend vorbereitet (`src/can_serial.c`).

Es gibt zwei gleichwertige Builds aus demselben Quellbaum:

1. **POSIX/Linux** (`make`) — laeuft direkt auf SocketCAN
2. **Zephyr/west** (`west build -b native_sim/native/64`) — CANboss als
   Zephyr-Applikation; auf `native_sim` bindet Zephyrs
   `native-linux-can`-Treiber ebenfalls an das Host-SocketCAN an, auf
   Hardware-Targets haengt hier der CAN-Controller aus dem Devicetree

## Unterschiede zum Rust-Original

| CANboss-rs | CANboss (C) |
|------------|-------------|
| Objektverzeichnisse aus CouchDB (`eds`-Datenbank) | einkompilierte Tabellen aus `eds/*.eds` + `eds/network.json` |
| Anlagen-Setups aus CouchDB (`can_device_configuration`) | Knotenliste aus `eds/network.json` (Netzwerk-Browser) |
| GTWA-JSON-Kommandos an ein serielles Gateway | SDO-Client von CANopenNode direkt auf dem Bus |
| ratatui/crossterm | eigene ANSI-Terminal-Schicht (`src/tui.c`, ohne ncurses) |

Der Monitor selbst haengt als CANopen-Knoten am Bus (Default Node-ID 127,
Objektverzeichnis `od/canboss_master.c/h`, generiert von CANopenEditor aus
`eds/canboss_master.eds`). Damit stehen neben dem SDO-Client auch
Heartbeat, EMCY und die RPDOs des Demo-Netzwerks bereit.

## Bauen (POSIX/Linux)

```bash
git clone --recurse-submodules https://github.com/protronic/CANboss.git
cd CANboss
make            # baut ./canboss
make test       # Selbsttest der Wertkonvertierung + Registry
make gen        # src/gen/ neu erzeugen (nach EDS-Aenderungen)
```

Benoetigt nur gcc/make (und python3 fuer `make gen`), keine weiteren
Bibliotheken.

## Bauen (Zephyr / west, native_sim)

CANboss ist zugleich eine Zephyr-Applikation (Manifest-Repository,
T2-Topologie). Einrichtung in einem frischen Workspace:

```bash
pip3 install west
mkdir canboss-workspace && cd canboss-workspace
west init -m https://github.com/protronic/CANboss
west update
git -C CANboss submodule update --init          # CANopenNode

# native_sim baut mit dem Host-gcc, das Zephyr-SDK ist nicht noetig:
ZEPHYR_TOOLCHAIN_VARIANT=host west build -b native_sim/native/64 CANboss
```

Bereits geklont? Dann den Workspace um den vorhandenen Klon herum
anlegen (`west build` gibt es erst innerhalb eines Workspace):

```bash
cd ..                     # ins Verzeichnis UEBER dem Klon
west init -l CANboss      # Klon als Manifest-Repo registrieren
west update               # holt zephyr daneben
```

Liegt daneben auch ein CANbossTouch-Klon, stattdessen
`west init -l CANbossTouch` verwenden — dessen Manifest umfasst alle
Module fuer beide Apps.

Starten (UART-Konsole liegt auf stdin/stdout, die TUI laeuft direkt im
Terminal; Groesse ueber `CONFIG_CANBOSS_TUI_{ROWS,COLS}`):

```bash
sudo ip link add dev vcan0 type vcan && sudo ip link set vcan0 up
./build/zephyr/zephyr.exe                 # nutzt vcan0 (boards/native_sim.overlay)
./build/zephyr/zephyr.exe -can-if=vcan1   # anderes Host-Interface
```

Ist das CAN-Interface nicht vorhanden, startet auch der Zephyr-Build im
Offline-Modus. Fuer Hardware-Targets muss im Board-Overlay `zephyr,canbus`
auf den CAN-Controller zeigen und `zephyr,console` auf eine freie UART;
die App-Optionen (eigene Node-ID, TUI-Groesse) liegen im Kconfig
(`CONFIG_CANBOSS_*`).

## Verwendung

```bash
# CAN-Interface vorbereiten (Bitrate setzt das Interface, nicht CANboss)
sudo ip link set can0 type can bitrate 125000
sudo ip link set can0 up

./canboss --can can0              # Netzwerk-Browser
./canboss --can can0 --node 16    # Monitor fuer Node 16 direkt
./canboss --offline               # ohne Bus browsen
./canboss --list                  # bekannte Knoten ausgeben
```

Fuer Tests ohne Hardware:

```bash
sudo ip link add dev vcan0 type vcan
sudo ip link set vcan0 up
./canboss --can vcan0
```

Schlaegt das Oeffnen des CAN-Backends fehl, startet CANboss wie das
Rust-Original im Offline-Modus (nur Browsen, kein Lesen/Schreiben).

### Tastenbelegung

**Netzwerk-Browser**

| Taste | Aktion |
|-------|--------|
| `↑` / `↓` | Knoten waehlen |
| `Enter` | Monitor oeffnen |
| `/` | Filtern (Enter uebernehmen, Esc abbrechen) |
| `q` | Beenden |

**Parameter-Monitor**

| Taste | Aktion |
|-------|--------|
| `b` | zurueck zum Netzwerk-Browser |
| `↑` / `↓` | Objekt waehlen |
| `←` / `→` / `Tab` | Datenpunkt (Subindex) waehlen |
| `/` | Objekte filtern |
| `r` | gewaehlten Datenpunkt per SDO lesen |
| `R` | alle lesbaren Datenpunkte des Objekts lesen |
| `w` | gewaehlten Datenpunkt schreiben (Enter bestaetigt) |
| `m` | Auto-Refresh (alle 2 s `R`) ein/aus |
| `q` | Beenden |

Werte-Eingaben: Ganzzahlen dezimal oder `0x`-Hex, `BOOL` auch
`true`/`false`, `F32` als Gleitkomma, `OCTET` als Hex-Bytefolge.

## CAN-Backends

Der CANopenNode-Treiber (`port/CO_driver.c`) spricht ausschliesslich die
Abstraktion `src/can_if.h`:

- **socketcan** (`src/can_socketcan.c`) — Linux SocketCAN (POSIX-Build)
- **zephyr** (`src/can_zephyr.c`) — Zephyr-CAN-API, Geraet aus dem
  Devicetree (`chosen zephyr,canbus`); auf native_sim der
  `native-linux-can`-Treiber, auf Hardware der jeweilige Controller
- **serial** (`src/can_serial.c`) — seriell angebundener CAN-Adapter,
  Platzhalter: sobald das Frame-Format des Adapters feststeht, ist nur
  dieses eine Backend zu implementieren; Stack, SDO und UI bleiben
  unveraendert (`--backend serial --can /dev/ttyACM0`)

Plattformdienste (Threads, Mutexe, Zeit) liegen analog hinter
`src/osal.h` (pthreads bzw. `k_thread`/`k_mutex`), Terminal-I/O hinter
`src/tui_io.h` (termios bzw. UART-Konsole).

## Netzwerk anpassen

1. EDS-Datei nach `eds/` legen
2. Knoten in `eds/network.json` eintragen (`node_id`, `name`, `eds`,
   optional `include`-Indexbereiche)
3. `make gen && make`

## Projektlayout

```
CANopenNode/        CANopenNode-Stack (git-Submodul, protronic-Fork)
od/                 Objektverzeichnis des Monitors (canboss_master, generiert
                    von CANopenEditor aus eds/canboss_master.eds)
port/               CANopenNode-Portierung: CO_driver_target.h,
                    CO_driver.c (auf can_if-/osal-Abstraktion)
src/
  main.c            CLI-Einstieg POSIX-Build (Port von main.rs)
  main_zephyr.c     Einstieg Zephyr-Build (Konfiguration via Kconfig/DT)
  canboss.h         Datenpunkt-Modell (Port von proto.rs)
  can_if.[ch]       CAN-Backend-Abstraktion
  can_socketcan.c   SocketCAN-Backend (POSIX)
  can_zephyr.c      Zephyr-CAN-Backend (chosen zephyr,canbus)
  can_serial.c      serielles Backend (geplant)
  osal.h            OS-Abstraktion: Threads/Mutexe/Zeit
  osal_posix.c      ... pthreads/clock_gettime
  osal_zephyr.c     ... k_thread/k_mutex/k_uptime
  co_node.[ch]      CANopenNode-Stack + blockierende SDO-Transfers
                    (ersetzt serial_gtwa.rs/gtwa.rs)
  sdo_value.[ch]    Rohdaten <-> Anzeigetext je CANopen-Datentyp
  tui.[ch]          Terminal-Schicht (ersetzt crossterm/ratatui)
  tui_io.h          Terminal-Plattform-I/O
  tui_io_posix.c    ... termios/select
  tui_io_zephyr.c   ... UART-Konsole (uart_poll_in/out)
  ui.c              Screens: Netzwerk-Browser + Monitor (Port von ui.rs)
  gen/              generierte Knotentabellen (make gen)
eds/                EDS-Dateien + network.json des Demo-Netzwerks
tests/              Selbsttest (make test)
tools/eds2tui.py    EDS -> C-Tabellen-Generator
west.yml            West-Manifest (Zephyr v4.1.0)
CMakeLists.txt      Zephyr-Build der App
prj.conf, Kconfig   Zephyr-Konfiguration + App-Optionen (CONFIG_CANBOSS_*)
boards/             native_sim: CAN-Overlay (vcan0) + UART auf stdin/stdout
```

## Offene Punkte

- serielles CAN-Backend implementieren (`src/can_serial.c`)
- Zephyr-Build auf einem Hardware-Target (CAN-faehiges Board) aufsetzen
  und Speicherbedarf tunen (TUI-Puffer haengen an CONFIG_CANBOSS_TUI_*)
- Live-Anzeige der per RPDO empfangenen Werte des Demo-Netzwerks
  (landen bereits in `canboss_master_RAM`)
- optional: CouchDB-Discovery wie im Rust-Original nachruesten
