// Avisos sonoros por el altavoz de la placa (codec ES8311 + amplificador).
//
// Deliberadamente pobre: tonos sinteticos, sin ficheros ni SD. Un monitor de
// aire solo necesita llamar la atencion, no sonar bien.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

typedef enum {
    SOUND_ALERT,   // dos pitidos: el CO2 ha pasado del umbral
    SOUND_CLEAR,   // un tono descendente: ya se ha ventilado
    SOUND_TEST,    // para el boton de prueba del panel web
} sound_effect_t;

esp_err_t sound_init(i2c_master_bus_handle_t bus);
bool sound_available(void);

// Encola un efecto. No bloquea: si la cola esta llena, se descarta.
void sound_play(sound_effect_t fx);

void sound_set_volume(uint8_t percent); // 0..100
