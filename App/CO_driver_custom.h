/**
 * CO_driver_custom.h
 *
 * Projektspezifische CANopenNode-Konfiguration fuer CANbossTouch.
 * Wird ueber das globale Define CO_DRIVER_CUSTOM (siehe Makefile) von
 * CO_driver_target.h eingebunden und ueberschreibt die Defaults aus
 * 301/CO_config.h.
 */

#ifndef CO_DRIVER_CUSTOM_H_
#define CO_DRIVER_CUSTOM_H_

#include "301/CO_config.h"

/* SDO-Client aktivieren: CANbossTouch liest und parametriert die
 * Datenpunkte fremder Knoten per SDO (Upload/Download, auch segmentiert
 * fuer Strings > 4 Byte). */
#define CO_CONFIG_SDO_CLI                                                                                              \
    (CO_CONFIG_SDO_CLI_ENABLE | CO_CONFIG_SDO_CLI_SEGMENTED | CO_CONFIG_GLOBAL_FLAG_CALLBACK_PRE                       \
     | CO_CONFIG_GLOBAL_FLAG_TIMERNEXT | CO_CONFIG_GLOBAL_FLAG_OD_DYNAMIC)

/* FIFO wird vom SDO-Client benoetigt */
#define CO_CONFIG_FIFO (CO_CONFIG_FIFO_ENABLE)

/* LSS-Slave: CO_app_STM32.c ruft CO_LSSinit() unbedingt auf */
#define CO_CONFIG_LSS (CO_CONFIG_LSS_SLAVE | CO_CONFIG_GLOBAL_FLAG_CALLBACK_PRE)

#endif /* CO_DRIVER_CUSTOM_H_ */
