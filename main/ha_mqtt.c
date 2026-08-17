#include "ha_mqtt.h"
#include "net.h"
#include "settings.h"
#include "version.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "mqtt_client.h"

static const char *TAG = "ha_mqtt";

static esp_mqtt_client_handle_t s_client;
static bool s_connected;
static char s_state_topic[64];
static char s_avty_topic[64];

// Clase de dispositivo de HA por metrica. Vacio = sin clase (HA no tiene
// clase para PM4 ni para los indices VOC/NOx de Sensirion).
static const char *device_class(air_metric_t m)
{
    switch (m) {
    case AIR_PM1:  return "pm1";
    case AIR_PM25: return "pm25";
    case AIR_PM10: return "pm10";
    case AIR_HUM:  return "humidity";
    case AIR_TEMP: return "temperature";
    case AIR_CO2:  return "carbon_dioxide";
    default:       return "";
    }
}

static const char *icon(air_metric_t m)
{
    switch (m) {
    case AIR_VOC:  return "mdi:air-filter";
    case AIR_NOX:  return "mdi:molecule";
    case AIR_PM4:  return "mdi:blur";
    default:       return "";
    }
}

// Bloque "device" comun a todas las entidades: es lo que hace que HA las
// agrupe en un solo aparato en vez de en nueve sueltas.
static int append_device(char *buf, size_t len)
{
    const settings_t *cfg = settings_get();
    return snprintf(buf, len,
        "\"dev\":{\"ids\":[\"%s\"],\"name\":\"%s\","
        "\"mdl\":\"SEN66 + ESP32-S3 AMOLED 1.75\",\"mf\":\"DIY\",\"sw\":\"%s\"}",
        net_device_id(), cfg->device_name, APP_VERSION);
}

static void publish_discovery_one(air_metric_t m)
{
    const settings_t *cfg = settings_get();
    const char *key = air_metric_key(m);
    char topic[160];
    char payload[640];

    snprintf(topic, sizeof(topic), "%s/sensor/%s/%s/config",
             cfg->mqtt_prefix, net_device_id(), key);

    int n = snprintf(payload, sizeof(payload),
        "{\"name\":\"%s\",\"uniq_id\":\"%s_%s\","
        "\"stat_t\":\"%s\",\"avty_t\":\"%s\","
        "\"val_tpl\":\"{{ value_json.%s }}\","
        "\"stat_cla\":\"measurement\",\"sug_dsp_prc\":%d,",
        air_metric_label(m), net_device_id(), key,
        s_state_topic, s_avty_topic, key, air_metric_decimals(m));

    if (air_metric_unit_ha(m)[0]) {
        n += snprintf(payload + n, sizeof(payload) - n,
                      "\"unit_of_meas\":\"%s\",", air_metric_unit_ha(m));
    }
    if (device_class(m)[0]) {
        n += snprintf(payload + n, sizeof(payload) - n,
                      "\"dev_cla\":\"%s\",", device_class(m));
    }
    if (icon(m)[0]) {
        n += snprintf(payload + n, sizeof(payload) - n, "\"ic\":\"%s\",", icon(m));
    }
    n += append_device(payload + n, sizeof(payload) - n);
    n += snprintf(payload + n, sizeof(payload) - n, "}");

    esp_mqtt_client_publish(s_client, topic, payload, n, 0, 1 /* retenido */);
}

// Entidades extra que no salen del sensor: el semaforo global y la cobertura.
static void publish_discovery_extras(void)
{
    const settings_t *cfg = settings_get();
    char topic[160], payload[640];
    int n;

    snprintf(topic, sizeof(topic), "%s/sensor/%s/level/config",
             cfg->mqtt_prefix, net_device_id());
    n = snprintf(payload, sizeof(payload),
        "{\"name\":\"Calidad del aire\",\"uniq_id\":\"%s_level\","
        "\"stat_t\":\"%s\",\"avty_t\":\"%s\",\"val_tpl\":\"{{ value_json.level }}\","
        "\"ic\":\"mdi:weather-windy\",",
        net_device_id(), s_state_topic, s_avty_topic);
    n += append_device(payload + n, sizeof(payload) - n);
    n += snprintf(payload + n, sizeof(payload) - n, "}");
    esp_mqtt_client_publish(s_client, topic, payload, n, 0, 1);

    snprintf(topic, sizeof(topic), "%s/sensor/%s/rssi/config",
             cfg->mqtt_prefix, net_device_id());
    n = snprintf(payload, sizeof(payload),
        "{\"name\":\"WiFi\",\"uniq_id\":\"%s_rssi\","
        "\"stat_t\":\"%s\",\"avty_t\":\"%s\",\"val_tpl\":\"{{ value_json.rssi }}\","
        "\"unit_of_meas\":\"dBm\",\"dev_cla\":\"signal_strength\","
        "\"stat_cla\":\"measurement\",\"ent_cat\":\"diagnostic\",",
        net_device_id(), s_state_topic, s_avty_topic);
    n += append_device(payload + n, sizeof(payload) - n);
    n += snprintf(payload + n, sizeof(payload) - n, "}");
    esp_mqtt_client_publish(s_client, topic, payload, n, 0, 1);
}

