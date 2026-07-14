#!/usr/bin/env python3
"""
eds2tui.py - generiert Datenpunkt-Tabellen fuer die CANboss-Terminal-UI
aus CANopen-EDS-Dateien (Ableger von CANbossTouch/tools/eds2lvgl.py,
ohne LVGL-Abhaengigkeit).

Liest eds/network.json (Knotenliste des CAN-Netzwerks) und die dort
referenzierten EDS-Dateien (CiA 306). Fuer jeden Knoten werden eine
Objekt- und eine Datenpunkt-Tabelle als C-Code nach src/gen/ erzeugt:

  - canboss_net_node_<id>.c   Objekte + Datenpunkte des Knotens
  - canboss_net_registry.c    Registry aller Knoten (cb_net_nodes[])
  - canboss_net.h             Deklarationen

Die Objekt-Tabelle gruppiert die Datenpunkte je Index (linke Spalte der
Monitor-Ansicht), die Datenpunkt-Tabelle enthaelt je (Index, Subindex)
Name, Datentyp, Zugriffsart und EDS-DefaultValue (rechte Spalte).

Aufruf:  python3 tools/eds2tui.py [--network eds/network.json]
                                  [--out src/gen]
"""

import argparse
import configparser
import json
import re
import sys
from pathlib import Path

# CANopen DataType (CiA 301) -> cb_dtype_t
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


def read_eds(path):
    cp = configparser.ConfigParser(interpolation=None, strict=False)
    cp.optionxform = str  # Gross-/Kleinschreibung der Keys erhalten
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        cp.read_file(f)

    def get(section, key, default=None):
        for k in cp[section]:
            if k.lower() == key.lower():
                return cp[section][k]
        return default

    return cp, get


def file_info(get, cp):
    """Produktname/Hersteller aus [DeviceInfo] (fuer die Netzwerk-Liste)."""
    product, vendor = "", ""
    if cp.has_section("DeviceInfo"):
        product = get("DeviceInfo", "ProductName", "") or ""
        vendor = get("DeviceInfo", "VendorName", "") or ""
    return product.strip(), vendor.strip()


def make_dp(idx, sub, name, section, get):
    dtype_raw = parse_int(get(section, "DataType"))
    if dtype_raw is None or dtype_raw not in DTYPE_MAP:
        return None  # DOMAIN, komplexe Typen usw. ueberspringen
    access_raw = (get(section, "AccessType", "ro") or "ro").strip().lower()
    access = ACCESS_MAP.get(access_raw, "CB_ACC_RO")
    default = (get(section, "DefaultValue", "") or "").strip()
    if "$" in default:  # $NODEID-Formeln nicht als Anzeigewert verwenden
        default = ""
    return {
        "index": idx,
        "sub": sub,
        "name": name.strip(),
        "dtype": DTYPE_MAP[dtype_raw],
        "access": access,
        "default": default,
    }


def extract_objects(eds_path, include_ranges):
    """Liefert Liste von Objekten: {index, name, dps: [dp, ...]}."""
    cp, get = read_eds(eds_path)

    objects = {}  # index -> Sektionsname (Hauptobjekt)
    subs = {}     # (index, sub) -> Sektionsname

    for section in cp.sections():
        m = OBJ_SECTION_RE.match(section.strip())
        if not m:
            continue
        idx = int(m.group(1), 16)
        if m.group(2) is None:
            objects[idx] = section
        else:
            subs[(idx, int(m.group(2), 16))] = section

    objs = []
    for idx in sorted(objects):
        if not in_ranges(idx, include_ranges):
            continue
        section = objects[idx]
        obj_name = get(section, "ParameterName", f"Object {idx:04X}") or f"Object {idx:04X}"
        obj_type = parse_int(get(section, "ObjectType"), 0x07)

        sub_sections = sorted(k[1] for k in subs if k[0] == idx)
        dps = []

        if obj_type == 0x07 or not sub_sections:  # VAR
            dp = make_dp(idx, 0, obj_name, section, get)
            if dp:
                dps.append(dp)
        else:
            # ARRAY (0x08) / RECORD (0x09): alle Subindizes inkl. 0
            # (Subindex 0 = Anzahl Eintraege, per SDO lesbar wie im
            # CouchDB-Proto des Rust-Originals).
            for sub in sub_sections:
                ssec = subs[(idx, sub)]
                sname = get(ssec, "ParameterName", f"Sub {sub}") or f"Sub {sub}"
                dp = make_dp(idx, sub, sname, ssec, get)
                if dp:
                    dps.append(dp)

        if dps:
            objs.append({"index": idx, "name": obj_name.strip(), "dps": dps})
    return objs, file_info(get, cp)


