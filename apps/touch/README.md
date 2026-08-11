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

Die Firmware ist eine **Zephyr-RTOS-Applikation** (``,
gebaut mit `west`): gleiche LVGL-UI und CANopenNode auf dem
STM32H573I-DK wie in der `native_sim`-Simulation am PC — inklusive
**Berry-Scripting** mit Skript-Zugriff auf Objektverzeichnis und
PDO-Werte über das Shell-Kommando `berry`. Details und Build-Anleitung:
[README.md](README.md).

Historie: Der frühere FreeRTOS/STM32-HAL-Build (Makefile), der
Linux/SDL2-Host-Build und die Vorgängerversion für das Riverdi-5.0"-
U599-Display liegen in der Git-Historie; ein Rust-Port des Bediengeräts
(embassy/oxivgl) liegt im embassy-Repo unter
`examples/gen4-ft813-70ctp-h573i-dk`.

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
        ▼  python3 tools/eds2lvgl.py  (+ tools/poc2lvgl.py)
app/generated/canboss_gen_node_<id>.c   ← LVGL-Screen je Knoten,
app/generated/canboss_gen_registry.c      ein Widget je EDS-Datenpunkt
        │
        ▼  west build  ()
build/zephyr/zephyr.elf|hex  (stm32h573i_dk)  bzw.  zephyr.exe  (native_sim)
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

Die SDO-Transfers laufen in einem eigenen Worker-Thread
(`src/canboss_sdo_zephyr.c`) über den CANopenNode-SDO-Client (OD 0x1280,
Expedited + Segmented); die LVGL-Oberfläche (`app/canboss_ui.c`) pollt die
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
python3 tools/eds2lvgl.py && python3 tools/poc2lvgl.py
west build -d build ...
```

Die generierten Dateien in `app/generated/` sind eingecheckt, damit das
Projekt ohne Python baubar bleibt.

## Bauen (Zephyr / west)

Beide Targets — Hardware (STM32H573I-DK) und PC-Simulation
(`native_sim`, LVGL-Fenster via SDL2) — baut `west` aus
``; ausführliche Anleitung inkl. Workspace-Einrichtung in
[README.md](README.md):

```sh
west init -m https://github.com/protronic/CANboss && west update
git -C CANboss submodule update --init

# Simulation am PC
west build -b native_sim/native/64 CANboss/apps/touch

# STM32H573I-DK (Zephyr-SDK oder arm-none-eabi-gcc + picolibc)
west build -b stm32h573i_dk CANboss/apps/touch
west flash
```

Submodule: `modules/CANopenNode` (CANopenNode v4,
**protronic-Fork** github.com/protronic/CANopenNode) und
`modules/berry` (Berry-Skriptsprache). LVGL kommt als
Zephyr-Modul aus dem west-Manifest.

## Hardware / CAN

- **FDCAN2** auf PB5 (RX) / PB6 (TX), Classic CAN **500 kbit/s** —
  konfiguriert im Devicetree-Overlay
  (`boards/stm32h573i_dk.overlay`)
- Eigene Node-ID des Panels: `CONFIG_CANBOSSTOUCH_NODE_ID`
  (Default **127**)
- Das Panel ist selbst ein vollwertiger CANopen-Knoten und **Quasi-Master**
  des Demo-Netzwerks: `lib/od/canboss_master/OD.c/.h` ist aus `eds/canboss_master.eds`
  generiert (CANopenNode v4, NMT/Heartbeat/SDO-Server/SDO-Client)

## Master-OD (lib/od/canboss_master)

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
    --outdir lib/od/canboss_master --od OD --json /tmp/CANboss-Master.json
```

## Display (FT813 / EVE2 über SPI)

Auf dem **STM32H573I-DK** rendert LVGL 9.5 per **`LV_USE_DRAW_EVE`**
direkt als EVE-Displaylisten (kein MCU-Framebuffer):

- `drivers/lv_draw_eve_zephyr.c`: SPI/GPIO-`op_cb` am DT-Knoten
  `protronic,ft813` (PD_N, manuelles CS, Bring-up 8 MHz → Betrieb
  15,6 MHz), Panel-Timings gen4-FT813-70 (WVGA), Touch über
  `lv_draw_eve_touch_create` (CTOUCH-Kompatibilitätsmodus)
- Zephyr-Chosen `zephyr,display` ist ein `zephyr,dummy-dc` (das
  LVGL-Modul braucht ein Chosen); die UI nutzt das EVE-Display als
  Default (`CONFIG_CANBOSSTOUCH_DRAW_EVE`)
- `native_sim` bleibt beim Zephyr-Display-Binding (SDL / headless Dummy)

Legacy (nicht für stm32h573i_dk): `drivers/ft813.c` als dummer
RGB565-Framebuffer in `RAM_G` + `zephyr,lvgl-pointer-input`.

## PoC-Hallenlichtsteuerung (JSON + CANopenNode)

Der Hauptmenü-Eintrag **„Hallenlicht (PoC)"** öffnet den aus
`poc/{hall,can}_config.json` generierten 5-Spalten-Screen — dieselben
JSON-Dateien wie im OxivGL-PoC (`examples/touch-projects/Demo` im
embassy-Repo). Generator: `tools/poc2lvgl.py` (Ergebnis `app/generated/canboss_poc_gen.[ch]` ist eingecheckt).

Protokoll (kompatibel zu `examples/touch-hall-common`):

