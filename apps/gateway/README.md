# apps/gateway — NDJSON-Gateway für Webapps

CANboss als **Brücke zwischen Browser und CANopen-Netz**: eine UART
(WebSerial-tauglich) transportiert zeilenweise JSON in beide
Richtungen. Vier Funktionsblöcke:

1. **`gtwa`** — CiA-309-3-ASCII-Kommandos, ausgeführt vom
   CANopenNode-Gateway (SDO-Client + NMT-Master):

   ```json
   {"gtwa": ["0 preop", "[1] 1 w 0x2500 2 u32 123", "[2] 1 r 0x2500 2 u32", "0 start"]}
   ```

   Antworten kommen asynchron zeilenweise zurück, korreliert über die
   `[sequence]`-Nummern: `{"gtwa": ["[1] OK"]}`, `{"gtwa": ["[2] 123"]}`.
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

4. **`repl`** — Berry-Code ausführen (Kconfig `CANBOSS_GW_BERRY`,
   Default an): dieselbe VM samt `od_*`-Bindings wie Monitor/Panel,
   Ausgabe zeilenweise zurück:

   ```json
   {"repl": "od_read(16, 0x1017, 0)"}   → {"repl": ["1000"]}, {"repl": {"ok": true}}
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

### STM32H573I-DK (FDCAN2 am Arduino-Header, wie apps/touch)

```bash
west build -b stm32h573i_dk CANboss/apps/gateway -d build-gw-h573
west flash -d build-gw-h573
```

Der NDJSON-Strom läuft auf der **ST-Link-VCP-UART** (115200) — im
Browser direkt als serieller Port sichtbar, keine Treiber nötig.
Konsole/Logging sind auf diesem Target deshalb abgeschaltet
(`boards/stm32h573i_dk.conf`). Firmware-Slots liegen im RAM
(`CANBOSS_GW_FW_SLOTS`/`_SLOT_SIZE`, Default 1 × 64 KiB) und sind nach
Reset leer — persistente Slots liefert das Flash-Backend von
canBLEberry (gleiches `jsonapi_fw_ops`-Interface).

## Einordnung

- Die Gateway-Logik steckt komplett in [`lib/jsonapi`](../../lib/jsonapi)
  und ist transport-agnostisch (Byte-Senke + `jsonapi_input()`), damit
  canBLEberry dasselbe Protokoll über **BLE** anbieten kann (STM32WBA6
  hat kein USB-Device am DK-VCP-Weg vorbei; dort ist NUS/GATT der Kanal).
- Das CANopenNode-Gateway teilt sich `SDOclient[0]` mit
  `cb_co_sdo_read/_write/_write_stream`; der jsonapi-Dispatcher
  arbeitet Requests sequenziell ab und vermeidet so Kollisionen.
  Weitere SDO-Nutzer (z. B. eine parallel laufende Berry-REPL) müssen
  selbst darauf achten.
- `0 preop`/`0 start` etc. sind NMT-Master-Kommandos (Node 0 =
  alle Knoten); das Gateway läuft standardmäßig als Node 126, um neben
  Monitor/Panel (127) existieren zu können.
