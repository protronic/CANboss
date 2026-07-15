/**
 * canboss_types.h
 *
 * Gemeinsame Typen fuer die aus EDS-Dateien generierte CANboss-Oberflaeche.
 *
 * Die Tabellen (canboss_dp_t / canboss_node_desc_t) werden vom Generator
 * tools/eds2lvgl.py aus den EDS-Dateien in eds/ erzeugt und liegen in
 * App/generated/.
 */

#ifndef CANBOSS_TYPES_H_
#define CANBOSS_TYPES_H_

#include <stdint.h>
#include <stddef.h>
#include "lvgl/lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Maximale Nutzdatengroesse eines Datenpunkts (Strings werden gekappt) */
#define CANBOSS_DP_DATA_MAX 32u

/* CANopen-Datentypen (Teilmenge nach CiA 301, DataType aus der EDS) */
typedef enum {
    CB_DT_BOOL = 0, /* 0x01 */
    CB_DT_I8,       /* 0x02 */
    CB_DT_I16,      /* 0x03 */
    CB_DT_I32,      /* 0x04 */
    CB_DT_U8,       /* 0x05 */
    CB_DT_U16,      /* 0x06 */
    CB_DT_U32,      /* 0x07 */
    CB_DT_F32,      /* 0x08 */
    CB_DT_STR,      /* 0x09 VISIBLE_STRING */
    CB_DT_OCTET,    /* 0x0A OCTET_STRING */
    CB_DT_I64,      /* 0x15 */
    CB_DT_U64,      /* 0x1B */
} canboss_dtype_t;

/* Zugriffsart (AccessType aus der EDS) */
typedef enum {
    CB_ACC_RO = 0, /* ro / const */
    CB_ACC_WO,     /* wo */
    CB_ACC_RW,     /* rw / rww / rwr */
} canboss_access_t;

/* Ein Datenpunkt = ein (Index, Subindex)-Blatt aus der EDS */
typedef struct {
    uint16_t index;
    uint8_t sub;
    uint8_t dtype;      /* canboss_dtype_t */
    uint8_t access;     /* canboss_access_t */
    uint8_t has_limits; /* LowLimit/HighLimit aus EDS vorhanden */
    const char* name;   /* ParameterName aus EDS */
    int32_t min;        /* Grenzen fuer Eingabe-Widgets (nur Integer) */
    int32_t max;
} canboss_dp_t;

/* Ein CANopen-Knoten des Netzwerks (aus eds/network.json) */
typedef struct {
    uint8_t node_id;
    const char* name;          /* Anzeigename aus network.json */
    const char* eds_file;      /* Quelldatei (nur informativ) */
    const canboss_dp_t* dps;   /* generierte Datenpunkt-Tabelle */
    uint16_t dp_count;
    void (*screen_create)(void); /* generierte Screen-Aufbau-Funktion */
} canboss_node_desc_t;

/* Vom Generator erzeugte Registry aller Knoten */
extern const canboss_node_desc_t* const canboss_nodes[];
extern const uint16_t canboss_node_count;

#ifdef __cplusplus
}
#endif

#endif /* CANBOSS_TYPES_H_ */
