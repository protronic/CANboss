/**
 * Demo-Knoten "Klimasensor" (Node 48, eds/demo_sensor.eds).
 *
 * Simulation:
 *  - Temperatur pendelt langsam um 21 GradC, relative Feuchte um 45 %
 *  - Luftdruck um 1013 hPa, Messzaehler inkrementiert je Zyklus
 *  - Alarm (0x2302/0x2305), sobald Temperatur oder Feuchte die
 *    Grenzwerte aus 0x2304 ueberschreiten (per SDO parametrierbar)
 */

#include <math.h>

#include "node_app.h"
#include "demo_sensor.h"

static void
sim(uint32_t t_ms)
{
	float t = (float)t_ms / 1000.0f;

	demo_sensor_RAM.x2300_klima.temperatur =
		21.0f + 3.0f * sinf(t * 0.05f) + 0.1f * sinf(t * 0.9f);
	demo_sensor_RAM.x2300_klima.relativeFeuchte = 45.0f + 10.0f * sinf(t * 0.03f + 2.0f);
	demo_sensor_RAM.x2301_umgebung.luftdruckHPa = 1013.0f + 4.0f * sinf(t * 0.01f);
	demo_sensor_RAM.x2301_umgebung.messzaehler++;

	uint8_t alarm = 0;
	if (demo_sensor_RAM.x2300_klima.temperatur >
	    demo_sensor_PERSIST_COMM.x2304_grenzwerte.temperaturMax) {
		alarm |= 0x01u;
	}
	if (demo_sensor_RAM.x2300_klima.relativeFeuchte >
	    demo_sensor_PERSIST_COMM.x2304_grenzwerte.relativeFeuchteMax) {
		alarm |= 0x02u;
	}
	demo_sensor_RAM.x2302_alarmstatus = alarm;
	demo_sensor_RAM.x2305_alarmAktiv = alarm != 0;
}

/* OD 0x2303 Messintervall ms (Default 1000, Limits 100..60000). */
static uint32_t
sim_interval_ms(void)
{
	uint16_t ms = demo_sensor_PERSIST_COMM.x2303_messintervallMs;

	if (ms < 100u) {
		ms = 100u;
	} else if (ms > 60000u) {
		ms = 60000u;
	}
	return ms;
}

int
main(void)
{
	static CO_config_t config;

	demo_sensor_INIT_CONFIG(config);

	node_app_run("demo_sensor", demo_sensor, &config, CONFIG_DEMO_NODE_ID, sim,
		     sim_interval_ms);
	return 0;
}
