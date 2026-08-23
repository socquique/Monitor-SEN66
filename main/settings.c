#include "settings.h"

#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "settings";

#define NVS_NS   "sen66"
#define NVS_KEY  "cfg"
// Subir esta version invalida lo guardado y vuelve a los valores por defecto.
#define CFG_VERSION 1

static settings_t s_cfg;

void settings_defaults(settings_t *s)
{
    memset(s, 0, sizeof(*s));
    strcpy(s->mqtt_prefix, "homeassistant");
    strcpy(s->device_name, "Monitor SEN66");
    // Espana peninsular; el cambio de horario lo hace la libc sola.
    strcpy(s->tz, "CET-1CEST,M3.5.0,M10.5.0/3");
    strcpy(s->ntp, "pool.ntp.org");
    s->brightness = 180;
    s->night_brightness = 20;
    s->screen_timeout_s = 0;      // por defecto siempre encendida: es un monitor
    s->page_dwell_s = 12;
    s->pages_mask = (1 << SETTINGS_PAGES) - 1;
    s->chart_span_min = 60;
    s->temp_offset_dc = 0;
    s->altitude_m = 0;
    s->co2_asc = true;
    s->alarm_enabled = true;
    s->alarm_co2_ppm = 1200;
    s->alarm_clear_ppm = 1000; // histeresis: no pita cada vez que roza el umbral
    s->alarm_volume = 60;
    strcpy(s->lang, "es");
    // Provisional: sacado de una sola comparacion contra un Qingping, que no
    // es un patron. Sirve para que el numero sea plausible, no exacto.
    s->noise_offset_db = 120;
}

esp_err_t settings_load(void)
{
    settings_defaults(&s_cfg);

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_RETURN_ON_ERROR(err, TAG, "nvs init");

    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        ESP_LOGI(TAG, "sin configuracion guardada, usando valores por defecto");
        return ESP_OK;
    }

    uint8_t ver = 0;
    // Partimos de los valores por defecto y dejamos que lo guardado los pise.
    // Aceptamos blobs MAS CORTOS que la estructura actual: mientras los campos
    // nuevos se anadan siempre al final, actualizar el firmware no obliga a
    // reconfigurar la red y el broker, que es lo que pasaba antes.
    settings_t tmp = s_cfg;
    size_t len = sizeof(tmp);
    if (nvs_get_u8(h, "ver", &ver) == ESP_OK && ver == CFG_VERSION &&
        nvs_get_blob(h, NVS_KEY, &tmp, &len) == ESP_OK && len <= sizeof(tmp)) {
        if (len < sizeof(tmp)) {
            ESP_LOGI(TAG, "configuracion de una version anterior (%u de %u bytes):"
                          " los campos nuevos quedan por defecto",
                     (unsigned)len, (unsigned)sizeof(tmp));
        }
        s_cfg = tmp;
        ESP_LOGI(TAG, "configuracion cargada (wifi='%s', mqtt='%s')",
                 s_cfg.wifi_ssid, s_cfg.mqtt_uri);
    } else {
        ESP_LOGW(TAG, "configuracion incompatible (v%u), volviendo a los valores por defecto", ver);
    }
    nvs_close(h);
    return ESP_OK;
}

esp_err_t settings_save(void)
{
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NS, NVS_READWRITE, &h), TAG, "open");
    esp_err_t err = nvs_set_blob(h, NVS_KEY, &s_cfg, sizeof(s_cfg));
    if (err == ESP_OK) err = nvs_set_u8(h, "ver", CFG_VERSION);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err == ESP_OK) ESP_LOGI(TAG, "configuracion guardada");
    return err;
}

settings_t *settings_get(void) { return &s_cfg; }

#define NVS_KEY_VOC "voc_state"

esp_err_t settings_save_voc_state(const void *state, size_t len)
{
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NS, NVS_READWRITE, &h), TAG, "open");
    esp_err_t err = nvs_set_blob(h, NVS_KEY_VOC, state, len);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

esp_err_t settings_load_voc_state(void *state, size_t len)
{
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NS, NVS_READONLY, &h), TAG, "open");
    size_t got = len;
    esp_err_t err = nvs_get_blob(h, NVS_KEY_VOC, state, &got);
    nvs_close(h);
    if (err == ESP_OK && got != len) return ESP_ERR_INVALID_SIZE;
    return err;
}
