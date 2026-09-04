# apps/gateway — NDJSON-Gateway für Webapps

CANboss als **Brücke zwischen Browser und CANopen-Netz**: eine UART
(WebSerial-tauglich) transportiert zeilenweise JSON in beide
Richtungen — auf dem STM32H573I-DK ist das der USB-Stecker des Boards
als CDC-ACM. Zeilen **ohne** führende `{` sind SLCAN-ASCII (unten).
Fünf Funktionsblöcke:

1. **`gtwa`** — CiA-309-3-ASCII-Kommandos, ausgeführt vom
   CANopenNode-Gateway (SDO-Client + NMT-Master):

   ```json
   {"gtwa": ["0 preop", "[1] 1 w 0x2500 2 u32 123", "[2] 1 r 0x2500 2 u32", "0 start"]}
   ```

   Jede Antwort ist ein Objekt mit der Sequenz als Key: Zahlen unquoted
   (`{"gtwa": {"2": 123}}`), sonst String (`{"gtwa": {"1": "OK"}}`).
   Ein Array wird in Batches zu je `batmax` Kommandos gesplittet,
   jede Teilausgabe eine Zeile `{"gtwa": {"1": "OK", "2": 123}}`.
   Default `batmax` ist 1; setzen mit `{"gtwa": {"batmax": n}}`
   (1…16), sichtbar in `{"info":…}`. Autozähler:
   `{"gtwa": {"seq": n}}` setzt die nächste Nummer (0…9999).
   `{"gtwa": "help"}` listet die komplette Kommandosyntax.

2. **`mon`** — PDO/COB-Monitor: COB-IDs abonnieren, Frames kommen als
   Events (Ratenlimit je COB, Default 100 ms):

   ```json
   {"mon": {"add": ["0x181", 641], "rate": 100}}
   {"pdo": {"cob": 385, "d": "1122334455667788", "t": 123456}}
   ```

3. **`fw`** — Firmware-Datei annehmen (Base64-Chunks) und per
   SDO-Block-Download streamend an einen Knoten schicken (CiA 302,
   Default `0x1F50:1`); der Fortschritt wird laufend gepostet:

   ```json
   {"fw": {"op": "begin", "slot": 0, "size": 46812, "name": "app.bin"}}
   {"fw": {"op": "data", "b64": "..."}}          → {"fw": {"ack": 2048}}
   {"fw": {"op": "end", "crc32": "aabbccdd"}}
   {"fw": {"op": "send", "slot": 0, "node": 16}} → {"fw": {"prog": [8192, 46812], ...}}
   ```

4. **SLCAN** — Zeilen ohne führende `{` gehen am JSON-Parser vorbei in
   die SLCAN-Brücke (Lawicel-ASCII wie CANUSB/slcand/python-can):
   Raw-CAN **parallel zum laufenden CANopen-Verkehr**, über dasselbe
   CAN-Interface. `O` öffnet den Kanal (empfangene Frames kommen als
   `t123 4 DEADBEEF`-Zeilen, CR-terminiert), `t.../r...` sendet,
   `C` schließt, `Z1` hängt Timestamps an, `V`/`N`/`F` antworten wie
   üblich; `S`/`M` & Co. werden angenommen und ignoriert (Bitrate und
   Filter gehören dem CANopen-Stack bzw. dem Devicetree). Extended
   Frames (`T`/`R`) meldet die Brücke als Fehler — die CAN-Schicht ist
   bewusst auf klassische 11-Bit-Frames beschränkt (CiA 301).
   Ein Host, der nur SLCAN spricht, sieht auch nur SLCAN-Antworten —
   der Port funktioniert damit direkt an `slcand`/python-can. Nicht
   `/dev/ttyACM0` nehmen (am H573I-DK ist das die ST-LINK-VCP), sondern
   den User-USB-Port per Produktstring:

   ```bash
   sudo slcand -o -c /dev/serial/by-id/usb-Protronic_GmbH_CANboss_Gateway_SLCAN_*-if00 can0
   sudo ip link set can0 up
   candump can0        # CANopen-Verkehr live, waehrend das Gateway
                       # weiter als CANopen-Master arbeitet
   ```

   Optional legt [`udev/99-canboss-gateway.rules`](udev/99-canboss-gateway.rules)
   das Symlink `/dev/ttyCANboss` an.

   Protokolldetails: [`lib/jsonapi/jsonapi_slcan.h`](../../lib/jsonapi/jsonapi_slcan.h).

