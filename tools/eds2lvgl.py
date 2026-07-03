#!/usr/bin/env python3
"""
eds2lvgl.py - generiert LVGL-Screens und -Widgets aus CANopen-EDS-Dateien.

Liest eds/network.json (Knotenliste des CAN-Netzwerks) und die dort
referenzierten EDS-Dateien (CiA 306). Fuer jeden Knoten wird eine
Datenpunkt-Tabelle und eine LVGL-Screen-Aufbau-Funktion als C-Code nach
App/generated/ erzeugt:

  - canboss_gen_node_<id>.c   Datenpunkte + Screen des Knotens
  - canboss_gen_registry.c    Registry aller Knoten (canboss_nodes[])
  - canboss_gen.h             Deklarationen

Widget-Zuordnung (DataType/AccessType aus der EDS):

  AccessType ro/const  -> Wertanzeige (Label, zyklischer SDO-Upload)
  BOOLEAN rw           -> lv_switch      (SDO-Download bei Aenderung)
  INTEGER/UNSIGNED rw  -> lv_spinbox     (Grenzen aus LowLimit/HighLimit)
  VISIBLE_STRING rw    -> lv_textarea    (Bildschirmtastatur, Enter=Schreiben)
  sonstige rw          -> Wertanzeige (nur lesen)

Aufruf:  python3 tools/eds2lvgl.py [--network eds/network.json]
                                   [--out App/generated]
"""

import argparse
import configparser
import json
import re
import sys
from pathlib import Path

# CANopen DataType (CiA 301) -> canboss_dtype_t
DTYPE_MAP = {
    0x01: "CB_DT_BOOL",
    0x02: "CB_DT_I8",
    0x03: "CB_DT_I16",
    0x04: "CB_DT_I32",
    0x05: "CB_DT_U8",
    0x06: "CB_DT_U16",
    0x07: "CB_DT_U32",
    0x08: "CB_DT_F32",
    0x09: "CB_DT_STR",
    0x0A: "CB_DT_OCTET",
    0x15: "CB_DT_I64",
    0x1B: "CB_DT_U64",
}
INT_DTYPES = {0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x15, 0x1B}

# Vorgabe-Grenzen je Datentyp (fuer Spinbox, wenn die EDS keine Limits nennt)
DTYPE_RANGE = {
    0x02: (-128, 127),
    0x03: (-32768, 32767),
    0x04: (-2147483648, 2147483647),
    0x05: (0, 255),
    0x06: (0, 65535),
    0x07: (0, 2147483647),        # UNSIGNED32: auf int32-Bereich der Spinbox gekappt
    0x15: (-2147483648, 2147483647),
    0x1B: (0, 2147483647),
}

ACCESS_MAP = {
    "ro": "CB_ACC_RO",
    "const": "CB_ACC_RO",
    "wo": "CB_ACC_WO",
    "rw": "CB_ACC_RW",
    "rww": "CB_ACC_RW",
    "rwr": "CB_ACC_RW",
}

OBJ_SECTION_RE = re.compile(r"^([0-9A-Fa-f]{4})(?:sub([0-9A-Fa-f]{1,2}))?$")

# Standardmaessig aufgenommene Indexbereiche, wenn network.json nichts angibt:
# Geraeteinfo + Fehlerregister + Hersteller- und Profilbereich.
DEFAULT_INCLUDE = ["0x1000-0x1001", "0x1008-0x100A", "0x2000-0x9FFF"]


def parse_int(value, default=None):
    """EDS-Zahlen: dezimal, 0x-Hex oder leere Strings."""
    if value is None:
        return default
    v = str(value).strip()
    if not v:
        return default
    # NodeID-Formeln ($NODEID+0x...) betreffen COB-IDs, nicht unsere Werte
    if "$" in v:
        return default
    try:
        return int(v, 0)
    except ValueError:
        return default


def parse_ranges(spec_list):
    ranges = []
    for spec in spec_list:
        s = str(spec).replace(" ", "")
        if "-" in s:
            lo, hi = s.split("-", 1)
            ranges.append((int(lo, 0), int(hi, 0)))
        else:
            ranges.append((int(s, 0), int(s, 0)))
    return ranges


def in_ranges(idx, ranges):
    return any(lo <= idx <= hi for lo, hi in ranges)


def c_escape(s):
    return s.replace("\\", "\\\\").replace('"', '\\"')


def c_ident(s):
    return re.sub(r"[^A-Za-z0-9_]", "_", s)


