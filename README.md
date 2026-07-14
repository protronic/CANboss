# CANbossTouch

CANopen-Bediengerät auf **STM32H573I-DK** mit **gen4-FT813-70CTP-CLB**
(4D Systems 7.0" 800×480, FT813/EVE2 über SPI, kapazitiver Touch):
Die LVGL-Widgets und -Screens werden **automatisch aus den EDS-Dateien**
(CAN-Datenpunkten) des Netzwerks generiert. Das Display zeigt die Datenpunkte
der verschiedenen Knoten an und kann sie per **SDO** parametrieren.
Zusätzlich gibt es die **PoC-Hallenlichtsteuerung** (JSON-getriebener
Screen mit Raw-CAN-Bitmaskenprotokoll, siehe unten).

Kombiniert aus:

- FT81x-Port des OxivGL-PoC ([embassy](https://github.com/protronic/embassy)
  `examples/gen4-ft813-70ctp-h573i-dk`) – FT813-Treiber, Panel-Timings, Pinmap
- [CanOpenSTM32](https://github.com/protronic/canopenstm32) – CANopenNode-v4-Stack mit STM32-FDCAN-Port

Die Vorgängerversion für das Riverdi 5.0" STM32U599-Display (LTDC) liegt in
der Git-Historie (Branch-Stand vor dem H573-Port).

**Rust-Port**: Ein vollständiger Port dieses Bediengeräts auf
Rust/embassy/oxivgl (EDS-Screens, eigener SDO-Client, PoC-Hallenlicht)
liegt im embassy-Repo unter `examples/gen4-ft813-70ctp-h573i-dk`
(Binary `canboss_touch`).

**Zephyr-Port**: Die App läuft zusätzlich als Zephyr-Applikation
(`west build -b native_sim/native/64 CANbossTouch/zephyr-app`) mit
derselben LVGL-UI und CANopenNode — inklusive **Berry-Scripting** mit
Skript-Zugriff auf Objektverzeichnis und PDO-Werte über das
Shell-Kommando `berry`. Siehe [zephyr-app/README.md](zephyr-app/README.md).

## Hardware-Verdrahtung (Arduino-Header des STM32H573I-DK)

| Signal (gen4-PA/gen4-IB) | Arduino | MCU-Pin | Funktion |
|---|---|---|---|
| SCK | D13 | PI1 | SPI2_SCK |
| MISO (SDO) | D12 | PI2 | SPI2_MISO |
| MOSI (SDI) | D11 | PB15 | SPI2_MOSI |
| /CS | D10 | PA3 | GPIO |
| PD (Power-Down) | D9 | PA8 | GPIO |
| INT | D8 | PG8 | unbenutzt (Touch wird gepollt) |
| CAN RX | D3 | PB5 | FDCAN2_RX |
| CAN TX | D15 | PB6 | FDCAN2_TX |

Das gen4-Modul braucht **5 V** (Backlight-Boost), Logik 3,3 V. Das DK hat
**keinen CAN-Transceiver an Bord** – ein externer 3,3-V-Transceiver
(SN65HVD230, TJA1051T/3, …) gehört zwischen PB5/PB6 und den Bus.

## Funktionsweise

```
eds/network.json  +  eds/*.eds
        │
        ▼  make gen  (tools/eds2lvgl.py)
App/generated/canboss_gen_node_<id>.c   ← LVGL-Screen je Knoten,
App/generated/canboss_gen_registry.c      ein Widget je EDS-Datenpunkt
        │
        ▼  make  (gnu-tools-for-stm32)
build/CANbossTouch.elf/.hex/.bin
```

Zur Laufzeit:

- **Hauptmenü**: Liste aller Knoten aus `eds/network.json`
- **Knoten-Screen** (generiert): eine Zeile pro Datenpunkt mit Name und
  `Index.Subindex`; Werte werden zyklisch per **SDO-Upload** gelesen
- **Parametrieren** per **SDO-Download** direkt aus dem Widget:

| EDS DataType / AccessType | LVGL-Widget |
|---|---|
| Integer `ro`/`const` mit `LowLimit`+`HighLimit` | Wertanzeige + `lv_bar` (Balken im Limitbereich) |
| `ro` / `const` sonstige | Wertanzeige (Label) |
| `BOOLEAN` `rw` | `lv_switch` |
| Integer `rw` mit Limits genau `0..1` | `lv_switch` |
| Integer `rw` mit Limit-Spanne ≤ 2000 | `lv_slider` mit Live-Wert (Schreiben beim Loslassen) |
| `INTEGER8..32`, `UNSIGNED8..32` `rw` sonst | `lv_spinbox` mit Grenzen aus `LowLimit`/`HighLimit` |
| `REAL32` `rw` | `lv_spinbox` Festkomma (3 Nachkommastellen) |
| `REAL32` `ro` | Anzeige mit 3 Nachkommastellen |
| `VISIBLE_STRING` `rw` | `lv_textarea` mit Bildschirmtastatur |

Die SDO-Transfers laufen in einem eigenen FreeRTOS-Worker-Task
(`App/canboss_sdo.c`) über den CANopenNode-SDO-Client (OD 0x1280,
Expedited + Segmented); die LVGL-Oberfläche (`App/canboss_ui.c`) pollt die
Ergebnisse und bleibt dadurch komplett im LVGL-Task.

## Netzwerk beschreiben

`eds/network.json`:

```json
{
  "nodes": [
    { "node_id": 16, "name": "IO-Modul",    "eds": "demo_io.eds" },
    { "node_id": 32, "name": "Antrieb",     "eds": "demo_drive.eds" },
    { "node_id": 48, "name": "Klimasensor", "eds": "demo_sensor.eds" }
  ]
}
```

Die drei Knoten sind das **Demo-Netzwerk** aus dem CANopenEditor-Repo
(`CANopenEditor/demo/`): IO-Modul (digitale/analoge Ein-/Ausgänge,
BOOL-Datenpunkte in RPDO/TPDO und per SDO), Antrieb (CiA402-Teilmenge)
und Klimasensor (Temperatur/Feuchte/Druck/Alarm). Die EDS-Dateien hier
sind Kopien von dort; die zugehörigen CANopenNode-ODs der Geräteknoten
liegen generiert unter `CANopenEditor/demo/generated/`, damit die Knoten
auf echter Hardware laufen können.

- `node_id` – CANopen-Node-ID (1…127)
- `eds` – EDS-Datei (CiA 306) im Verzeichnis `eds/`
- `include` (optional) – Indexbereiche, die auf dem Screen erscheinen.
  Default: `0x1000-0x1001`, `0x1008-0x100A`, `0x2000-0x9FFF`
  (Geräteinfo + Hersteller-/Profilbereich)

Danach Code neu generieren und bauen:

```sh
make gen
make
```

Die generierten Dateien in `App/generated/` sind eingecheckt, damit das
Projekt ohne Python baubar bleibt.

## Bauen (gnu-tools-for-stm32)

Das Projekt wird mit der ST-Toolchain
[gnu-tools-for-stm32](https://github.com/STMicroelectronics/gnu-tools-for-stm32)
gebaut (`arm-none-eabi-gcc`, in STM32CubeIDE enthalten):

```sh
git clone --recurse-submodules https://github.com/protronic/CANbossTouch
cd CANbossTouch

# Pfad zur CubeIDE-Toolchain angeben ...
make GCC_PATH=/opt/st/stm32cubeide/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.13.3.rel1.linux64_*/tools/bin -j

# ... oder arm-none-eabi-gcc aus dem PATH verwenden (gleiche Codebasis)
make -j
```

Ergebnis: `build/CANbossTouch.elf` / `.hex` / `.bin` (flashen z. B. mit
STM32CubeProgrammer).

Submodule: `Middlewares/Third_Party/LVGL/lvgl` (LVGL 9.5) und
`Middlewares/CANopen/CANopenNode` (CANopenNode v4, **protronic-Fork** github.com/protronic/CANopenNode).

## Hardware / CAN

- **FDCAN2** auf PB5 (RX) / PB6 (TX), Classic CAN **500 kbit/s**
  (25 MHz HSE-Kernel-Takt, Prescaler 5, 10 tq, Samplepoint 80 % –
  anpassbar in `Core/Src/fdcan.c`)
- **TIM7** liefert den 1-ms-Takt für den CANopen-Stack
  (`canopen_app_interrupt()`)
- Eigene Node-ID des Panels: `CANBOSS_NODE_ID` (Default **127**,
  in `Core/Inc/main.h`)
- Das Panel ist selbst ein vollwertiger CANopen-Knoten und **Quasi-Master**
  des Demo-Netzwerks: `App/OD/OD.c/.h` ist aus `eds/canboss_master.eds`
  generiert (CANopenNode v4, NMT/Heartbeat/SDO-Server/SDO-Client/LSS-Slave)

## Master-OD (App/OD)

Das Objektverzeichnis des Panels stammt aus `eds/canboss_master.eds`
(Demo-Netzwerk, siehe `CANopenEditor/demo/README.md`) und macht das Panel
zum Monitor des gesamten Netzwerks – zusätzlich zum SDO-Polling der
EDS-Screens:

- **7 RPDOs** hören auf die TPDOs der Demo-Knoten und spiegeln alle
  Prozessdaten in eigene OD-Objekte: 0x2110/0x2111/0x2112 (IO: analoge
  Eingänge, Temperatur, digitale Eingänge als BOOL), 0x2120/0x2121
  (Antrieb: Status, Drehzahlen, Motorstrom, Endstufentemperatur),
  0x2130–0x2132 (Sensor: Klima, Umgebung, Alarm)
- **2 TPDOs** senden Kommandos auf die RPDOs der Knoten: 0x2210
  (IO: Ausgänge 1–4 als BOOL + Sollwert) und 0x2220 (Antrieb:
  Controlword, Drehzahl-Sollwert, Bremse)
- **Heartbeat-Consumer** (0x1016) überwacht die Knoten 16/32/48
  (Timeout 3000 ms), eigener Heartbeat 1000 ms
- **SDO-Client** (0x1280) für die Parametrierung aus der Touch-Oberfläche

Neu generieren (EDSSharp-CLI aus dem CANopenEditor-Repo):

```sh
dotnet EDSSharp.dll --export-project --infile eds/canboss_master.eds \
    --outdir App/OD --od OD --json /tmp/CANboss-Master.json
```

## Display (FT813 / EVE2 über SPI)

- `Core/Src/ft81x.c`: minimaler FT81x-Hosttreiber (C-Port des Rust-Treibers
  aus dem embassy-Repo) – Power-up mit CLKINT/CLKEXT-Fallback, Standard-
  WVGA-Timings, RAM_G als "dummer Framebuffer" mit statischer
  Scanout-Displayliste, Backlight über `REG_PWM_DUTY`
- LVGL rendert im **PARTIAL-Modus** in zwei 40-Zeilen-Streifenpuffer
  (2 × 64 000 B SRAM); der Flush-Callback (`Core/Src/lvgl_port_display.c`)
  schiebt Dirty-Rechtecke per SPI2 (15,6 MHz) ins RAM_G
- Touch liefert die FT813-Touch-Engine, gepollt im LVGL-Read-Callback
  (`Core/Src/lvgl_port_touch.c`) – Display und Touch teilen sich den
  SPI-Bus im LVGL-Task, kein Interrupt-Pin nötig

## PoC-Hallenlichtsteuerung (JSON + CANopenNode)

Der Hauptmenü-Eintrag **„Hallenlicht (PoC)"** öffnet den aus
`poc/{hall,can}_config.json` generierten 5-Spalten-Screen — dieselben
JSON-Dateien wie im OxivGL-PoC (`examples/touch-projects/Demo` im
embassy-Repo). Generator: `tools/poc2lvgl.py` (läuft bei `make gen` mit,
Ergebnis `App/generated/canboss_poc_gen.[ch]` ist eingecheckt).

Protokoll (kompatibel zu `examples/touch-hall-common`):

- **TX** `tx_id` (Demo: 0x200): 6-Byte-One-Hot-Bitmaske des gedrückten
  Buttons, Wiederholung alle `command_repeat_ms` (25 ms) solange gehalten,
  Release = 6 Nullbytes, Idle-Keepalive 1 s
- **RX** minp-ID (Demo: 0x285): Pegelbits der `minp`-Tabelle steuern das
  Button-Highlight

Die Frames laufen über **denselben FDCAN wie der CANopen-Stack**: TX über
die `CO_LOCK_CAN_SEND`-gesicherte HAL-TX-FIFO, RX über den
Unmatched-Frame-Hook `CO_CANrawRxHook()` im STM32-Port
(`CO_driver_STM32.c`) — Frames, die kein CANopen-RX-Puffer beansprucht,
werden an die Anwendung durchgereicht. Das `state_script` (Rhai) des
OxivGL-PoC wird auf dem C-Target nicht ausgeführt; das Highlight folgt
direkt den minp-Pegeln.

## Host-Build (Linux/SDL2)

Die komplette Oberflaeche (EDS-Screens + PoC-Hallenlicht) laeuft auch am
Entwicklungs-PC — Vorbild ist `examples/oxivgl-host` im embassy-Repo:

```sh
sudo apt-get install libsdl2-dev pkg-config
make host
./build/host/canboss_host [can-interface]   # Default: vcan0
```

- Display/Maus: LVGL-SDL-Treiber (`host/lv_conf.h`, 800×480-Fenster)
- SDO: Mock (`host/canboss_sdo_host.c`) — Leseaufträge liefern simulierte,
  zum EDS-Datentyp passende Werte; Geschriebenes wird zurückgelesen
- PoC-CAN: SocketCAN (`host/canboss_poc_can_host.c`); zum Testen:

```sh
sudo modprobe vcan
sudo ip link add dev vcan0 type vcan
sudo ip link set vcan0 up
candump vcan0                       # TX-Bitmasken beobachten
cansend vcan0 285#0F00000000000000  # minp-Feedback -> Button-Highlights
```

Headless-Vorschau/CI: `CANBOSS_HOST_SCREENSHOT=<prefix>` rendert
nacheinander Hauptmenü, alle Knoten-Screens (inkl. gescrolltem Listenende)
und den PoC-Screen, schreibt BMPs und beendet sich
(`SDL_VIDEODRIVER=dummy` funktioniert).

## Projektstruktur

```
App/                    Anwendung
  canboss_ui.c/h        LVGL-Laufzeit: Menü, Widget-Fabriken, SDO-Refresh
  canboss_sdo.c/h       SDO-Client-Worker (FreeRTOS-Task + Queue)
  canboss_poc.c/h       PoC-Hallenlichtsteuerung (JSON-Screen + Raw-CAN)
  canboss_types.h       gemeinsame Typen der generierten Tabellen
  CO_driver_custom.h    CANopenNode-Konfiguration (SDO-Client, LSS, FIFO)
  OD/                   Objektverzeichnis des Panels (DS301)
  generated/            ⚙ vom Generator erzeugte Screens (nicht editieren)
Core/                   MCU-Code (STM32H573I-DK, FT81x, Peripherie, FreeRTOS-Start)
  Src/ft81x.c           FT813-Treiber (SPI2, RAM_G-Framebuffer, Touch)
Drivers/                STM32H5 HAL + CMSIS (Device H5)
Middlewares/
  CANopen/CANopenNode        CANopenNode v4 (Submodul, protronic-Fork)
  CANopen/CANopenNode_STM32  STM32-FDCAN-Port (+ CO_CANrawRxHook fuer PoC)
  Third_Party/LVGL           lv_conf.h + LVGL (Submodul)
  Third_Party/FreeRTOS       FreeRTOS-Kernel + CMSIS-RTOS2
eds/                    network.json + EDS-Dateien des Netzwerks
poc/                    hall_config.json + can_config.json (PoC-Hallen-UI)
host/                   Linux/SDL2-Host-Build (main, SDO-Mock, SocketCAN)
tools/eds2lvgl.py       EDS→LVGL-Codegenerator
tools/poc2lvgl.py       PoC-JSON→LVGL/CAN-Codegenerator
Startup/, *.ld          Startup-Code und Linkerskript (STM32H573IIKxQ)
Makefile                Firmware-Build mit gnu-tools-for-stm32
Makefile.host           Host-Build (make host)
```
