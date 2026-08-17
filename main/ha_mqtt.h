// Publicacion a Home Assistant por MQTT con autodescubrimiento.
//
// Al conectar publica un mensaje de descubrimiento retenido por cada metrica
// bajo <prefijo>/sensor/<id>/<clave>/config, de modo que el aparato aparece
// solo en HA como un unico dispositivo con todas sus entidades. El estado va
// en un unico JSON a <id>/state y la disponibilidad (con LWT) a <id>/status.
#pragma once

#include <stdbool.h>

#include "air.h"

void ha_mqtt_start(void);
void ha_mqtt_stop(void);
bool ha_mqtt_connected(void);

// Publica la muestra actual. No bloquea (encola en el cliente MQTT).
void ha_mqtt_publish(const air_sample_t *s);
