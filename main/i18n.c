#include "i18n.h"

#include <string.h>

static lang_t s_lang = LANG_ES;

// Filas: el orden de str_id_t. Columnas: ES, EN, DE.
//
// El aleman esta escrito evitando la mayoria de umlauts sin forzar la
// lengua ("Gesamt" en vez de "Uebersicht"), pero los que quedan son
// naturales y van con la fuente de reserva.
static const char *const TBL[STR_COUNT][LANG_COUNT] = {
    [STR_PAGE_GASES]     = {"Gases", "Gases", "Gase"},
    [STR_PAGE_CLIMATE]   = {"Clima", "Climate", "Klima"},
    [STR_ALL_OK]         = {"todo en orden", "all clear", "alles in Ordnung"},
    [STR_DRIVEN_BY]      = {"manda el %s", "driven by %s", "bestimmt durch %s"},
    [STR_WAITING]        = {"esperando datos", "waiting for data", "warte auf Daten"},
    [STR_GAS_HINT]       = {"100 = ambiente habitual", "100 = typical air",
                            "100 = normale Umgebung"},
    [STR_NO_SENSOR]      = {"sin sensor - revisa el I2C", "no sensor - check I2C",
                            "kein Sensor - I2C prüfen"},
    [STR_WARMING]        = {"sensor calentando", "sensor warming up",
                            "Sensor heizt auf"},
    [STR_NOT_RESPONDING] = {"sensor no responde", "sensor not responding",
                            "Sensor antwortet nicht"},
    [STR_SETUP_AT]       = {"configura en %s", "set up at %s", "einrichten unter %s"},
    [STR_NO_WIFI]        = {"sin wifi", "no wifi", "kein WLAN"},
    [STR_LBL_TEMP]       = {"Temp", "Temp", "Temp"},
    [STR_LBL_HUM]        = {"Humedad", "Humidity", "Feuchte"},
};

// Los cinco niveles. En aleman "MÄSSIG" lleva umlaut y es la palabra correcta:
// no la cambiamos por una peor solo para evitar el glifo.
static const char *const NIVELES[AIR_LVL_COUNT][LANG_COUNT] = {
    {"BUENO",     "GOOD",      "GUT"},
    {"ACEPTABLE", "FAIR",      "MÄSSIG"},
    {"REGULAR",   "MODERATE",  "MITTEL"},
    {"MALO",      "POOR",      "SCHLECHT"},
    {"MUY MALO",  "VERY POOR", "SEHR SCHLECHT"},
};

static const char *const METRICAS[AIR_METRIC_COUNT][LANG_COUNT] = {
    [AIR_PM1]  = {"PM1.0", "PM1.0", "PM1.0"},
    [AIR_PM25] = {"PM2.5", "PM2.5", "PM2.5"},
    [AIR_PM4]  = {"PM4.0", "PM4.0", "PM4.0"},
    [AIR_PM10] = {"PM10",  "PM10",  "PM10"},
    [AIR_HUM]  = {"Humedad", "Humidity", "Feuchte"},
    [AIR_TEMP] = {"Temperatura", "Temperature", "Temperatur"},
    [AIR_VOC]  = {"VOC", "VOC", "VOC"},
    [AIR_NOX]  = {"NOx", "NOx", "NOx"},
    [AIR_CO2]  = {"CO2", "CO2", "CO2"},
    [AIR_NOISE] = {"Ruido", "Noise", "Lärm"},
};

void i18n_set_lang(lang_t lang)
{
    if (lang >= 0 && lang < LANG_COUNT) s_lang = lang;
}

lang_t i18n_get_lang(void) { return s_lang; }

const char *i18n_lang_code(lang_t lang)
{
    switch (lang) {
    case LANG_EN: return "en";
    case LANG_DE: return "de";
    default:      return "es";
    }
}

lang_t i18n_lang_from_code(const char *code)
{
    if (!code) return LANG_ES;
    if (strncmp(code, "en", 2) == 0) return LANG_EN;
    if (strncmp(code, "de", 2) == 0) return LANG_DE;
    return LANG_ES;
}

const char *T(str_id_t id)
{
    if (id < 0 || id >= STR_COUNT) return "?";
    return TBL[id][s_lang];
}

const char *i18n_level(air_level_t lvl)
{
    if (lvl < 0 || lvl >= AIR_LVL_COUNT) return "---";
    return NIVELES[lvl][s_lang];
}

const char *i18n_metric(air_metric_t m)
{
    if (m < 0 || m >= AIR_METRIC_COUNT) return "?";
    return METRICAS[m][s_lang];
}