class DataPoint:
    def __init__(self, index, sub, name, dtype, access, low, high):
        self.index = index
        self.sub = sub
        self.name = name
        self.dtype = dtype
        self.access = access
        self.low = low
        self.high = high

    def widget(self):
        """Widget-Auswahl -> Name der canboss_ui-Fabrikfunktion."""
        if self.access == "CB_ACC_RW":
            if self.dtype == "CB_DT_BOOL":
                return "canboss_ui_add_switch_row"
            if DTYPE_MAP_INV[self.dtype] in INT_DTYPES:
                return "canboss_ui_add_spinbox_row"
            if self.dtype == "CB_DT_STR":
                return "canboss_ui_add_text_row"
        return "canboss_ui_add_value_row"


DTYPE_MAP_INV = {v: k for k, v in DTYPE_MAP.items()}


def read_eds(path):
    cp = configparser.ConfigParser(interpolation=None, strict=False)
    cp.optionxform = str  # Gross-/Kleinschreibung der Keys erhalten
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        cp.read_file(f)
    # Keys case-insensitiv nachschlagen
    def get(section, key, default=None):
        for k in cp[section]:
            if k.lower() == key.lower():
                return cp[section][k]
        return default
    return cp, get


def extract_datapoints(eds_path, include_ranges):
    cp, get = read_eds(eds_path)

    objects = {}   # index -> Sektionsname (Hauptobjekt)
    subs = {}      # (index, sub) -> Sektionsname

    for section in cp.sections():
        m = OBJ_SECTION_RE.match(section.strip())
        if not m:
            continue
        idx = int(m.group(1), 16)
        if m.group(2) is None:
            objects[idx] = section
        else:
            subs[(idx, int(m.group(2), 16))] = section

    dps = []
    for idx in sorted(objects):
        if not in_ranges(idx, include_ranges):
            continue
        section = objects[idx]
        obj_name = get(section, "ParameterName", f"Object {idx:04X}") or f"Object {idx:04X}"
        obj_type = parse_int(get(section, "ObjectType"), 0x07)

        sub_sections = sorted(k[1] for k in subs if k[0] == idx)

        if obj_type == 0x07 or not sub_sections:  # VAR
            dp = make_dp(idx, 0, obj_name, section, get)
            if dp:
                dps.append(dp)
            continue

        # ARRAY (0x08) / RECORD (0x09): Subindizes 1..n aufnehmen,
        # Subindex 0 (Anzahl Eintraege) ist reine Verwaltungsinfo.
        for sub in sub_sections:
            if sub == 0:
                continue
            ssec = subs[(idx, sub)]
            sname = get(ssec, "ParameterName", f"Sub {sub}") or f"Sub {sub}"
            label = f"{obj_name}: {sname}" if sname.lower() not in obj_name.lower() else sname
            dp = make_dp(idx, sub, label, ssec, get)
            if dp:
                dps.append(dp)
    return dps


def make_dp(idx, sub, name, section, get):
    dtype_raw = parse_int(get(section, "DataType"))
    if dtype_raw is None or dtype_raw not in DTYPE_MAP:
        return None  # DOMAIN, komplexe Typen usw. ueberspringen
    access_raw = (get(section, "AccessType", "ro") or "ro").strip().lower()
    access = ACCESS_MAP.get(access_raw, "CB_ACC_RO")

    low = parse_int(get(section, "LowLimit"))
    high = parse_int(get(section, "HighLimit"))
    if dtype_raw in INT_DTYPES:
        dlo, dhi = DTYPE_RANGE[dtype_raw]
        low = dlo if low is None else max(low, dlo)
        high = dhi if high is None else min(high, dhi)
    else:
        low, high = None, None

    return DataPoint(idx, sub, name.strip(), DTYPE_MAP[dtype_raw], access, low, high)


HEADER_TEMPLATE = """\
/* Automatisch generiert von tools/eds2lvgl.py - NICHT von Hand editieren.
 * Quelle: eds/network.json + EDS-Dateien. Neu erzeugen mit: make gen */

#ifndef CANBOSS_GEN_H_
#define CANBOSS_GEN_H_

#include "canboss_types.h"

#ifdef __cplusplus
extern "C" {{
#endif

{externs}

#ifdef __cplusplus
}}
#endif

#endif /* CANBOSS_GEN_H_ */
"""

NODE_TEMPLATE = """\
/* Automatisch generiert von tools/eds2lvgl.py - NICHT von Hand editieren.
 * Knoten {node_id} "{name}" aus {eds}
 * {dp_count} Datenpunkte. Neu erzeugen mit: make gen */

#include "canboss_gen.h"
#include "canboss_ui.h"

static const canboss_dp_t {ident}_dps[] = {{
{dp_rows}
}};

static void {ident}_screen_create(void);

const canboss_node_desc_t {ident} = {{
    .node_id = {node_id},
    .name = "{name}",
    .eds_file = "{eds}",
    .dps = {ident}_dps,
    .dp_count = {dp_count},
    .screen_create = {ident}_screen_create,
}};

/* LVGL-Screen des Knotens: eine Widget-Zeile je EDS-Datenpunkt */
static void
{ident}_screen_create(void) {{
    lv_obj_t* cont = canboss_ui_screen_begin(&{ident});
{widget_rows}
}}
"""