5. **`repl`** — Berry-Code ausführen (Kconfig `CANBOSS_GW_BERRY`,
   Default an): dieselbe VM samt `od_*`-Bindings wie Monitor/Panel,
   Ausgabe zeilenweise zurück:

   ```json
   {"repl": "od_read(16, 0x1017, 0)"}   → {"repl": 1000}
   ```

Vollständige Protokollreferenz: [`lib/jsonapi/jsonapi.h`](../../lib/jsonapi/jsonapi.h).
Bedienoberfläche: [`webapp/index.html`](webapp/index.html) (Chrome/Edge,
WebSerial; über HTTPS oder `http://localhost` öffnen).

## Bauen & Testen

### native_sim (vcan0, ohne Hardware)

```bash
west build -b native_sim/native/64 CANboss/apps/gateway -d build-gw
./build-gw/zephyr/zephyr.exe          # NDJSON auf stdin/stdout

# Beispiel:
echo '{"ping":1}' | ./build-gw/zephyr/zephyr.exe
# zusammen mit den Demo-Knoten (apps/nodes/*) auf vcan0:
echo '{"gtwa":["[1] 16 r 0x1017 0 u16"]}' | ./build-gw/zephyr/zephyr.exe
```

Interaktiv bietet sich `socat` an:
`socat - EXEC:./build-gw/zephyr/zephyr.exe`

### STM32H573I-DK (USB-CDC-ACM, FDCAN2 am Arduino-Header)

```bash
west build -b stm32h573i_dk CANboss/apps/gateway -d build-gw-h573
west flash -d build-gw-h573
```

Der NDJSON-Strom läuft über den **USB-Stecker des Boards**
(USB_DRD_FS an PA11/PA12, Board-Label `zephyr_udc0`) als CDC-ACM:
Kabel an den Rechner, `navigator.serial.requestPort()` zeigt
„CANboss Gateway SLCAN“, fertig — kein ST-Link im Datenpfad, keine
Treiberinstallation. Das Board meldet sich mit VID/PID `0483:5740`
(STMicroelectronics / STM32 Virtual COM Port), damit Filter wie
`{ usbVendorId: 0x0483 }` in LSM6 den Port sehen, Produktstring
`CANboss Gateway SLCAN` und einer Seriennummer aus der STM32-UID.
Die ST-LINK-VCP ist ebenfalls VID `0483` (PID `374e`) — im
WebSerial-Dialog den CANboss-Eintrag wählen, nicht den ST-LINK.
`lsusb` ohne `-v` zeigt den usb.ids-Namen zum VID/PID-Paar; die
Firmware-Strings stehen in `lsusb -v` und in `/dev/serial/by-id/`.
VID/PID/Strings sind über `CANBOSS_GW_USB_*` einstellbar (`Kconfig`).

Weil der JSON-Strom nicht mehr auf der VCP liegt, ist die
**ST-Link-VCP jetzt die Zephyr-Konsole samt Shell** (115200):
Boot-Banner, Statusmeldungen des Gateways und der
USB-Enumerations-Fortschritt (`usbd`-Stack auf INF) laufen dort auf —
praktisch für die Inbetriebnahme. Nur CDC-ACM bleibt stumm
(`boards/stm32h573i_dk.conf`): `usbd_cdc_acm.c` macht auf INF-Level
einen Hexdump *jedes* empfangenen Pakets; das wuerde die VCP und
jeden Firmware-Upload ausbremsen.

#### Monitoring über die Zephyr-Shell (ST-Link-VCP)

Terminal auf die ST-Link-VCP, 115200 Baud — `Enter` holt den
`uart:~$`-Prompt (Tab vervollständigt Kommandos und Gerätenamen):

```bash
picocom -b 115200 /dev/ttyACM0     # oder minicom -D /dev/ttyACM0 -b 115200
```

