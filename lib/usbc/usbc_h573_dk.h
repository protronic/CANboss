/**
 * usbc_h573_dk.h - USB-C-Bring-up fuer den User-USB (CN17) des
 * STM32H573I-DK, gemeinsam fuer apps/gateway und apps/usbdemo.
 *
 * Zwei Dinge muessen stimmen, sonst enumeriert der Host nichts
 * (LD7/VBUS an heisst nur "Strom da", nicht "Device sichtbar"):
 *
 * 1. Dead-Battery-Rd auf den CC-Leitungen. Zephyr v4.4.2 schaltet sie
 *    in soc_early_init_hook() ab, weil die Bedingung dort nur den
 *    alten USB-Stack (CONFIG_USB_DEVICE_DRIVER) kennt - upstream nach
 *    v4.4.2 gefixt ("keep Type-C dead-battery CC pull-downs for the
 *    UDC stack"). Diese Datei haelt Rd per SYS_INIT(PRE_KERNEL_1)
 *    dagegen. TODO: Beim Zephyr-Update pruefen, ob soc.c inzwischen
 *    CONFIG_UDC_DRIVER beruecksichtigt - dann kann der SYS_INIT-Hook
 *    hier ersatzlos raus.
 *
 * 2. Der TCPP03-M20 (I2C4 @ 0x34, Enable an PG0) muss in den
 *    NORMAL-Modus; solange er in Low-Power haengt, bleibt D+/D- am
 *    CN17 tot. Empirisch bestaetigt ueber apps/usbdemo.
 *
 * Auf anderen Boards ist canboss_usbc_prepare() ein No-op (0).
 */

#ifndef CB_USBC_H573_DK_H_
#define CB_USBC_H573_DK_H_

#ifdef __cplusplus
extern "C" {
#endif

/* Rd-Status loggen und den TCPP03 in den NORMAL-Modus schalten.
 * Vor usbd_init()/usbd_enable() rufen. 0 = ok (auch auf Boards ohne
 * TCPP), sonst -errno vom I2C-Zugriff. */
int canboss_usbc_prepare(void);

#ifdef __cplusplus
}
#endif

#endif /* CB_USBC_H573_DK_H_ */