REGISTRY_TEMPLATE = """\
/* Automatisch generiert von tools/eds2lvgl.py - NICHT von Hand editieren.
 * Registry aller Knoten aus eds/network.json. Neu erzeugen mit: make gen */

#include "canboss_gen.h"

const canboss_node_desc_t* const canboss_nodes[] = {{
{entries}
}};

const uint16_t canboss_node_count = {count};
"""


def generate(network_path, out_dir):
    network_path = Path(network_path)
    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    eds_dir = network_path.parent

    with open(network_path, "r", encoding="utf-8") as f:
        network = json.load(f)

    nodes = network.get("nodes", [])
    if not nodes:
        sys.exit("eds2lvgl: keine Knoten in network.json")

    externs = []
    registry = []
    generated = []

    for node in nodes:
        node_id = int(node["node_id"])
        if not 1 <= node_id <= 127:
            sys.exit(f"eds2lvgl: ungueltige node_id {node_id}")
        name = node.get("name", f"Node {node_id}")
        eds_file = node["eds"]
        include = parse_ranges(node.get("include", DEFAULT_INCLUDE))

        dps = extract_datapoints(eds_dir / eds_file, include)
        if not dps:
            print(f"  WARNUNG: {eds_file}: keine Datenpunkte im Include-Bereich", file=sys.stderr)

        ident = f"canboss_node_{node_id}"
        dp_rows = []
        widget_rows = []
        for i, dp in enumerate(dps):
            lo = dp.low if dp.low is not None else 0
            hi = dp.high if dp.high is not None else 0
            has_limits = 1 if dp.low is not None else 0
            dp_rows.append(
                f'    {{ .index = 0x{dp.index:04X}, .sub = 0x{dp.sub:02X}, '
                f'.dtype = {dp.dtype}, .access = {dp.access}, '
                f'.has_limits = {has_limits}, .name = "{c_escape(dp.name)}", '
                f'.min = {lo}, .max = {hi} }},'
            )
            widget_rows.append(
                f"    {dp.widget()}(cont, &{ident}, &{ident}_dps[{i}]); "
                f"/* 0x{dp.index:04X}.{dp.sub:02X} {c_escape(dp.name)} */"
            )

        node_c = NODE_TEMPLATE.format(
            node_id=node_id,
            name=c_escape(name),
            eds=c_escape(eds_file),
            ident=ident,
            dp_count=len(dps),
            dp_rows="\n".join(dp_rows) if dp_rows else "    {0}",
            widget_rows="\n".join(widget_rows) if widget_rows else "    (void)cont;",
        )
        out_file = out_dir / f"canboss_gen_node_{node_id}.c"
        out_file.write_text(node_c, encoding="utf-8")
        generated.append(out_file)

        externs.append(f"extern const canboss_node_desc_t {ident};")
        registry.append(f"    &{ident},")
        print(f"  Node {node_id:3d} \"{name}\": {len(dps)} Datenpunkte aus {eds_file} -> {out_file.name}")

    (out_dir / "canboss_gen.h").write_text(
        HEADER_TEMPLATE.format(externs="\n".join(externs)), encoding="utf-8")
    (out_dir / "canboss_gen_registry.c").write_text(
        REGISTRY_TEMPLATE.format(entries="\n".join(registry), count=len(nodes)), encoding="utf-8")

    # Verwaiste Node-Dateien frueherer Laeufe entfernen
    keep = {p.name for p in generated} | {"canboss_gen.h", "canboss_gen_registry.c"}
    for old in out_dir.glob("canboss_gen_node_*.c"):
        if old.name not in keep:
            old.unlink()
            print(f"  entfernt: {old.name}")

    print(f"eds2lvgl: {len(nodes)} Knoten generiert -> {out_dir}")


def main():
    ap = argparse.ArgumentParser(description="LVGL-Screens aus CANopen-EDS-Dateien generieren")
    ap.add_argument("--network", default="eds/network.json", help="Netzwerkbeschreibung (JSON)")
    ap.add_argument("--out", default="App/generated", help="Ausgabeverzeichnis")
    args = ap.parse_args()
    generate(args.network, args.out)


if __name__ == "__main__":
    main()