| Kommando | zeigt |
|---|---|
| `device list` | alle Treiberinstanzen + Init-Status (`can@4000a800`, `usb@40016000`, `cdc_acm_uart0`, …) |
| `can show can@4000a800` | CAN-Zustand: Bitrate, Modus, Error-Counter, Bus-Status (FDCAN2 = Arduino-Header) |
| `can stop/start can@4000a800` | CAN-Controller anhalten/starten (Vorsicht: trennt auch CANopen) |
| `kernel uptime` / `kernel version` | Laufzeit, Zephyr-Version |
| `kernel threads` | alle Threads mit Zustand und Stack-Verbrauch (CANopen-RX/-Mainline, `usbd`, `sysworkq`, `main`) |
| `usbd …` | USB-Device-Stack von Hand steuern (`enable`/`disable`, Deskriptoren) — fürs Debugging der Enumeration |
| `log …` | Log-Backends/Level zur Laufzeit umschalten |

Typischer Bring-up-Blick: nach dem Reset erscheinen Banner,
`CANboss-Gateway startet (Node-ID 126)`, `USB-Device aktiv … warte auf
Host`, `Berry-REPL bereit`, `CANopen laeuft (gtwa + SLCAN bereit)` und
`NDJSON-Loop laeuft`; beim Anstecken des User-USB-C folgen
`USBD: reset` → `USBD: configuration`. Bleibt eine der Zeilen aus,
zeigt die letzte sichtbare, in welchem Schritt es hängt — bitte dann
mit angeschlossenem Terminal den Reset-Taster drücken und die Ausgabe
mitnehmen. Ab da muss
das Gerät in `lsusb` stehen (`0483:5740`). Danach z. B. mit
`can show can@4000a800` prüfen, ob der Bus fehlerfrei läuft, und mit
`kernel threads` die Stacks im Blick behalten.

In der VCP-Variante (`overlays/stlink_vcp.*`) ist die Shell aus —
dort gehört `usart1` dem NDJSON-Strom.

#### Board taucht nicht in `lsusb` auf?

- **Richtiger Stecker?** Das Gateway hängt am **User-USB-C CN17** (am
  STM32-USB_FS, per TCPP03-M20 geschützt) — nicht am ST-LINK-Port
  (CN10). Zwei Kabel: ST-LINK für Flashen/Konsole, User-USB für
  NDJSON/WebSerial. LD7 an heißt nur VBUS, nicht Enumeration.
- **TCPP im NORMAL-Modus?** Der TCPP03-M20 auf I2C4 (PG0 = Enable)
  muss in den NORMAL-Modus, sonst bleibt D+/D- am CN17 tot — Konsole
  zeigt dann `USBD: Device suspended`, `lsusb` bleibt leer. Das
  erledigt zentral
  [`lib/usbc/usbc_h573_dk.c`](../../lib/usbc/usbc_h573_dk.c)
  (gemeinsam mit `apps/usbdemo`) vor `usbd_enable()`. Zephyrs
  Abschalten der Dead-Battery-Pull-downs (v4.4.2) ist dagegen
  unkritisch — per `apps/usbdemo` verifiziert, der frühere
  Rd-Workaround ist entfernt.
- **FDCAN1 muss aus sein:** Das Board-DTS aktiviert FDCAN1 auf
  **PA11/PA12**. Das sind gleichzeitig USB D-/D+. Mit `CONFIG_CAN=y`
  setzt der FDCAN1-Treiber diese Pins beim Boot auf CAN AF9; der
  USB-Pull-up bleibt sichtbar, aber Bus-Reset und Enumeration erreichen
  den USB-Controller nicht. Das Gateway-Overlay deaktiviert deshalb
  FDCAN1 explizit und nutzt nur FDCAN2 auf PB5/PB6.
- **USB-Takt:** CN17 nutzt HSI48 über `USBSEL=3`; der STM32-UDC-Treiber
  prüft mit `CONFIG_UDC_STM32_CLOCK_CHECK=y` auf exakt 48 MHz. Beim
  Pin-Konflikt waren HSI48 `on/ready` und USBSEL korrekt — der Takt war
  daher nicht die Ursache.
- **CAN ohne Gegenstelle blockiert nichts mehr:** FDCAN2 braucht einen
  **externen Transceiver** am Arduino-Header und mindestens einen
  zweiten Knoten, der ACKt. Ohne ACK retransmittiert der Controller
  endlos — früher hing damit jeder sendende Thread fest
  (`can_send()` ohne Callback wartet unbegrenzt auf TX-Complete),
  inklusive NDJSON-Poll. Seit dem asynchronen TX in
  `lib/canopen/can_zephyr.c` bleiben USB-CDC, Shell und NDJSON davon
  unabhängig; `can show can@4000a800` zeigt die wachsenden
  Error-Counter.
