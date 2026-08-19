/**
 * usbc_h573_dk.h - USB-C-Bring-up (TCPP03) fuer den User-USB (CN17) des
 * STM32H573I-DK, gemeinsam fuer apps/gateway und apps/usbdemo.
 *
 * Damit der Host am CN17 ein Device sieht (LD7/VBUS an heisst nur
 * "Strom da", nicht "Device sichtbar"), muss der TCPP03-M20
 * (I2C4 @ 0x34, Enable an PG0) in den NORMAL-Modus; solange er in
 * Low-Power haengt, bleibt D+/D- tot. Empirisch ueber apps/usbdemo
 * bestaetigt.
 *
 * Zephyrs Abschalten der UCPD-Dead-Battery-Pull-downs in
 * soc_early_init_hook() (v4.4.2) ist dagegen unkritisch: die
 * Enumeration am CN17 klappt nachweislich auch mit "Rd AUS" - die
 * CC-Beschaltung des TCPP03 uebernimmt die Senken-Erkennung. Der
 * frueher hier gehaltene Rd-Workaround wurde deshalb entfernt
 * (Historie: lib/usbc vor Commit "workaround ueberall raus"). Sollte
 * ein reiner Type-C-Host (C-auf-C-Kabel) je kein VBUS liefern, hier
 * zuerst wieder ansetzen.
 *
 * Auf anderen Boards ist canboss_usbc_prepare() ein No-op (0).
 */

#ifndef CB_USBC_H573_DK_H_
#define CB_USBC_H573_DK_H_

#ifdef __cplusplus
extern "C" {
#endif

/* Den TCPP03 in den NORMAL-Modus schalten.
 * Vor usbd_init()/usbd_enable() rufen. 0 = ok (auch auf Boards ohne
 * TCPP), sonst -errno vom I2C-Zugriff. */
int canboss_usbc_prepare(void);

#ifdef __cplusplus
}
#endif

#endif /* CB_USBC_H573_DK_H_ */
