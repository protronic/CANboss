# CANbossTouch

CANopen-Bediengerät auf Basis des **Riverdi 5.0" STM32U599 LVGL-Displays**:
Die LVGL-Widgets und -Screens werden **automatisch aus den EDS-Dateien**
(CAN-Datenpunkten) des Netzwerks generiert. Das Display zeigt die Datenpunkte
der verschiedenen Knoten an und kann sie per **SDO** parametrieren.

Kombiniert aus:

- [riverdi-50-stm32u5-lvgl](https://github.com/protronic/riverdi-50-stm32u5-lvgl) – Board-Support (STM32U599NJHxQ, LTDC-Display 800×480, Touch, FreeRTOS, LVGL 9.5)
- [CanOpenSTM32](https://github.com/protronic/canopenstm32) – CANopenNode-v4-Stack mit STM32-FDCAN-Port

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
| `ro` / `const` (alle Typen) | Wertanzeige (Label) |
| `BOOLEAN` `rw` | `lv_switch` |
| `INTEGER8..32`, `UNSIGNED8..32` `rw` | `lv_spinbox` mit Grenzen aus `LowLimit`/`HighLimit` |
| `VISIBLE_STRING` `rw` | `lv_textarea` mit Bildschirmtastatur |
| `REAL32` | Anzeige mit 3 Nachkommastellen |

Die SDO-Transfers laufen in einem eigenen FreeRTOS-Worker-Task
(`App/canboss_sdo.c`) über den CANopenNode-SDO-Client (OD 0x1280,
Expedited + Segmented); die LVGL-Oberfläche (`App/canboss_ui.c`) pollt die
Ergebnisse und bleibt dadurch komplett im LVGL-Task.

## Netzwerk beschreiben

`eds/network.json`:

```json
{
  "nodes": [
    { "node_id": 16, "name": "IO-Modul", "eds": "demo_io.eds" },
    { "node_id": 32, "name": "Antrieb",  "eds": "demo_drive.eds" },
    { "node_id": 48, "name": "DS301 Geraet", "eds": "DS301_profile.eds",
      "include": ["0x1000-0x1001", "0x1008-0x100A", "0x1017"] }
  ]
}
```

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
`Middlewares/CANopen/CANopenNode` (CANopenNode v4.1).

## Hardware / CAN

- **FDCAN1** auf PB8 (RX) / PB9 (TX), Classic CAN **500 kbit/s**
  (160 MHz Kernel-Takt, Prescaler 16, 20 tq, Samplepoint 80 % –
  anpassbar in `Core/Src/fdcan.c`)
- **TIM7** liefert den 1-ms-Takt für den CANopen-Stack
  (`canopen_app_interrupt()`)
- Eigene Node-ID des Panels: `CANBOSS_NODE_ID` (Default **127**,
  in `Core/Inc/main.h`)
- Das Panel ist selbst ein vollwertiger CANopen-Knoten
  (DS301-OD in `App/OD/`, NMT/Heartbeat/SDO-Server/LSS-Slave)

## Projektstruktur

```
App/                    Anwendung
  canboss_ui.c/h        LVGL-Laufzeit: Menü, Widget-Fabriken, SDO-Refresh
  canboss_sdo.c/h       SDO-Client-Worker (FreeRTOS-Task + Queue)
  canboss_types.h       gemeinsame Typen der generierten Tabellen
  CO_driver_custom.h    CANopenNode-Konfiguration (SDO-Client, LSS, FIFO)
  OD/                   Objektverzeichnis des Panels (DS301)
  generated/            ⚙ vom Generator erzeugte Screens (nicht editieren)
Core/                   CubeMX-Code (STM32U599, Peripherie, FreeRTOS-Start)
Drivers/                STM32U5 HAL + CMSIS
Middlewares/
  CANopen/CANopenNode        CANopenNode v4 (Submodul)
  CANopen/CANopenNode_STM32  STM32-FDCAN-Port
  Third_Party/LVGL           lv_conf.h + LVGL (Submodul)
  Third_Party/FreeRTOS       FreeRTOS-Kernel + CMSIS-RTOS2
eds/                    network.json + EDS-Dateien des Netzwerks
tools/eds2lvgl.py       EDS→LVGL-Codegenerator
Startup/, *.ld          Startup-Code und Linkerskript (STM32U599NJHxQ)
Makefile                Build mit gnu-tools-for-stm32
```

Hinweis: `CANbossTouch.ioc` dient nur als Referenz der Pin-/Takt-
Konfiguration (vom Riverdi-Projekt übernommen). Maßgeblich für den Build
ist das Makefile; ein CubeMX-Regenerieren überschreibt die
CAN-/TIM7-Anpassungen in `Core/` nicht vollständig korrekt.
