#include "air.h"

#include <math.h>
#include <stddef.h>

// Bandas de clasificacion -----------------------------------------------
//
// Contaminantes: cuatro fronteras -> cinco niveles (BUENO..MUY MALO).
//   CO2      semaforo clasico de ventilacion (DIN EN 13779 / recomendaciones
//            de ventilacion en interiores).
//   PM       inspirado en el indice europeo de calidad del aire (EAQI).
//   VOC/NOx  indices propios de Sensirion; 100 (VOC) y 1 (NOx) son la
//            referencia de "ambiente habitual" que aprende el sensor.
//
// Confort (temperatura y humedad): rango ideal + margenes simetricos, se
// clasifica por la distancia al rango, no por "cuanto mas alto peor".

typedef struct {
    bool comfort;
    float b[4];      // fronteras crecientes (contaminantes)
    float ideal_lo;  // rango ideal (confort)
    float ideal_hi;
    float step;      // ancho de cada banda fuera del ideal (confort)
} band_t;

static const band_t k_bands[AIR_METRIC_COUNT] = {
    [AIR_PM1]  = {.b = {10, 20, 25, 50}},
    [AIR_PM25] = {.b = {10, 20, 25, 50}},
    [AIR_PM4]  = {.b = {20, 40, 50, 100}},
    [AIR_PM10] = {.b = {20, 40, 50, 100}},
    [AIR_VOC]  = {.b = {150, 250, 400, 450}},
    [AIR_NOX]  = {.b = {20, 150, 300, 400}},
    [AIR_CO2]  = {.b = {800, 1000, 1400, 2000}},
    // Ruido en dB: biblioteca, conversacion, calle, molesto.
    [AIR_NOISE] = {.b = {45, 55, 65, 75}},
    [AIR_HUM]  = {.comfort = true, .ideal_lo = 40, .ideal_hi = 60, .step = 10},
    [AIR_TEMP] = {.comfort = true, .ideal_lo = 19, .ideal_hi = 25, .step = 3},
};

// Metricas que entran en el semaforo global, de mas a menos prioritaria a
// igualdad de nivel (para poder decir cual manda).
static const air_metric_t k_overall[] = {AIR_CO2, AIR_PM25, AIR_PM10, AIR_VOC, AIR_NOX};

void air_sample_clear(air_sample_t *s)
{
    for (int i = 0; i < AIR_METRIC_COUNT; i++) s->v[i] = NAN;
    s->valid = false;
    s->age_s = 0;
}

const char *air_metric_key(air_metric_t m)
{
    switch (m) {
    case AIR_PM1:  return "pm1";
    case AIR_PM25: return "pm25";
    case AIR_PM4:  return "pm4";
    case AIR_PM10: return "pm10";
    case AIR_HUM:  return "humidity";
    case AIR_TEMP: return "temperature";
    case AIR_VOC:  return "voc";
    case AIR_NOX:  return "nox";
    case AIR_CO2:  return "co2";
    case AIR_NOISE: return "noise";
    default:       return "?";
    }
}

const char *air_metric_label(air_metric_t m)
{
    switch (m) {
    case AIR_PM1:  return "PM1.0";
    case AIR_PM25: return "PM2.5";
    case AIR_PM4:  return "PM4.0";
    case AIR_PM10: return "PM10";
    case AIR_HUM:  return "Humedad";
    case AIR_TEMP: return "Temperatura";
    case AIR_VOC:  return "VOC";
    case AIR_NOX:  return "NOx";
    case AIR_CO2:  return "CO2";
    case AIR_NOISE: return "Ruido";
    default:       return "?";
    }
}

const char *air_metric_unit_ui(air_metric_t m)
{
    switch (m) {
    case AIR_PM1: case AIR_PM25: case AIR_PM4: case AIR_PM10: return "ug/m3";
    case AIR_HUM:  return "%";
    case AIR_TEMP: return "C";
    case AIR_VOC: case AIR_NOX: return "idx";
    case AIR_CO2:  return "ppm";
    case AIR_NOISE: return "dB";
    default:       return "";
    }
}

