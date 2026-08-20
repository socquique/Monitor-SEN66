#include "net.h"
#include "settings.h"

#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "net";

// Tras esta racha de fallos abrimos ademas el punto de acceso, para poder
// corregir la contrasena sin tener que reflashear.
#define FAILS_BEFORE_PORTAL 8

static net_state_t s_state = NET_IDLE;
static char s_ip[16] = "";
static char s_dev_id[24] = "sen66";
static char s_ap_ssid[24] = "SEN66-setup";
static int s_fails;
static bool s_portal_up;
static void (*s_time_cb)(void);
static esp_netif_t *s_sta_netif;
static esp_netif_t *s_ap_netif;
static esp_timer_handle_t s_retry_timer;

static void start_portal(void);

static void retry_connect_cb(void *arg)
{
    (void)arg;
    esp_wifi_connect();
}

static void on_time_sync(struct timeval *tv)
{
    (void)tv;
    ESP_LOGI(TAG, "hora sincronizada por SNTP");
    if (s_time_cb) s_time_cb();
}

static void start_sntp(void)
{
    static bool started;
    if (started) return;
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG(settings_get()->ntp);
    cfg.sync_cb = on_time_sync;
    cfg.start = true;
    if (esp_netif_sntp_init(&cfg) == ESP_OK) started = true;
}

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)data;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        s_state = NET_CONNECTING;
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_ip[0] = '\0';
        s_state = s_portal_up ? NET_PORTAL : NET_CONNECTING;
        if (++s_fails == FAILS_BEFORE_PORTAL && !s_portal_up) {
            ESP_LOGW(TAG, "%d fallos seguidos: abro el portal de configuracion", s_fails);
            start_portal();
        }
        // Espera creciente hasta 10 s para no machacar el router ni la radio.
        // Con un temporizador y NO con vTaskDelay: esto corre en la tarea del
        // bucle de eventos, y dormirla retrasaria el GOT_IP y los eventos MQTT.
        int wait_ms = 500 * (s_fails < 20 ? s_fails : 20);
        if (wait_ms > 10000) wait_ms = 10000;
        esp_timer_stop(s_retry_timer);
        esp_timer_start_once(s_retry_timer, (uint64_t)wait_ms * 1000);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *e = (const ip_event_got_ip_t *)data;
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&e->ip_info.ip));
        s_fails = 0;
        s_state = NET_CONNECTED;
        ESP_LOGI(TAG, "conectado, IP %s", s_ip);
        start_sntp();
    }
}

esp_err_t net_init(void)
{
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "event loop");
    s_sta_netif = esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "wifi init");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                            wifi_event, NULL, NULL),
                        TAG, "ev wifi");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                            wifi_event, NULL, NULL),
                        TAG, "ev ip");

    const esp_timer_create_args_t retry_args = {
        .callback = retry_connect_cb,
        .name = "wifi_retry",
    };
    ESP_RETURN_ON_ERROR(esp_timer_create(&retry_args, &s_retry_timer), TAG, "timer");

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(s_dev_id, sizeof(s_dev_id), "sen66-%02x%02x%02x", mac[3], mac[4], mac[5]);
    snprintf(s_ap_ssid, sizeof(s_ap_ssid), "SEN66-%02X%02X%02X", mac[3], mac[4], mac[5]);
    ESP_LOGI(TAG, "id de dispositivo: %s", s_dev_id);
    return ESP_OK;
}

// Punto de acceso abierto solo para configurar; el servidor web responde en
// 192.168.4.1. No lleva contrasena a proposito: no da salida a internet y
// solo esta activo mientras no haya una red configurada que funcione.
static void start_portal(void)
{
    if (s_portal_up) return;
    if (!s_ap_netif) s_ap_netif = esp_netif_create_default_wifi_ap();

    wifi_config_t ap = {0};
    strlcpy((char *)ap.ap.ssid, s_ap_ssid, sizeof(ap.ap.ssid));
    ap.ap.ssid_len = strlen(s_ap_ssid);
    ap.ap.channel = 1;
    ap.ap.max_connection = 3;
    ap.ap.authmode = WIFI_AUTH_OPEN;

    // Conservar la estacion si ya estaba levantada (portal de rescate tras
    // varios fallos); si no, solo AP.
    wifi_mode_t mode = WIFI_MODE_NULL;
    esp_wifi_get_mode(&mode);
    const wifi_mode_t want = (mode == WIFI_MODE_STA || mode == WIFI_MODE_APSTA)
                             ? WIFI_MODE_APSTA : WIFI_MODE_AP;
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_mode(want));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_config(WIFI_IF_AP, &ap));

    // Arrancar SIEMPRE: si ya estaba en marcha esto es un no-op. Antes se
    // llamaba solo con el modo en NULL, pero net_start() ya lo habia puesto
    // en AP, asi que la radio no llegaba a arrancar y no habia portal.
    const esp_err_t err = esp_wifi_start();
    if (err != ESP_OK && err != ESP_ERR_WIFI_CONN) {
        ESP_LOGW(TAG, "esp_wifi_start en el portal: %s", esp_err_to_name(err));
    }

    s_portal_up = true;
    if (s_state != NET_CONNECTED) s_state = NET_PORTAL;
    ESP_LOGW(TAG, "portal de configuracion abierto: red '%s' -> http://192.168.4.1",
             s_ap_ssid);
}

esp_err_t net_start(void)
{
    const settings_t *cfg = settings_get();

    if (cfg->wifi_ssid[0] == '\0') {
        ESP_LOGW(TAG, "sin red configurada");
        start_portal(); // pone el modo y arranca la radio
        return ESP_OK;
    }

    wifi_config_t sta = {0};
    strlcpy((char *)sta.sta.ssid, cfg->wifi_ssid, sizeof(sta.sta.ssid));
    strlcpy((char *)sta.sta.password, cfg->wifi_pass, sizeof(sta.sta.password));
    sta.sta.threshold.authmode = cfg->wifi_pass[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "modo sta");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &sta), TAG, "config sta");
    // Sin ahorro de energia: con DTIM el ping y MQTT se vuelven erraticos y
    // este aparato vive enchufado.
    esp_wifi_set_ps(WIFI_PS_NONE);
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "start");
    ESP_LOGI(TAG, "conectando a '%s'", cfg->wifi_ssid);
    return ESP_OK;
}

net_state_t net_state(void) { return s_state; }
const char *net_ip(void) { return s_ip; }
const char *net_device_id(void) { return s_dev_id; }
const char *net_ap_ssid(void) { return s_ap_ssid; }

int net_rssi(void)
{
    wifi_ap_record_t ap;
    if (s_state == NET_CONNECTED && esp_wifi_sta_get_ap_info(&ap) == ESP_OK) return ap.rssi;
    return 0;
}

void net_set_power_save(bool enabled)
{
    esp_wifi_set_ps(enabled ? WIFI_PS_MIN_MODEM : WIFI_PS_NONE);
    ESP_LOGI(TAG, "ahorro de radio %s", enabled ? "activado" : "desactivado");
}

void net_set_time_sync_cb(void (*cb)(void)) { s_time_cb = cb; }
