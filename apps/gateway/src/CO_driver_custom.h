/**
 * CO_driver_custom.h - CANopenNode-Overrides der Gateway-App
 * (eingebunden von lib/canopen/port/CO_driver_target.h via
 * Compile-Definition CO_DRIVER_CUSTOM).
 *
 * - Gateway-ASCII (CiA 309-3) mit SDO-Client und NMT-Master-Kommandos;
 *   dafuer braucht die FIFO die ASCII-Kommando-/Datentyp-Funktionen.
 * - SDO-Client mit Block-Transfer (Firmware-Streaming, 0x1F50) inkl.
 *   der dafuer noetigen FIFO/CRC16-Flags.
 * - Groesserer GTWA-Kommandopuffer (Block-Downloads ueber das Gateway).
 */

#ifndef CO_DRIVER_CUSTOM_H_
#define CO_DRIVER_CUSTOM_H_

#define CO_CONFIG_NMT (CO_CONFIG_NMT_MASTER | CO_CONFIG_GLOBAL_FLAG_CALLBACK_PRE | CO_CONFIG_GLOBAL_FLAG_TIMERNEXT)

#define CO_CONFIG_GTW                                                                                                  \
    (CO_CONFIG_GTW_ASCII | CO_CONFIG_GTW_ASCII_SDO | CO_CONFIG_GTW_ASCII_NMT | CO_CONFIG_GTW_ASCII_ERROR_DESC          \
     | CO_CONFIG_GTW_ASCII_LOG | CO_CONFIG_GTW_ASCII_PRINT_HELP)
#define CO_CONFIG_GTWA_COMM_BUF_SIZE 1000
#define CO_CONFIG_GTWA_LOG_BUF_SIZE  256 /* Default nur unter CO_DOXYGEN */

/* CO_gateway_ascii.c nutzt das Makro ohne eigenen Default (nur unter
 * CO_DOXYGEN definiert) - ohne diese Zeile bricht der Build ab, sobald
 * GTW_ASCII_SDO und SDO_CLI_BLOCK zusammen an sind. 3 Durchlaeufe je
 * process() beschleunigen den Block-Download, solange die CAN-TX-Queue
 * des Treibers Platz hat. */
#define CO_CONFIG_GTW_BLOCK_DL_LOOP 3

#define CO_CONFIG_CRC16 (CO_CONFIG_CRC16_ENABLE)

#define CO_CONFIG_SDO_CLI (CO_CONFIG_SDO_CLI_ENABLE | CO_CONFIG_SDO_CLI_SEGMENTED | CO_CONFIG_SDO_CLI_BLOCK)

#define CO_CONFIG_FIFO                                                                                                 \
    (CO_CONFIG_FIFO_ENABLE | CO_CONFIG_FIFO_ALT_READ | CO_CONFIG_FIFO_CRC16_CCITT | CO_CONFIG_FIFO_ASCII_COMMANDS      \
     | CO_CONFIG_FIFO_ASCII_DATATYPES)

#endif /* CO_DRIVER_CUSTOM_H_ */
