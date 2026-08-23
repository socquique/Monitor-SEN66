// Ajustes persistentes en NVS.
//
// OJO (leccion de TamaPoke): escribir en NVS congela ~1 s los DOS nucleos.
// Por eso settings_save() solo se llama desde el servidor web o al terminar
// el aprovisionamiento, nunca desde el bucle de la UI ni por cada muestra.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define SETTINGS_PAGES 6 // resumen, CO2, particulas, gases, clima, ruido

typedef struct {
    char wifi_ssid[33];
    char wifi_pass[65];

    char mqtt_uri[96];   // p.ej. "mqtt://192.168.1.10:1883"
    char mqtt_user[33];
    char mqtt_pass[65];
    char mqtt_prefix[32]; // prefijo de descubrimiento de HA, normalmente "homeassistant"

    char device_name[32]; // como aparece en Home Assistant
    char tz[48];          // cadena POSIX TZ, p.ej. "CET-1CEST,M3.5.0,M10.5.0/3"
    char ntp[64];

    uint8_t brightness;      // 1..255 (comando 0x51 del panel)
    uint8_t night_brightness;// brillo al atenuar
    uint16_t screen_timeout_s; // 0 = nunca atenuar
    uint16_t page_dwell_s;     // 0 = sin rotacion automatica
    uint8_t pages_mask;        // bit i = pagina i visible
    uint16_t chart_span_min;   // ventana de las graficas, en minutos

    // Decimas de grado C. Se SUMA a la lectura del sensor, que es como lo
    // define Sensirion: para corregir un aparato que marca de mas, va en
    // negativo. El comentario decia lo contrario y era falso.
    int16_t temp_offset_dc;
    uint16_t altitude_m;
    bool co2_asc;            // autocalibracion del CO2

    // --- Campos nuevos SIEMPRE al final ---
    // settings_load() acepta blobs mas cortos que esta estructura, asi que
    // anadir aqui no obliga a nadie a reconfigurar. Insertar en medio, si.
    bool alarm_enabled;      // aviso sonoro al pasar el umbral de CO2
    uint16_t alarm_co2_ppm;  // umbral de disparo
    uint16_t alarm_clear_ppm;// umbral de rearme (histeresis)
    uint8_t alarm_volume;    // 0..100
    uint32_t last_fan_clean;  // epoch de la ultima limpieza de ventilador
    char lang[4];             // "es", "en" o "de"

    // Calibracion del sonometro: dB que se SUMAN al nivel del microfono
    // (que sale en dBFS, negativo) para dar dB SPL. Depende de la
    // sensibilidad del micro y de la ganancia del codec, asi que se ajusta
    // contra una referencia; no se deduce.
    int16_t noise_offset_db;
} settings_t;

// Carga de NVS; si no hay nada guardado deja los valores por defecto.
esp_err_t settings_load(void);
esp_err_t settings_save(void);
settings_t *settings_get(void);

// Estado del algoritmo VOC del sensor, en su propia clave: asi guardar los
// ajustes no lo pisa ni al reves. Devuelve ESP_ERR_NVS_NOT_FOUND si no hay
// nada guardado todavia.
esp_err_t settings_save_voc_state(const void *state, size_t len);
esp_err_t settings_load_voc_state(void *state, size_t len);
void settings_defaults(settings_t *s);

static inline bool settings_page_visible(const settings_t *s, int page)
{
    return (s->pages_mask >> page) & 1;
}
