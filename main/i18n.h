// Textos de la interfaz en varios idiomas.
//
// Logica pura, sin ESP-IDF ni LVGL: la comparte el firmware con el simulador.
//
// OJO con lo que se escribe aqui: las fuentes Montserrat de LVGL solo traen
// ASCII (0x20-0x7F) mas el grado. El aleman necesita ÄÖÜäöüß, que van en una
// fuente de reserva aparte (ver fonts/). El espanol se escribe SIN acentos a
// proposito: "Particulas", no "Partículas".
#pragma once

#include "air.h"

typedef enum { LANG_ES = 0, LANG_EN, LANG_DE, LANG_COUNT } lang_t;

typedef enum {
    STR_PAGE_GASES,        // "Gases"
    STR_PAGE_CLIMATE,      // "Clima"
    STR_ALL_OK,            // "todo en orden"
    STR_DRIVEN_BY,         // "manda el %s"  (lleva un %s)
    STR_WAITING,           // "esperando datos"
    STR_GAS_HINT,          // "100 = ambiente habitual"
    STR_NO_SENSOR,         // "sin sensor - revisa el I2C"
    STR_WARMING,           // "sensor calentando"
    STR_NOT_RESPONDING,    // "sensor no responde"
    STR_SETUP_AT,          // "configura en %s"  (lleva un %s)
    STR_NO_WIFI,           // "sin wifi"
    STR_LBL_TEMP,          // rotulo corto de temperatura
    STR_LBL_HUM,           // rotulo corto de humedad
    STR_COUNT,
} str_id_t;

void i18n_set_lang(lang_t lang);
lang_t i18n_get_lang(void);
const char *i18n_lang_code(lang_t lang);   // "es", "en", "de"
lang_t i18n_lang_from_code(const char *code);

// Texto en el idioma activo.
const char *T(str_id_t id);

// Nombre del nivel de calidad del aire en el idioma activo.
const char *i18n_level(air_level_t lvl);

// Nombre de una magnitud. Las de particulas no se traducen (PM2.5 es PM2.5
// en todas partes); las demas si.
const char *i18n_metric(air_metric_t m);
