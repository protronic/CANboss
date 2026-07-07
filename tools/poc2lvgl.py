#!/usr/bin/env python3
"""poc2lvgl.py - PoC-JSON-Hallensteuerung nach C-Konfigtabellen.

Liest die Touch-Projekt-Konfiguration des OxivGL-PoC (hall_config.json +
can_config.json, wie in examples/touch-projects/<Projekt> des embassy-Repos)
und erzeugt App/generated/canboss_poc_gen.{h,c}: UI-Strings, Feld-/Button-
Tabellen und das CAN-Bitmasken-Protokoll (TX-One-Hot-Payload, minp-RX-Map)
fuer den LVGL-Hallenscreen in App/canboss_poc.c.

Aufruf (siehe Makefile-Ziel `gen`):
    python3 tools/poc2lvgl.py --config poc --out App/generated
"""

import argparse
import json
from pathlib import Path


def c_str(s: str) -> str:
    s = s.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n")
    return '"' + s + '"'


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--config", required=True, help="Verzeichnis mit hall_config.json + can_config.json")
    ap.add_argument("--out", required=True, help="Zielverzeichnis fuer canboss_poc_gen.{h,c}")
    args = ap.parse_args()

    cfg_dir = Path(args.config)
    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)

    hall = json.loads((cfg_dir / "hall_config.json").read_text(encoding="utf-8"))
    can = json.loads((cfg_dir / "can_config.json").read_text(encoding="utf-8"))

    fields = hall["fields"]
    field_count = len(fields)
    button_count = field_count * 3 + 3
    group_base = field_count * 3
    ui = hall.get("ui", {})

    tx_id = int(can.get("tx_id", 0x200))
    tx_little = can.get("tx_endian", "little") == "little"
    minp = can.get("minp", [])
    minp_ids = {int(e[0]) for e in minp} or {0}
    if len(minp_ids) > 1:
        raise SystemExit("poc2lvgl: nur eine minp-CAN-ID unterstuetzt")
    minp_id = minp_ids.pop()

    # minp-Tabelle auf button_count begrenzen/auffuellen (inaktive Eintraege)
    minp_rows = []
    for i in range(button_count):
        if i < len(minp):
            _, active, byte_idx, bit_idx = (int(x) for x in minp[i])
        else:
            active, byte_idx, bit_idx = 0, 0, 0
        minp_rows.append((active, byte_idx, bit_idx))

    lux_suffix = ui.get("lux_suffix", "Lux")
    off_label = ui.get("off_label", "Aus")
    all_prefix = ui.get("all_prefix", "Alle")
    central_off = ui.get("central_off_label", "Zentral Aus")

    h = []
    h.append("/* @generated von tools/poc2lvgl.py - nicht editieren */")
    h.append("#ifndef CANBOSS_POC_GEN_H_")
    h.append("#define CANBOSS_POC_GEN_H_")
    h.append("")
    h.append("#include <stdint.h>")
    h.append("#include <stdbool.h>")
    h.append("")
    h.append(f"#define CANBOSS_POC_FIELD_COUNT   {field_count}u")
    h.append(f"#define CANBOSS_POC_BUTTON_COUNT  {button_count}u")
    h.append(f"#define CANBOSS_POC_GROUP_BASE    {group_base}u")
    h.append("")
    h.append(f"#define CANBOSS_POC_CAN_ENABLED   {1 if can.get('enabled', True) else 0}")
    h.append(f"#define CANBOSS_POC_TX_ID         0x{tx_id:03X}u")
    h.append(f"#define CANBOSS_POC_TX_LITTLE     {1 if tx_little else 0}")
    h.append(f"#define CANBOSS_POC_MINP_ID       0x{minp_id:03X}u")
    h.append(f"#define CANBOSS_POC_REPEAT_MS     {int(can.get('command_repeat_ms', 25))}u")
    h.append(f"#define CANBOSS_POC_REFRESH_MS    1000u /* Idle-Keepalive (Release-Payload) */")
    h.append("")
    h.append("typedef struct {")
    h.append("    const char* eyebrow;")
    h.append("    const char* title;")
    h.append("} canboss_poc_field_t;")
    h.append("")
    h.append("typedef struct {")
    h.append("    uint8_t active; /* 0 = kein Feedback-Bit fuer diesen Button */")
    h.append("    uint8_t byte_index;")
    h.append("    uint8_t bit_index;")
    h.append("} canboss_poc_minp_t;")
    h.append("")
    h.append("extern const char* const canboss_poc_hall_name;")
    h.append("extern const char* const canboss_poc_page_title;")
    h.append("extern const canboss_poc_field_t canboss_poc_fields[CANBOSS_POC_FIELD_COUNT];")
    h.append("/* Beschriftungen der 3 Buttons je Spalte (Feld i -> Button-Index i*3+j;")
    h.append(" * letzte Spalte = Zentral-Gruppe ab CANBOSS_POC_GROUP_BASE) */")
    h.append("extern const char* const canboss_poc_button_labels[CANBOSS_POC_BUTTON_COUNT];")
    h.append("extern const canboss_poc_minp_t canboss_poc_minp[CANBOSS_POC_BUTTON_COUNT];")
    h.append("")
    h.append("#endif /* CANBOSS_POC_GEN_H_ */")

    c = []
    c.append("/* @generated von tools/poc2lvgl.py - nicht editieren */")
    c.append('#include "canboss_poc_gen.h"')
    c.append("")
    c.append(f"const char* const canboss_poc_hall_name = {c_str(hall.get('hall_name', ''))};")
    page_title = hall.get("page_title_prefix", hall.get("app_title", ""))
    c.append(f"const char* const canboss_poc_page_title = {c_str(page_title)};")
    c.append("")
    c.append("const canboss_poc_field_t canboss_poc_fields[CANBOSS_POC_FIELD_COUNT] = {")
    tribune_eyebrow = ui.get("tribune_eyebrow", "")
    for i, f in enumerate(fields):
        eyebrow = tribune_eyebrow if (i == 0 and tribune_eyebrow) else f.get("eyebrow", "")
        c.append(f"    {{ {c_str(eyebrow)}, {c_str(f.get('label', f.get('name', '')))} }},")
    c.append("};")
    c.append("")
    c.append("const char* const canboss_poc_button_labels[CANBOSS_POC_BUTTON_COUNT] = {")
    for _ in fields:
        c.append(f"    {c_str('500 ' + lux_suffix)}, {c_str('300 ' + lux_suffix)}, {c_str(off_label)},")
    c.append(
        f"    {c_str(all_prefix + chr(10) + '500 ' + lux_suffix)}, "
        f"{c_str(all_prefix + chr(10) + '300 ' + lux_suffix)}, {c_str(central_off)},"
    )
    c.append("};")
    c.append("")
    c.append("const canboss_poc_minp_t canboss_poc_minp[CANBOSS_POC_BUTTON_COUNT] = {")
    for active, byte_idx, bit_idx in minp_rows:
        c.append(f"    {{ {active}, {byte_idx}, {bit_idx} }},")
    c.append("};")

    (out_dir / "canboss_poc_gen.h").write_text("\n".join(h) + "\n", encoding="utf-8")
    (out_dir / "canboss_poc_gen.c").write_text("\n".join(c) + "\n", encoding="utf-8")
    print(f"poc2lvgl: {field_count} Felder, {button_count} Buttons -> {out_dir}/canboss_poc_gen.[ch]")


if __name__ == "__main__":
    main()
