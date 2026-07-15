/**
 * Demo-Knoten "Antrieb" (Node 32, eds/demo_drive.eds, CiA402-Teilmenge).
 *
 * Simulation:
 *  - Drehzahl-Demand faehrt eine Rampe auf den vl target velocity
 *    (0x6042, per SDO/RPDO beschreibbar), sofern das Controlword
 *    (0x6040) Bit 0 (Switch On) gesetzt hat; sonst Rampe auf 0
 *  - Istdrehzahl = Demand + kleines Rauschen
 *  - Motorstrom ~ |Drehzahl|, Endstufentemperatur folgt dem Strom
 *  - Statusword: Bit 2 (Operation enabled) spiegelt das Controlword,
 *    Bremse aktiv solange Drehzahl 0 und nicht freigegeben
 */

#include <math.h>
#include <stdlib.h>

#include "node_app.h"
#include "demo_drive.h"

#define RAMPE_PRO_TICK 10 /* Drehzahlaenderung je 100 ms */

static void
sim(uint32_t t_ms) {
    bool freigabe = (demo_drive_RAM.x6040_controlword & 0x0001u) != 0;
    int16_t ziel = freigabe ? demo_drive_PERSIST_COMM.x6042_vlTargetVelocity : 0;

    int16_t demand = demo_drive_RAM.x6043_vlVelocityDemand;
    int32_t diff = (int32_t)ziel - demand;
    if (diff > RAMPE_PRO_TICK) {
        diff = RAMPE_PRO_TICK;
    } else if (diff < -RAMPE_PRO_TICK) {
        diff = -RAMPE_PRO_TICK;
    }
    demand = (int16_t)(demand + diff);

    demo_drive_RAM.x6043_vlVelocityDemand = demand;
    demo_drive_RAM.x6044_vlVelocityActualValue = (int16_t)(demand + (int)(t_ms / 100u % 3u) - 1);
    demo_drive_RAM.x2200_motorstromMA = 50 + 2 * abs((int)demand);
    demo_drive_RAM.x2202_temperaturEndstufe = 35.0f + (float)abs((int)demand) / 50.0f;
    demo_drive_RAM.x2201_bremseAktiv = (demand == 0 && !freigabe);
    demo_drive_RAM.x6041_statusword = (uint16_t)(freigabe ? 0x0027u : 0x0021u);
}

int
main(void) {
    static CO_config_t config;
    demo_drive_INIT_CONFIG(config);

    node_app_run("demo_drive", demo_drive, &config, CONFIG_DEMO_NODE_ID, sim);
    return 0;
}
