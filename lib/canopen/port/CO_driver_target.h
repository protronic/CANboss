/**
 * CO_driver_target.h
 *
 * CANopenNode-Zielkonfiguration fuer den CANboss-Monitor (Linux/POSIX
 * und Zephyr). Der eigentliche Buszugriff laeuft ueber die
 * austauschbare Backend-Schnittstelle src/can_if.h (SocketCAN bzw.
 * Zephyr-CAN heute, seriell spaeter).
 *
 * Threads (src/osal.h: pthreads bzw. k_thread):
 *   - RX-Thread:       cb_can_recv() -> CO_CANrxDispatch()
 *   - Mainline-Thread: CO_process() + SYNC/RPDO/TPDO
 *   - UI/Main-Thread:  SDO-Client-Transfers (seriell, mutex-geschuetzt)
 *
 * Die CO_LOCK_*-Makros sind deshalb mit echten Mutexen belegt
 * (Definition in port/CO_driver.c auf Basis der OS-Abstraktion).
 */

#ifndef CO_DRIVER_TARGET_H
#define CO_DRIVER_TARGET_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef CO_DRIVER_CUSTOM
#include "CO_driver_custom.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Mehrere Objektverzeichnisse mit eigenem Praefix (od/canboss_master.h)
 * statt des Standard-"OD"-Praefixes. */
#ifndef CO_MULTIPLE_OD
#define CO_MULTIPLE_OD
#endif

/* Stack-Konfiguration (Abweichungen von 301/CO_config.h) */

/* SDO-Client fuer den Parametermonitor: expedited + segmented reicht
 * (Block-Transfer braucht kein Geraet der RZ25/Demo-Familie).
 * Apps mit groesseren Transfers (z.B. Firmware-Download 0x1F50)
 * uebersteuern das per CO_DRIVER_CUSTOM/CO_driver_custom.h, daher
 * #ifndef-Guards. */
#ifndef CO_CONFIG_SDO_CLI
#define CO_CONFIG_SDO_CLI (CO_CONFIG_SDO_CLI_ENABLE | CO_CONFIG_SDO_CLI_SEGMENTED)
#endif
#ifndef CO_CONFIG_FIFO
#define CO_CONFIG_FIFO (CO_CONFIG_FIFO_ENABLE)
#endif

/* Master/Panel/Demo-Knoten: kein LSS, keine LEDs, keine
 * Parameterspeicherung. */
#define CO_CONFIG_LSS     (0)
#define CO_CONFIG_LEDS    (0)
#define CO_CONFIG_STORAGE (0)

/* Emergency: Producer ja, Error-History (0x1003) nein - die ODs der
 * Demo-Knoten haben kein 0x1003, und Monitor/Panel werten die History
 * nicht aus. */
#define CO_CONFIG_EM                                                                                                   \
    (CO_CONFIG_EM_PRODUCER | CO_CONFIG_GLOBAL_FLAG_CALLBACK_PRE | CO_CONFIG_GLOBAL_FLAG_TIMERNEXT)

/* Basisdefinitionen: x86/ARM Linux ist little-endian. */
#define CO_LITTLE_ENDIAN
#define CO_SWAP_16(x) x
#define CO_SWAP_32(x) x
#define CO_SWAP_64(x) x
typedef uint_fast8_t bool_t;
typedef float float32_t;
typedef double float64_t;

/* Empfangener CAN-Frame, wie ihn port/CO_driver.c an die RX-Callbacks
 * der Stack-Module uebergibt. */
typedef struct {
    uint32_t ident;
    uint8_t DLC;
    uint8_t data[8];
} CO_CANrxMsg_t;

/* Zugriff auf den empfangenen Frame aus den RX-Callbacks */
#define CO_CANrxMsg_readIdent(msg) ((uint16_t)(((const CO_CANrxMsg_t*)(msg))->ident))
#define CO_CANrxMsg_readDLC(msg)   ((uint8_t)(((const CO_CANrxMsg_t*)(msg))->DLC))
#define CO_CANrxMsg_readData(msg)  ((const uint8_t*)(((const CO_CANrxMsg_t*)(msg))->data))

/* Empfangs-Filterobjekt */
typedef struct {
    uint16_t ident;
    uint16_t mask;
    void* object;
    void (*CANrx_callback)(void* object, void* message);
} CO_CANrx_t;

/* Sendeobjekt */
typedef struct {
    uint32_t ident;
    uint8_t DLC;
    uint8_t data[8];
    volatile bool_t bufferFull;
    volatile bool_t syncFlag;
} CO_CANtx_t;

/* CAN-Modul: haelt das aktive can_if-Backend in CANptr */
typedef struct {
    void* CANptr; /* const cb_can_backend_t* */
    CO_CANrx_t* rxArray;
    uint16_t rxSize;
    CO_CANtx_t* txArray;
    uint16_t txSize;
    uint16_t CANerrorStatus;
    volatile bool_t CANnormal;
    volatile bool_t useCANrxFilters;
    volatile bool_t bufferInhibitFlag;
    volatile bool_t firstCANtxMessage;
    volatile uint16_t CANtxCount;
    uint32_t errOld;
} CO_CANmodule_t;

/* Datenspeicherung ist deaktiviert; Struktur nur fuer die API-Signatur. */
typedef struct {
    void* addr;
    size_t len;
    uint8_t subIndexOD;
    uint8_t attr;
} CO_storage_entry_t;

/* Kritische Abschnitte: Mutexe der OS-Abstraktion (port/CO_driver.c) */
void CO_driver_lock_send(void);
void CO_driver_unlock_send(void);
void CO_driver_lock_emcy(void);
void CO_driver_unlock_emcy(void);
void CO_driver_lock_od(void);
void CO_driver_unlock_od(void);

#define CO_LOCK_CAN_SEND(CAN_MODULE)   CO_driver_lock_send()
#define CO_UNLOCK_CAN_SEND(CAN_MODULE) CO_driver_unlock_send()

#define CO_LOCK_EMCY(CAN_MODULE)   CO_driver_lock_emcy()
#define CO_UNLOCK_EMCY(CAN_MODULE) CO_driver_unlock_emcy()

#define CO_LOCK_OD(CAN_MODULE)   CO_driver_lock_od()
#define CO_UNLOCK_OD(CAN_MODULE) CO_driver_unlock_od()

/* Synchronisation zwischen RX-Thread und Verarbeitungsthreads */
#define CO_MemoryBarrier() __sync_synchronize()
#define CO_FLAG_READ(rxNew) ((rxNew) != NULL)
#define CO_FLAG_SET(rxNew)                                                                                             \
    {                                                                                                                  \
        CO_MemoryBarrier();                                                                                            \
        rxNew = (void*)1L;                                                                                             \
    }
#define CO_FLAG_CLEAR(rxNew)                                                                                           \
    {                                                                                                                  \
        CO_MemoryBarrier();                                                                                            \
        rxNew = NULL;                                                                                                  \
    }

/* Einen empfangenen Frame an die passenden RX-Callbacks verteilen
 * (Aufruf aus dem RX-Thread, Implementierung in port/CO_driver.c). */
void CO_CANrxDispatch(CO_CANmodule_t* CANmodule, const CO_CANrxMsg_t* rcvMsg);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* CO_DRIVER_TARGET_H */