- **Zwei `ttyACM` — den richtigen erwischen!** Sobald das Gateway
  enumeriert, gibt es *zwei* CDC-Geräte, und die Nummerierung
  (`ttyACM0`/`ttyACM1`) kann nach Reset/Umstecken wechseln. Auf dem
  NDJSON-Port gibt es keinen `uart:~$`-Prompt (Enter = leere Zeile =
  ignoriert; Tippzeichen quittiert die SLCAN-Brücke mit einem
  unsichtbaren BEL). Stabil per ID ansprechen:

  ```bash
  ls -l /dev/serial/by-id/
  # ...STLINK-V3EC...-if02                 -> Konsole/Shell (picocom hier drauf)
  # ...CANboss_Gateway_SLCAN...-if00       -> NDJSON/WebSerial/slcand
  picocom -b 115200 /dev/serial/by-id/usb-STMicroelectronics_STLINK-V3EC_*-if02
  ```
- **Konsole mitlesen:** `picocom -b 115200 /dev/ttyACM0` (ST-LINK-VCP)
  zeigt nach Reset das Boot-Banner, `CANboss-Gateway startet …`,
  `USB-Device aktiv … warte auf Host` und beim Anstecken die
  `USBD: …`-Enumerationsmeldungen. Bleibt `USBD: configuration` aus,
  kommt am Host nichts an → Kabel/Stecker prüfen.

Braucht man nur ein Kabel (oder soll der USB-Device-Stack raus), geht
es zurück auf die VCP:

```bash
west build -b stm32h573i_dk CANboss/apps/gateway -d build-gw-h573 -- \
  -DEXTRA_DTC_OVERLAY_FILE=overlays/stlink_vcp.overlay \
  -DEXTRA_CONF_FILE=overlays/stlink_vcp.conf
```

Firmware-Slots liegen im RAM (`CANBOSS_GW_FW_SLOTS`/`_SLOT_SIZE`,
Default 1 × 64 KiB) und sind nach Reset leer — persistente Slots
liefert das Flash-Backend von canBLEberry (gleiches
`jsonapi_fw_ops`-Interface).

### Webapp öffnen

WebSerial gibt es nur in sicheren Kontexten (Chrome/Edge), also über
HTTPS oder `http://localhost`:

```bash
python3 -m http.server -d CANboss/apps/gateway/webapp 8000
# http://localhost:8000
```

Die Seite ist eine einzelne Datei ohne Abhängigkeiten und kann genauso
gut von einem beliebigen Webserver kommen. Neben der rohen
gtwa-Konsole bietet sie ein SDO-Formular (Node/Index/Sub/Typ, `r`/`w`
mit automatisch vergebener `[sequence]`), NMT-Knöpfe, den PDO-Monitor,
eine SLCAN-Karte (Kanal öffnen, Frames senden, Live-Tabelle der
empfangenen IDs) und den Firmware-Upload. Setzt das Board zurück, verschwindet der
CDC-ACM-Port und taucht neu auf — die Webapp verbindet sich über die
`connect`-Events von WebSerial von selbst wieder, ohne erneute
Portfreigabe.

## Einordnung

- Die Gateway-Logik steckt komplett in [`lib/jsonapi`](../../lib/jsonapi)
  und ist transport-agnostisch (Byte-Senke + `jsonapi_input()`). Welche
  UART den Strom trägt, entscheidet allein das chosen
  `canboss,jsonapi-uart` — CDC-ACM, ST-Link-VCP oder native_sim-PTY,
  `src/main.c` sieht keinen Unterschied. Dieselbe Trennung erlaubt es
  canBLEberry, das Protokoll über **BLE** anzubieten (STM32WBA6 hat
  kein USB-Device am DK-VCP-Weg vorbei; dort ist NUS/GATT der Kanal).
- Das CANopenNode-Gateway teilt sich `SDOclient[0]` mit
  `cb_co_sdo_read/_write/_write_stream`; der jsonapi-Dispatcher
  arbeitet Requests sequenziell ab und vermeidet so Kollisionen.
  Weitere SDO-Nutzer (z. B. eine parallel laufende Berry-REPL) müssen
  selbst darauf achten.
- `0 preop`/`0 start` etc. sind NMT-Master-Kommandos (Node 0 =
  alle Knoten); das Gateway läuft standardmäßig als Node 126, um neben
  Monitor/Panel (127) existieren zu können.