HEADER_TEMPLATE = """\
/* Automatisch generiert von tools/eds2tui.py - NICHT von Hand editieren.
 * Quelle: eds/network.json + EDS-Dateien. Neu erzeugen mit: make gen */

#ifndef CANBOSS_NET_H_
#define CANBOSS_NET_H_

#include "canboss.h"

#ifdef __cplusplus
extern "C" {{
#endif

{externs}

#ifdef __cplusplus
}}
#endif

#endif /* CANBOSS_NET_H_ */
"""

NODE_TEMPLATE = """\
/* Automatisch generiert von tools/eds2tui.py - NICHT von Hand editieren.
 * Knoten {node_id} "{name}" aus {eds}
 * {obj_count} Objekte, {dp_count} Datenpunkte. Neu erzeugen mit: make gen */

#include "canboss_net.h"

static const cb_dp_t {ident}_dps[] = {{
{dp_rows}
}};

static const cb_obj_t {ident}_objs[] = {{
{obj_rows}
}};

const cb_node_desc_t {ident} = {{
    .node_id = {node_id},
    .name = "{name}",
    .eds_file = "{eds}",
    .product_name = "{product}",
    .vendor_name = "{vendor}",
    .objs = {ident}_objs,
    .obj_count = {obj_count},
    .dps = {ident}_dps,
    .dp_count = {dp_count},
}};
"""

REGISTRY_TEMPLATE = """\
/* Automatisch generiert von tools/eds2tui.py - NICHT von Hand editieren.
 * Registry aller Knoten aus eds/network.json. Neu erzeugen mit: make gen */

#include "canboss_net.h"

const cb_node_desc_t* const cb_net_nodes[] = {{
{rows}
}};

const uint16_t cb_net_node_count = {count};
"""


def gen_node(node, objs, product, vendor):
    ident = f"cb_net_node_{node['node_id']}"
    dp_rows = []
    obj_rows = []
    dp_pos = 0
    for obj in objs:
        obj_rows.append(
            '    {{ .index = 0x{:04X}, .name = "{}", .dp_first = {}, .dp_count = {} }},'.format(
                obj["index"], c_escape(obj["name"]), dp_pos, len(obj["dps"])
            )
        )
        for dp in obj["dps"]:
            dp_rows.append(
                '    {{ .index = 0x{:04X}, .sub = 0x{:02X}, .dtype = {}, .access = {}, '
                '.name = "{}", .default_value = "{}" }},'.format(
                    dp["index"], dp["sub"], dp["dtype"], dp["access"],
                    c_escape(dp["name"]), c_escape(dp["default"])
                )
            )
            dp_pos += 1

    return ident, NODE_TEMPLATE.format(
        node_id=node["node_id"],
        name=c_escape(node["name"]),
        eds=c_escape(node["eds"]),
        product=c_escape(product),
        vendor=c_escape(vendor),
        ident=ident,
        obj_count=len(objs),
        dp_count=dp_pos,
        dp_rows="\n".join(dp_rows),
        obj_rows="\n".join(obj_rows),
    )


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--network", default="eds/network.json")
    ap.add_argument("--out", default="src/gen")
    args = ap.parse_args()

    network_path = Path(args.network)
    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)

    with open(network_path, "r", encoding="utf-8") as f:
        network = json.load(f)

    eds_dir = network_path.parent
    externs = []
    registry = []

    for node in network["nodes"]:
        include = parse_ranges(node.get("include", DEFAULT_INCLUDE))
        eds_path = eds_dir / node["eds"]
        objs, (product, vendor) = extract_objects(eds_path, include)
        if not objs:
            print(f"WARNUNG: {eds_path} enthaelt keine Datenpunkte", file=sys.stderr)

        ident, code = gen_node(node, objs, product, vendor)
        out_file = out_dir / f"canboss_net_node_{node['node_id']}.c"
        out_file.write_text(code, encoding="utf-8")
        externs.append(f"extern const cb_node_desc_t {ident};")
        registry.append(f"    &{ident},")
        dp_count = sum(len(o["dps"]) for o in objs)
        print(f"{out_file}: {len(objs)} Objekte, {dp_count} Datenpunkte")

    (out_dir / "canboss_net.h").write_text(
        HEADER_TEMPLATE.format(externs="\n".join(externs)), encoding="utf-8"
    )
    (out_dir / "canboss_net_registry.c").write_text(
        REGISTRY_TEMPLATE.format(rows="\n".join(registry), count=len(registry)),
        encoding="utf-8",
    )
    print(f"{out_dir}/canboss_net.h + canboss_net_registry.c geschrieben")


if __name__ == "__main__":
    main()
