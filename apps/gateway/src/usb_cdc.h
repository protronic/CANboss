/**
 * usb_cdc.h - USB-Device (CDC-ACM) fuer das NDJSON-Gateway.
 *
 * Auf Targets mit USB-Device-Controller (STM32H573I-DK: USB_DRD_FS an
 * PA11/PA12, Board-Label zephyr_udc0) laeuft der NDJSON-Strom ueber
 * eine CDC-ACM-Instanz statt ueber die ST-Link-VCP-UART. Der Browser
 * sieht sie als ganz normalen seriellen Port (WebSerial), ohne
 * Treiber und ohne ST-Link im Spiel.
 *
 * Ohne CONFIG_CANBOSS_GW_USB sind alle Funktionen No-ops, damit
 * native_sim und der VCP-Pfad unveraendert bauen.
 */

#ifndef CB_GW_USB_CDC_H_
#define CB_GW_USB_CDC_H_

#include <stdbool.h>

#ifdef CONFIG_CANBOSS_GW_USB

/* USB-Device aufsetzen (Deskriptoren, CDC-ACM) und einschalten.
 * 0 = ok; danach dauert es je nach Host ~1 s bis zur Enumeration. */
int canboss_usb_init(void);

/* true genau einmal je DTR-Flanke: der Host (die Webapp) hat den Port
 * gerade geoeffnet. Aufrufer nutzt das, um eine angefangene Zeile
 * abzuschliessen, damit die Webapp nicht auf einem Fragment aufsetzt. */
bool canboss_usb_take_attach(void);

#else

static inline int
canboss_usb_init(void) {
    return 0;
}

static inline bool
canboss_usb_take_attach(void) {
    return false;
}

#endif /* CONFIG_CANBOSS_GW_USB */

#endif /* CB_GW_USB_CDC_H_ */
