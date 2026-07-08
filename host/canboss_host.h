/**
 * canboss_host.h
 *
 * Host-spezifische Schnittstellen des Linux/SDL2-Builds.
 */

#ifndef CANBOSS_HOST_H_
#define CANBOSS_HOST_H_

#ifdef __cplusplus
extern "C" {
#endif

/* SocketCAN-Backend der PoC-Hallenlichtsteuerung oeffnen (z.B. "vcan0").
 * Rueckgabe 0 bei Erfolg, sonst -1 (UI laeuft dann ohne CAN weiter). */
int canboss_poc_can_host_init(const char* ifname);

#ifdef __cplusplus
}
#endif

#endif /* CANBOSS_HOST_H_ */
