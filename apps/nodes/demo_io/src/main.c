/**
 * Demo-Knoten "IO-Modul" (Node 16, eds/demo_io.eds).
 *
 * Simulation:
 *  - digitale Eingaenge D1..D4 spiegeln die digitalen Ausgaenge 1..4
 *    (die der Master per TPDO 0x2210 bzw. SDO setzt) - so ist die
 *    Wirkung von Schreibzugriffen direkt beobachtbar
 *  - analoge Eingaenge 1..4: Sinus/Saegezahn im EDS-Bereich +-1000
 *  - Temperatur pendelt um 22 GradC, angezogen vom Sollwert (0x2101)
 */

#include <math.h>

#include "node_app.h"
#include "demo_io.h"

static void
sim(uint32_t t_ms) {
    float t = (float)t_ms / 1000.0f;

    for (int i = 0; i < 4; i++) {
        demo_io_RAM.x2003_digitaleEingaenge[i] = demo_io_RAM.x2000_digitaleAusgaenge[i];
    }

    demo_io_RAM.x2001_analogeEingaenge[0] = (int16_t)(1000.0f * sinf(t * 0.5f));
    demo_io_RAM.x2001_analogeEingaenge[1] = (int16_t)(1000.0f * sinf(t * 0.2f + 1.0f));
    demo_io_RAM.x2001_analogeEingaenge[2] = (int16_t)((int)(t * 100.0f) % 2000 - 1000); /* Saegezahn */
    demo_io_RAM.x2001_analogeEingaenge[3] = demo_io_RAM.x2101_sollwert;                 /* folgt dem Sollwert */

    float ziel = 22.0f + (float)demo_io_RAM.x2101_sollwert / 100.0f;
    demo_io_RAM.x2100_temperatur += (ziel - demo_io_RAM.x2100_temperatur) * 0.05f;
    demo_io_RAM.x2100_temperatur += 0.2f * sinf(t * 1.3f);
}

int
main(void) {
    static CO_config_t config;
    demo_io_INIT_CONFIG(config);

    node_app_run("demo_io", demo_io, &config, CONFIG_DEMO_NODE_ID, sim);
    return 0;
}