static void publish_discovery(void)
{
    for (int m = 0; m < AIR_METRIC_COUNT; m++) publish_discovery_one((air_metric_t)m);
    publish_discovery_extras();
    ESP_LOGI(TAG, "descubrimiento publicado (%d entidades)", AIR_METRIC_COUNT + 2);
}

static void mqtt_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)base;
    const esp_mqtt_event_handle_t e = data;
    switch ((esp_mqtt_event_id_t)id) {
    case MQTT_EVENT_CONNECTED:
        s_connected = true;
        ESP_LOGI(TAG, "conectado al broker");
        esp_mqtt_client_publish(s_client, s_avty_topic, "online", 0, 1, 1);
        publish_discovery();
        break;
    case MQTT_EVENT_DISCONNECTED:
        s_connected = false;
        ESP_LOGW(TAG, "desconectado del broker");
        break;
    case MQTT_EVENT_ERROR:
        if (e && e->error_handle) {
            ESP_LOGW(TAG, "error mqtt (tipo %d)", e->error_handle->error_type);
        }
        break;
    default:
        break;
    }
}

void ha_mqtt_start(void)
{
    const settings_t *cfg = settings_get();
    if (cfg->mqtt_uri[0] == '\0') {
        ESP_LOGW(TAG, "sin broker configurado, no publico");
        return;
    }
    if (s_client) return;

    snprintf(s_state_topic, sizeof(s_state_topic), "%s/state", net_device_id());
    snprintf(s_avty_topic, sizeof(s_avty_topic), "%s/status", net_device_id());

    const esp_mqtt_client_config_t mcfg = {
        .broker.address.uri = cfg->mqtt_uri,
        .credentials.username = cfg->mqtt_user[0] ? cfg->mqtt_user : NULL,
        .credentials.authentication.password = cfg->mqtt_pass[0] ? cfg->mqtt_pass : NULL,
        .credentials.client_id = net_device_id(),
        .session.last_will = {
            .topic = s_avty_topic,
            .msg = "offline",
            .qos = 1,
            .retain = true,
        },
        .session.keepalive = 30,
        .network.reconnect_timeout_ms = 5000,
    };
    s_client = esp_mqtt_client_init(&mcfg);
    if (!s_client) {
        ESP_LOGE(TAG, "no se pudo crear el cliente para '%s'", cfg->mqtt_uri);
        return;
    }
    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event, NULL);
    esp_mqtt_client_start(s_client);
    ESP_LOGI(TAG, "cliente arrancado hacia %s", cfg->mqtt_uri);
}

void ha_mqtt_stop(void)
{
    if (!s_client) return;
    esp_mqtt_client_publish(s_client, s_avty_topic, "offline", 0, 1, 1);
    esp_mqtt_client_stop(s_client);
    esp_mqtt_client_destroy(s_client);
    s_client = NULL;
    s_connected = false;
}

bool ha_mqtt_connected(void) { return s_connected; }

void ha_mqtt_publish(const air_sample_t *s)
{
    if (!s_client || !s_connected || !s->valid) return;

    char json[420];
    int n = snprintf(json, sizeof(json), "{");
    for (int m = 0; m < AIR_METRIC_COUNT; m++) {
        // Las metricas sin dato se omiten: HA conserva el ultimo valor
        // conocido en vez de mostrar "desconocido" mientras el sensor calienta.
        if (isnan(s->v[m])) continue;
        n += snprintf(json + n, sizeof(json) - n, "%s\"%s\":%.*f",
                      n > 1 ? "," : "", air_metric_key((air_metric_t)m),
                      air_metric_decimals((air_metric_t)m), s->v[m]);
    }
    n += snprintf(json + n, sizeof(json) - n, "%s\"level\":\"%s\",\"rssi\":%d}",
                  n > 1 ? "," : "", air_level_text(air_overall(s)), net_rssi());

    esp_mqtt_client_publish(s_client, s_state_topic, json, n, 0, 0);
}