- **TX** `tx_id` (Demo: 0x200): 6-Byte-One-Hot-Bitmaske des gedrückten
  Buttons, Wiederholung alle `command_repeat_ms` (25 ms) solange gehalten,
  Release = 6 Nullbytes, Idle-Keepalive 1 s
- **RX** minp-ID (Demo: 0x285): Pegelbits der `minp`-Tabelle steuern das
  Button-Highlight

Die Frames laufen über **denselben CAN wie der CANopen-Stack**: TX über
das `can_if`-Backend (`src/canboss_poc_can_zephyr.c`), RX
über den RX-Thread der CANopen-Schicht (`src/co_zephyr.c`),
der jeden Frame zusätzlich an `canboss_poc_can_rx()` durchreicht. Das
`state_script` (Rhai) des OxivGL-PoC wird nicht ausgeführt; das
Highlight folgt direkt den minp-Pegeln.

## Simulation am PC (native_sim)

Die komplette Oberfläche (EDS-Screens + PoC-Hallenlicht + Berry-Shell)
läuft als `native_sim`-Build am Entwicklungs-PC — mit **echtem**
CANopen-Stack statt SDO-Mock: Standard ist der CAN-Loopback (das Panel
beantwortet seine eigenen SDO-Anfragen), per Overlay auch Host-SocketCAN
(`vcan0`). Siehe [README.md](README.md).

### Monkey-Test (headless UI-Stresstest)

Zwei Compile-Schalter bauen einen Selbsttest ein, der die Oberfläche
ohne Fenster dauerbedient: `CB_DEBUG_OPEN_NODE` erzeugt zufällige
Touch-Eingaben über den echten Zephyr-Input-Pfad (Navigation, Slider,
Tastatur, ...) und loggt die LVGL-Heap-Belegung; `CB_DEBUG_FAKE_SDO`
lässt den SDO-Worker zufällige Erfolgs-/Fehlerantworten liefern, damit
auch die Ergebnispfade der Widgets laufen. Zusammen mit AddressSanitizer
findet das Speicherfehler, bevor sie am Panel zuschlagen:

```sh
west build -p -b native_sim/native/64 CANboss/apps/touch -d build-monkey -- \
  -DCONFIG_SDL_DISPLAY_USE_HARDWARE_ACCELERATOR=n -DCONFIG_ASAN=y \
  -DCONFIG_SYS_HEAP_RUNTIME_STATS=y \
  -DEXTRA_CFLAGS="-DCB_DEBUG_OPEN_NODE;-DCB_DEBUG_FAKE_SDO"
SDL_VIDEODRIVER=dummy ./build-monkey/zephyr/zephyr.exe
```

Hinweis zum LVGL-Speicher: beim Screen-Wechsel wird der alte Screen
**vor** dem Aufbau des neuen gelöscht (`canboss_ui_screen_prepare()`),
damit nie beide gleichzeitig im Pool liegen. Gemessener Spitzenbedarf
(64-bit native_sim, Monkey-Test): ~56 KiB — `CONFIG_LV_Z_MEM_POOL_SIZE`
entsprechend dimensionieren (native_sim: 256 KiB, H573: 40 KiB bei
halbierten 32-bit-Strukturen). `CONFIG_LV_USE_ASSERT_MALLOC=y` macht
einen vollen Pool als Assert sichtbar statt als Absturz.

## Berry-Scripting

Das Shell-Kommando `berry` (Konsole/UART) führt Skripte mit direktem
Zugriff auf das Objektverzeichnis aus — `od_read`/`od_write` (SDO zu
beliebigen Knoten), `od_local`/`od_local_write` (eigenes OD inkl. der
per RPDO empfangenen PDO-Werte), `od_nodes()`. API-Tabelle und
Beispiele in [README.md](README.md).

## Projektstruktur

```
App/                    Anwendung (plattformunabhaengige LVGL-Schicht)
  canboss_ui.c/h        LVGL-Laufzeit: Menü, Widget-Fabriken, SDO-Refresh
  canboss_sdo.h         API des SDO-Client-Workers (Impl. in )
  canboss_poc.c/h       PoC-Hallenlichtsteuerung (JSON-Screen + Raw-CAN)
  canboss_types.h       gemeinsame Typen der generierten Tabellen
  OD/                   Objektverzeichnis des Panels (DS301)
  generated/            ⚙ vom Generator erzeugte Screens (nicht editieren)
             Zephyr-Applikation (west) — Firmware + Simulation
  drivers/ft813.c       Legacy: FT813 als RGB565-Framebuffer (nicht stm32)
  drivers/lv_draw_eve_zephyr.*  DRAW_EVE SPI-Glue + Touch (stm32h573i_dk)
  src/                  CANopen-Stack, SDO-Worker, Berry, main
  boards/, overlays/    stm32h573i_dk + native_sim Konfiguration
  berry/berry_conf.h    Berry-Konfiguration
Middlewares/
  CANopen/CANopenNode   CANopenNode v4 (Submodul, protronic-Fork)
  Third_Party/berry     Berry-Skriptsprache (Submodul)
eds/                    network.json + EDS-Dateien des Netzwerks
poc/                    hall_config.json + can_config.json (PoC-Hallen-UI)
tools/eds2lvgl.py       EDS→LVGL-Codegenerator
tools/poc2lvgl.py       PoC-JSON→LVGL/CAN-Codegenerator
west.yml                west-Manifest (Zephyr v4.4.2 + Module)
```
