// Logica pura de calidad del aire: metricas del SEN66, umbrales, niveles y
// colores. Sin dependencias de ESP-IDF ni de LVGL — la comparte el firmware
// con el simulador de escritorio y es facil de probar en el PC.
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    AIR_PM1 = 0,   // ug/m3
    AIR_PM25,      // ug/m3
    AIR_PM4,       // ug/m3
    AIR_PM10,      // ug/m3
    AIR_HUM,       // %RH
    AIR_TEMP,      // grados C
    AIR_VOC,       // indice Sensirion 1..500 (100 = ambiente tipico)
    AIR_NOX,       // indice Sensirion 1..500 (1 = ambiente tipico)
    AIR_CO2,       // ppm
    AIR_NOISE,     // dB(A) aproximados; NO entra en el semaforo del aire
    AIR_METRIC_COUNT,
} air_metric_t;

// Cinco niveles ordenados de mejor a peor + desconocido.
typedef enum {
    AIR_LVL_GOOD = 0,
    AIR_LVL_FAIR,
    AIR_LVL_MODERATE,
    AIR_LVL_POOR,
    AIR_LVL_BAD,
    AIR_LVL_COUNT,
    AIR_LVL_UNKNOWN = AIR_LVL_COUNT,
} air_level_t;

typedef struct {
    float v[AIR_METRIC_COUNT]; // NAN = valor no disponible
    bool valid;                // false mientras no haya llegado ninguna lectura
    uint32_t age_s;            // segundos desde la ultima lectura buena
} air_sample_t;

// Deja la muestra a NAN / no valida.
void air_sample_clear(air_sample_t *s);

// Nombres. `key` es el identificador estable usado en MQTT y en el JSON del
// servidor web; `label` y `unit_ui` son ASCII puro porque las fuentes
// Montserrat de LVGL no llevan acentos ni el simbolo micro.
const char *air_metric_key(air_metric_t m);
const char *air_metric_label(air_metric_t m);
const char *air_metric_unit_ui(air_metric_t m);
const char *air_metric_unit_ha(air_metric_t m); // UTF-8, para Home Assistant
int air_metric_decimals(air_metric_t m);

// Clasificacion. Para los contaminantes son bandas crecientes; para
// temperatura y humedad es distancia al rango de confort.
air_level_t air_level(air_metric_t m, float value);
const char *air_level_text(air_level_t lvl);
uint32_t air_level_color(air_level_t lvl); // 0xRRGGBB

// Peor nivel entre CO2, PM2.5, PM10, VOC y NOx: el "semaforo" del resumen.
// Ignora temperatura y humedad, que son confort y no contaminacion.
air_level_t air_overall(const air_sample_t *s);
// Metrica responsable de ese peor nivel (para poder decir "por el CO2").
air_metric_t air_overall_driver(const air_sample_t *s);

// Tope de la escala de los indicadores circulares de cada metrica.
float air_gauge_max(air_metric_t m);
float air_gauge_min(air_metric_t m);
