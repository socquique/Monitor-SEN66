// Nivel de ruido con los microfonos de la placa.
//
// OJO: los micros NO cuelgan del ES8311 (ese es solo salida, ver sound.c).
// Van a un ADC aparte, el ES7210 en 0x40, y entran por I2S_DIN. Como ambos
// codecs comparten los relojes del mismo I2S, la recepcion se abre en el
// MISMO puerto que la reproduccion, en full-duplex: el canal de RX lo crea
// sound_init() y aqui solo se usa.
//
// De momento esto NO da decibelios reales: da nivel a fondo de escala
// (dBFS, siempre negativo). Convertirlo a dB SPL exige conocer la
// sensibilidad del microfono y la ganancia del codec, y eso se calibra
// contra una referencia, no se deduce.
#pragma once

#include <stdbool.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

// Configura el ES7210 y arranca la tarea de medida. Necesita que sound_init()
// se haya llamado antes: es quien crea el canal de recepcion.
esp_err_t mic_init(i2c_master_bus_handle_t bus);
bool mic_available(void);

// Nivel RMS de la ultima ventana y pico de la misma, en dBFS. NAN mientras
// no haya ninguna medida todavia.
float mic_level_dbfs(void);
float mic_peak_dbfs(void);

// Texto corto con lo que ha pasado: "ok", o el motivo por el que no mide.
// Sin consola serie a mano, esto es lo unico que dice donde falla.
const char *mic_status(void);
