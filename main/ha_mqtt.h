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
// No publica nada si el aparato esta marcado como no disponible.
void ha_mqtt_publish(const air_sample_t *s);

// Marca el aparato como disponible o no. Sin esto, si el sensor deja de
// responder, Home Assistant seguiria enseñando el ultimo valor conocido como
// si fuera de ahora mismo: datos rancios disfrazados de frescos.
void ha_mqtt_set_available(bool available);
