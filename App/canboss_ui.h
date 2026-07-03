/**
 * canboss_ui.h
 *
 * LVGL-Laufzeitschicht fuer die aus EDS-Dateien generierten Screens.
 *
 * Der Generator (tools/eds2lvgl.py) erzeugt pro Knoten eine
 * Screen-Aufbau-Funktion, die canboss_ui_screen_begin() aufruft und dann
 * pro Datenpunkt eine der add-Row-Fabriken. Die Fabriken erzeugen die
 * LVGL-Widgets, verdrahten SDO-Lesen (zyklischer Refresh) und SDO-Schreiben
 * (Parametrierung bei Wertaenderung) ueber canboss_sdo.
 */

#ifndef CANBOSS_UI_H_
#define CANBOSS_UI_H_

#include "canboss_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Hauptmenue (Knotenliste aus der generierten Registry) aufbauen und laden.
 * Im LVGL-Kontext nach lv_init()/Displayinit aufrufen. */
void canboss_ui_init(void);

/* Neuen Knoten-Screen anlegen (Header mit Zurueck-Knopf + Titel + Status,
 * scrollbare Datenpunktliste) und laden. Liefert den Listencontainer, an
 * den die Row-Fabriken anbauen. */
lv_obj_t* canboss_ui_screen_begin(const canboss_node_desc_t* node);

/* Row-Fabriken - Auswahl trifft der Generator anhand von DataType und
 * AccessType aus der EDS: */

/* Nur-Lese-Zeile: Name + zyklisch per SDO-Upload aktualisierter Wert */
void canboss_ui_add_value_row(lv_obj_t* cont, const canboss_node_desc_t* node, const canboss_dp_t* dp);

/* BOOLEAN rw: Schalter, Aenderung wird per SDO-Download geschrieben */
void canboss_ui_add_switch_row(lv_obj_t* cont, const canboss_node_desc_t* node, const canboss_dp_t* dp);

/* Integer rw: Spinbox mit Grenzen aus der EDS, Schreiben per SDO-Download */
void canboss_ui_add_spinbox_row(lv_obj_t* cont, const canboss_node_desc_t* node, const canboss_dp_t* dp);

/* VISIBLE_STRING rw: Textfeld mit Bildschirmtastatur, Schreiben bei Enter */
void canboss_ui_add_text_row(lv_obj_t* cont, const canboss_node_desc_t* node, const canboss_dp_t* dp);

#ifdef __cplusplus
}
#endif

#endif /* CANBOSS_UI_H_ */
