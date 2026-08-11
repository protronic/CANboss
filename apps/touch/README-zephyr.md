# CANbossTouch auf Zephyr (west + LVGL + Berry)

Die CANbossTouch-Firmware auf Zephyr RTOS: dieselbe LVGL-UI
(EDS-generierte Screens aus `app/`, PoC-Hallenlicht) und derselbe
CANopenNode-Stack, gebaut mit `west` — als Firmware fuer das
**STM32H573I-DK** (FT813-Display am SPI2, FDCAN2) und als `native_sim`
(Simulation am PC, SDL-Fenster). Der fruehere FreeRTOS/STM32-HAL-Build
liegt in der Git-Historie.

Dazu kommt **Berry-Scripting** (Submodul
`modules/berry`): eine Skript-VM mit direktem Zugriff
auf das Objektverzeichnis und die PDO-Daten des Netzwerks, bedienbar
ueber das Shell-Kommando `berry`.

## Aufbau

```

  CMakeLists.txt        Zephyr-App: App/-Quellen + CANopenNode + Berry
  prj.conf, Kconfig     Konfiguration (CONFIG_CANBOSSTOUCH_*)
  boards/stm32h573i_dk.* FT813 an SPI2 + DRAW_EVE, FDCAN2, RAM-Tuning
  boards/native_sim.*   SDL-Display 800x480, Shell auf stdin/stdout
  overlays/headless.*   Dummy-Display (CI/Container ohne SDL2)
  overlays/vcan.overlay CAN auf Host-SocketCAN statt Loopback
  drivers/lv_draw_eve_zephyr.*  LVGL DRAW_EVE SPI/GPIO-Glue (FT813) +
                        Touch-Indev (native CTSE, Achsen transponiert)
  drivers/ft813.c       Legacy-Framebuffer-Treiber (optional)
  port/                 CANopenNode-Port (CO_driver auf can_if/osal)
  berry/berry_conf.h    Berry-Konfiguration (ohne Dateisystem/OS-Modul)
  src/
    main.c                    Einstieg: CANopen + SDO-Worker + UI + Berry
    co_zephyr.[ch]            Stack-Anbindung (lib/od/canboss_master, Node 127)
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
west init -m https://github.com/protronic/CANboss
west update
git -C CANboss submodule update --init

# Simulation am PC (LVGL-Fenster via SDL2, libsdl2-dev noetig):
ZEPHYR_TOOLCHAIN_VARIANT=host west build -b native_sim/native/64 CANboss/apps/touch
./build/zephyr/zephyr.exe
```

Bereits geklont? Dann den Workspace um den vorhandenen Klon herum
anlegen (`west build` gibt es erst innerhalb eines Workspace):

```bash
cd ..                       # ins Verzeichnis UEBER dem Klon
west init -l CANboss   # Klon als Manifest-Repo registrieren
west update                 # holt zephyr + Module daneben
```

Ein daneben liegender CANboss-Klon baut im selben Workspace mit
(`west build -b native_sim/native/64 CANboss`).

Ohne SDL2 (CI/Container) laeuft die UI unsichtbar auf einem
Dummy-Display weiter:

```bash
west build -b native_sim/native/64 CANboss/apps/touch -- \
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

## Bauen und flashen (STM32H573I-DK)

Zephyr **4.4+** braucht SDK **1.0** (Arch/CachyOS: AUR `zephyr-sdk`
→ `/opt/zephyr-sdk`). Kurzfassung auch in der [Root-README](../../README.md):

```bash
# mit dem Zephyr-SDK (empfohlen):
export ZEPHYR_SDK_INSTALL_DIR=/opt/zephyr-sdk   # Arch/CachyOS-AUR-Pfad
unset ZEPHYR_TOOLCHAIN_VARIANT
west build -b stm32h573i_dk CANboss/apps/touch
# Arch: Zephyr sucht STM32_Programmer_CLI (Case-sensitiv); AUR-Paket
# stellt nur stm32_programmer_cli bereit → PATH oder --cli setzen:
export PATH=/opt/stm32cubeprog/bin:$PATH
west flash
# west flash --cli=/opt/stm32cubeprog/bin/STM32_Programmer_CLI

# alternativ mit Distro-Paketen (arm-none-eabi-gcc + picolibc):
ZEPHYR_TOOLCHAIN_VARIANT=gnuarmemb GNUARMEMB_TOOLCHAIN_PATH=/usr \
  west build -b stm32h573i_dk CANboss/apps/touch -- -DTOOLCHAIN_HAS_PICOLIBC=ON
```

Das Board-Overlay (`boards/stm32h573i_dk.overlay`) bindet das
gen4-FT813-Modul am Arduino-Header an (SPI2 PI1/PI2/PB15, /CS PA3,
PD PA8 — Verdrahtung siehe README im Repo-Root) und CANopen an FDCAN2
(PB5/PB6, 500 kbit/s). Die UI laeuft ueber **LVGL DRAW_EVE**
(`CONFIG_CANBOSSTOUCH_DRAW_EVE`); `zephyr,display` ist nur ein Dummy
fuer das Zephyr-LVGL-Modul. Heaps/Stacks in
`boards/stm32h573i_dk.conf` — ohne grossen RGB565-VDB deutlich
entspannter als der fruehere Framebuffer-Pfad.

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
`berry/tools/coc` (Python) nach `modules/berry/generate/`
erzeugt (Konfiguration: `berry/berry_conf.h`).

## Verifiziert (native_sim, headless)

- LVGL-UI (Hauptmenue aus der EDS-Registry) laeuft auf dem Dummy-Display,
  SDL-Variante baugleich
- SDO end-to-end ueber den CAN-Loopback: `od_read`/`od_write`/`od_reads`
  gegen den eigenen SDO-Server (expedited + segmented)
- Berry-Shell inkl. Schleifen, Variablen, Fehlermeldungen (SDO-Abort
  als Berry-Exception)

## Offene Punkte

- Hardware-Bring-up am realen STM32H573I-DK (der Board-Build ist
  kompiliert/gelinkt, aber noch nicht auf dem Geraet gelaufen);
  RAM-Feintuning, ggf. LVGL-Puffer nach SRAM2/3
- Berry-Autostart-Skript (z.B. aus Settings/Flash) und LVGL-Konsole
  fuer Berry direkt am Panel
- PoC-Hallenlicht: minp-Feedback ueber vcan gegen die Demo-Umgebung
  testen