const char *air_metric_unit_ha(air_metric_t m)
{
    switch (m) {
    case AIR_PM1: case AIR_PM25: case AIR_PM4: case AIR_PM10: return "µg/m³";
    case AIR_HUM:  return "%";
    case AIR_TEMP: return "°C";
    case AIR_VOC: case AIR_NOX: return "";
    case AIR_CO2:  return "ppm";
    case AIR_NOISE: return "dB";
    default:       return "";
    }
}

int air_metric_decimals(air_metric_t m)
{
    switch (m) {
    case AIR_CO2: case AIR_VOC: case AIR_NOX: case AIR_NOISE: return 0;
    default: return 1;
    }
}

air_level_t air_level(air_metric_t m, float value)
{
    if (m < 0 || m >= AIR_METRIC_COUNT || isnan(value)) return AIR_LVL_UNKNOWN;
    const band_t *b = &k_bands[m];

    if (b->comfort) {
        float d = 0;
        if (value < b->ideal_lo) d = b->ideal_lo - value;
        else if (value > b->ideal_hi) d = value - b->ideal_hi;
        if (d <= 0) return AIR_LVL_GOOD;
        int n = (int)(d / b->step) + 1; // 1 paso fuera -> FAIR
        if (n >= AIR_LVL_BAD) return AIR_LVL_BAD;
        return (air_level_t)n;
    }

    for (int i = 0; i < 4; i++) {
        if (value < b->b[i]) return (air_level_t)i;
    }
    return AIR_LVL_BAD;
}

const char *air_level_text(air_level_t lvl)
{
    switch (lvl) {
    case AIR_LVL_GOOD:     return "BUENO";
    case AIR_LVL_FAIR:     return "ACEPTABLE";
    case AIR_LVL_MODERATE: return "REGULAR";
    case AIR_LVL_POOR:     return "MALO";
    case AIR_LVL_BAD:      return "MUY MALO";
    default:               return "---";
    }
}

uint32_t air_level_color(air_level_t lvl)
{
    switch (lvl) {
    case AIR_LVL_GOOD:     return 0x22c55e; // verde
    case AIR_LVL_FAIR:     return 0xa3e635; // lima
    case AIR_LVL_MODERATE: return 0xfacc15; // amarillo
    case AIR_LVL_POOR:     return 0xf97316; // naranja
    case AIR_LVL_BAD:      return 0xef4444; // rojo
    default:               return 0x475569; // gris
    }
}

air_level_t air_overall(const air_sample_t *s)
{
    air_level_t worst = AIR_LVL_UNKNOWN;
    for (size_t i = 0; i < sizeof(k_overall) / sizeof(k_overall[0]); i++) {
        air_level_t l = air_level(k_overall[i], s->v[k_overall[i]]);
        if (l == AIR_LVL_UNKNOWN) continue;
        if (worst == AIR_LVL_UNKNOWN || l > worst) worst = l;
    }
    return worst;
}

air_metric_t air_overall_driver(const air_sample_t *s)
{
    air_level_t worst = AIR_LVL_UNKNOWN;
    air_metric_t driver = AIR_CO2;
    for (size_t i = 0; i < sizeof(k_overall) / sizeof(k_overall[0]); i++) {
        air_metric_t m = k_overall[i];
        air_level_t l = air_level(m, s->v[m]);
        if (l == AIR_LVL_UNKNOWN) continue;
        // ">" y no ">=": a igualdad gana la primera de k_overall
        if (worst == AIR_LVL_UNKNOWN || l > worst) { worst = l; driver = m; }
    }
    return driver;
}

float air_gauge_min(air_metric_t m)
{
    switch (m) {
    case AIR_CO2:  return 400;   // el aire exterior ya esta ahi
    case AIR_NOISE: return 30;  // por debajo no llega: el ventilador pone el suelo
    case AIR_TEMP: return 0;
    default:       return 0;
    }
}

float air_gauge_max(air_metric_t m)
{
    switch (m) {
    case AIR_PM1: case AIR_PM25:  return 60;
    case AIR_PM4: case AIR_PM10:  return 120;
    case AIR_HUM:  return 100;
    case AIR_TEMP: return 40;
    case AIR_VOC: case AIR_NOX: return 500;
    case AIR_CO2:  return 2400;
    case AIR_NOISE: return 90;
    default:       return 100;
    }
}
