# CANbossTouch auf Zephyr (west + LVGL + Berry)

Port der CANbossTouch-Oberflaeche auf Zephyr RTOS: dieselbe LVGL-UI
(EDS-generierte Screens aus `App/`, PoC-Hallenlicht) und derselbe
CANopenNode-Stack, gebaut mit `west` — lauffaehig als `native_sim`
(Simulation am PC, SDL-Fenster) und vorbereitet fuer CAN-faehige
Zephyr-Boards. Der STM32-Build (Makefile im Repo-Root) bleibt
unveraendert bestehen.

Dazu kommt **Berry-Scripting** (Submodul
`Middlewares/Third_Party/berry`): eine Skript-VM mit direktem Zugriff
auf das Objektverzeichnis und die PDO-Daten des Netzwerks, bedienbar
ueber das Shell-Kommando `berry`.

## Aufbau

```
zephyr-app/
  CMakeLists.txt        Zephyr-App: App/-Quellen + CANopenNode + Berry
  prj.conf, Kconfig     Konfiguration (CONFIG_CANBOSSTOUCH_*)
  boards/native_sim.*   SDL-Display 800x480, Shell auf stdin/stdout
  overlays/headless.*   Dummy-Display (CI/Container ohne SDL2)
  overlays/vcan.overlay CAN auf Host-SocketCAN statt Loopback
  port/                 CANopenNode-Port (CO_driver auf can_if/osal)
  berry/berry_conf.h    Berry-Konfiguration (ohne Dateisystem/OS-Modul)
  src/
    main.c                    Einstieg: CANopen + SDO-Worker + UI + Berry
    co_zephyr.[ch]            Stack-Anbindung (App/OD, Node 127)
    canboss_sdo_zephyr.c      canboss_sdo-API auf k_msgq/k_thread
    canboss_poc_can_zephyr.c  Raw-Frame-Backend der PoC-UI
    can_if.[ch], can_zephyr.c CAN-Backend (chosen zephyr,canbus)
    osal.[h], osal_zephyr.c   Threads/Mutexe/Zeit
    canboss_berry.[ch]        Berry-VM, od_*-Bindings, Shell-Kommando
    berry_port.c              Berry-I/O (Ausgabe an Shell/Konsole)
```

## Bauen und starten (native_sim)

```bash
pip3 install west
mkdir touch-workspace && cd touch-workspace
west init -m https://github.com/protronic/CANbossTouch
west update
git -C CANbossTouch submodule update --init \
    Middlewares/CANopen/CANopenNode Middlewares/Third_Party/berry

# Simulation am PC (LVGL-Fenster via SDL2, libsdl2-dev noetig):
ZEPHYR_TOOLCHAIN_VARIANT=host west build -b native_sim/native/64 CANbossTouch/zephyr-app
./build/zephyr/zephyr.exe
```

Ohne SDL2 (CI/Container) laeuft die UI unsichtbar auf einem
Dummy-Display weiter:

```bash
west build -b native_sim/native/64 CANbossTouch/zephyr-app -- \
  -DEXTRA_CONF_FILE=overlays/headless.conf \
  -DEXTRA_DTC_OVERLAY_FILE=overlays/headless.overlay
```

### CAN-Anbindung

Standard auf native_sim ist der **CAN-Loopback** (`can_loopback0`):
das Panel redet mit seinem eigenen SDO-Server — ideal zum Ausprobieren
der Berry-Skripte ohne Bus. Fuer echtes Host-SocketCAN:

```bash
sudo ip link add dev vcan0 type vcan && sudo ip link set vcan0 up
west build ... -- -DEXTRA_DTC_OVERLAY_FILE=overlays/vcan.overlay
./build/zephyr/zephyr.exe -can-if=vcan0
```

Auf Hardware-Targets zeigt `chosen zephyr,canbus` im Board-Overlay auf
den CAN-Controller, `zephyr,display` auf das Panel.

## Berry-Scripting (OD-/PDO-Zugriff)

Die Shell (stdin/stdout bzw. UART) bietet das Kommando `berry`.
Skript-Ausgabe (`print`, Fehler) erscheint in der Shell; einfache
Ausdruecke geben ihr Ergebnis direkt zurueck.

```text
uart:~$ berry od_read(127, 0x1017, 0)          # SDO-Upload (Heartbeat-Zeit)
1000
uart:~$ berry od_write(127, 0x1017, 0, 1234, 2) # SDO-Download (UNSIGNED16)
true
uart:~$ berry od_reads(127, 0x1008, 0)          # Geraetename (segmentierter SDO)
CANboss-Master
uart:~$ berry od_local(0x1017, 0)               # eigenes OD direkt
1234
uart:~$ berry od_localf(0x2111, 0)              # REAL32: per RPDO empfangene IO-Temperatur
0
uart:~$ berry "for n : od_nodes() print('Node', n) end"
Node 16
Node 32
Node 48
```

| Funktion | Bedeutung |
|----------|-----------|
| `od_read(node, index, sub)` | SDO-Upload; int (little-endian) oder string (>8 Byte) |
| `od_reads(node, index, sub)` | SDO-Upload als String (VISIBLE_STRING) |
| `od_readf(node, index, sub)` | SDO-Upload REAL32 als real |
| `od_write(node, index, sub, wert, size)` | SDO-Download Integer (size 1/2/4/8) |
| `od_write(node, index, sub, "text" \| 1.5)` | SDO-Download String bzw. REAL32 |
| `od_local(index, sub)` / `od_localf(...)` | eigenes OD lesen — **hier stehen auch die per RPDO empfangenen PDO-Werte** (z.B. 0x2110 Analogeingaenge, 0x2120 Antriebsstatus, 0x2130 Klimasensor) |
| `od_local_write(index, sub, wert[, size])` | eigenes OD schreiben (TPDO-gemappte Werte fuer den Bus setzen) |
| `od_nodes()` | Liste der Node-IDs aus `eds/network.json` |

Mehrzeilige Skripte als ein Argument quoten
(`berry "var s = 0; for i : 1..10 s += i end; print(s)"`); innerhalb
doppelter Anfuehrungszeichen sind einfache fuer Berry-Strings frei.

Die Berry-Konstanten-Tabellen werden beim Bauen automatisch mit
`berry/tools/coc` (Python) nach `Middlewares/Third_Party/berry/generate/`
erzeugt (Konfiguration: `zephyr-app/berry/berry_conf.h`).

## Verifiziert (native_sim, headless)

- LVGL-UI (Hauptmenue aus der EDS-Registry) laeuft auf dem Dummy-Display,
  SDL-Variante baugleich
- SDO end-to-end ueber den CAN-Loopback: `od_read`/`od_write`/`od_reads`
  gegen den eigenen SDO-Server (expedited + segmented)
- Berry-Shell inkl. Schleifen, Variablen, Fehlermeldungen (SDO-Abort
  als Berry-Exception)

## Offene Punkte

- Hardware-Target (STM32H5/U5 mit Zephyr) aufsetzen: Board-Overlay
  fuer FDCAN + Display/Touch, Speicher-Tuning
- Berry-Autostart-Skript (z.B. aus Settings/Flash) und LVGL-Konsole
  fuer Berry direkt am Panel
- PoC-Hallenlicht: minp-Feedback ueber vcan gegen die Demo-Umgebung
  testen
