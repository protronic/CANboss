/**
 * canboss.h
 *
 * Gemeinsame Typen des CANboss-Parametermonitors (C-Port von CANboss-rs).
 *
 * Das Objektmodell entspricht dem CouchDB-Proto des Rust-Originals
 * (proto.rs): ein Knoten besitzt Objekte (Index), ein Objekt besitzt
 * Datenpunkte (Subindex). Die Tabellen werden vom Generator
 * tools/eds2tui.py aus den EDS-Dateien in eds/ erzeugt und liegen in
 * src/gen/ (siehe canboss_net.h).
 */

#ifndef CANBOSS_H_
#define CANBOSS_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Maximale Nutzdatengroesse eines Datenpunkts (Strings werden gekappt) */
#define CB_DP_DATA_MAX 32u

/* Maximale Laenge eines formatierten Anzeigewerts (inkl. NUL) */
#define CB_VALUE_MAX 48u

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
} cb_dtype_t;

/* Zugriffsart (AccessType aus der EDS) */
typedef enum {
    CB_ACC_RO = 0, /* ro / const */
    CB_ACC_WO,     /* wo */
    CB_ACC_RW,     /* rw / rww / rwr */
} cb_access_t;

/* Ein Datenpunkt = ein (Index, Subindex)-Blatt aus der EDS */
typedef struct {
    uint16_t index;
    uint8_t sub;
    uint8_t dtype;             /* cb_dtype_t */
    uint8_t access;            /* cb_access_t */
    const char* name;          /* ParameterName aus der EDS */
    const char* default_value; /* DefaultValue aus der EDS ("" wenn keiner) */
} cb_dp_t;

/* Ein Objekt (Index) gruppiert seine Datenpunkte (Bereich in dps[]) */
typedef struct {
    uint16_t index;
    const char* name; /* ParameterName des Hauptobjekts */
    uint16_t dp_first;
    uint16_t dp_count;
} cb_obj_t;

/* Ein CANopen-Knoten des Netzwerks (aus eds/network.json) */
typedef struct {
    uint8_t node_id;
    const char* name;         /* Anzeigename aus network.json */
    const char* eds_file;     /* Quelldatei (nur informativ) */
    const char* product_name; /* ProductName aus [DeviceInfo] der EDS */
    const char* vendor_name;  /* VendorName aus [DeviceInfo] der EDS */
    const cb_obj_t* objs;
    uint16_t obj_count;
    const cb_dp_t* dps;
    uint16_t dp_count;
} cb_node_desc_t;

/* Vom Generator erzeugte Registry aller Knoten */
extern const cb_node_desc_t* const cb_net_nodes[];
extern const uint16_t cb_net_node_count;

const char* cb_dtype_name(cb_dtype_t dtype);
const char* cb_access_name(cb_access_t access);

#ifdef __cplusplus
}
#endif

#endif /* CANBOSS_H_ */
