#include "webcfg.h"
#include "display.h"
#include "ha_mqtt.h"
#include "net.h"
#include "sen66.h"
#include "settings.h"
#include "version.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_check.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "webcfg";

static httpd_handle_t s_server;
static webcfg_sample_fn s_get_sample;

// ------------------------------------------------------------------- pagina
// Una sola pagina, sin dependencias externas (no hay internet en el portal).
static const char k_page[] =
"<!doctype html><html lang=es><meta charset=utf-8>"
"<meta name=viewport content='width=device-width,initial-scale=1'>"
"<title>Monitor SEN66</title><style>"
"*{box-sizing:border-box}body{margin:0;padding:18px;font:15px/1.5 system-ui,sans-serif;"
"background:#0f172a;color:#e2e8f0}h1{font-size:20px;margin:0 0 4px}"
"h2{font-size:15px;margin:22px 0 8px;color:#94a3b8;text-transform:uppercase;"
"letter-spacing:.06em}.card{background:#1e293b;border-radius:12px;padding:14px;"
"margin-bottom:14px}label{display:block;margin:8px 0 2px;color:#94a3b8;font-size:13px}"
"input,select{width:100%;padding:8px;border-radius:8px;border:1px solid #334155;"
"background:#0f172a;color:#e2e8f0}button{margin-top:14px;padding:10px 16px;"
"border:0;border-radius:8px;background:#2563eb;color:#fff;font-weight:600;cursor:pointer}"
"button.alt{background:#334155}.grid{display:grid;grid-template-columns:1fr 1fr;gap:10px}"
"table{width:100%;border-collapse:collapse}td{padding:4px 0;border-bottom:1px solid #334155}"
"td:last-child{text-align:right;font-variant-numeric:tabular-nums}"
".ok{color:#22c55e}.warn{color:#facc15}#msg{margin-top:10px;color:#facc15}"
"</style><h1>Monitor SEN66</h1><div id=sub style='color:#64748b'></div>"
"<h2>Ahora mismo</h2><div class=card><table id=st></table></div>"
"<h2>Configuracion</h2><div class=card><form id=f>"
"<div class=grid><div><label>Red WiFi</label><input name=wifi_ssid></div>"
"<div><label>Contrasena WiFi</label><input name=wifi_pass type=password placeholder='(sin cambios)'></div></div>"
"<label>Broker MQTT</label><input name=mqtt_uri placeholder='mqtt://192.168.1.10:1883'>"
"<div class=grid><div><label>Usuario MQTT</label><input name=mqtt_user></div>"
"<div><label>Contrasena MQTT</label><input name=mqtt_pass type=password placeholder='(sin cambios)'></div></div>"
"<div class=grid><div><label>Prefijo de descubrimiento</label><input name=mqtt_prefix></div>"
"<div><label>Nombre en Home Assistant</label><input name=device_name></div></div>"
"<div class=grid><div><label>Zona horaria (POSIX TZ)</label><input name=tz></div>"
"<div><label>Servidor NTP</label><input name=ntp></div></div>"
"<div class=grid><div><label>Brillo (1-255)</label><input name=brightness type=number min=1 max=255></div>"
"<div><label>Brillo atenuado</label><input name=night_brightness type=number min=0 max=255></div></div>"
"<div class=grid><div><label>Atenuar tras (s, 0=nunca)</label><input name=screen_timeout_s type=number min=0></div>"
"<div><label>Cambio de pagina (s, 0=manual)</label><input name=page_dwell_s type=number min=0></div></div>"
"<div class=grid><div><label>Ventana de graficas (min)</label><input name=chart_span_min type=number min=5 max=1440></div>"
"<div><label>Paginas visibles</label><input name=pages_mask type=number min=1 max=31></div></div>"
"<div class=grid><div><label>Correccion de temperatura (C)</label><input name=temp_offset type=number step=0.1></div>"
"<div><label>Altitud (m)</label><input name=altitude_m type=number min=0 max=3000></div></div>"
"<label>Autocalibracion del CO2</label><select name=co2_asc>"
"<option value=1>activada</option><option value=0>desactivada</option></select>"
"<button type=submit>Guardar y reiniciar</button></form><div id=msg></div></div>"
"<h2>Mantenimiento</h2><div class=card>"
"<button class=alt onclick=\"post('/api/fanclean')\">Limpiar ventilador</button> "
"<button class=alt onclick=\"post('/api/reboot')\">Reiniciar</button>"
"<label style='margin-top:14px'>Actualizar firmware (.bin)</label>"
"<input type=file id=fw accept='.bin'>"
"<button class=alt onclick=ota()>Subir e instalar</button>"
"<div id=otamsg></div></div>"
"<script>"
"const M={pm1:'PM1.0',pm25:'PM2.5',pm4:'PM4.0',pm10:'PM10',co2:'CO2',"
"voc:'VOC',nox:'NOx',temperature:'Temperatura',humidity:'Humedad'};"
"async function refresh(){const s=await(await fetch('/api/state')).json();"
"document.getElementById('sub').textContent=s.name+' - v'+s.version+' - '+s.id+' - '+s.ip;"
"let h='';for(const k in M){const v=s[k];h+='<tr><td>'+M[k]+'</td><td>'+"
"(v==null?'--':v)+'</td></tr>'}"
"h+='<tr><td>Calidad</td><td>'+(s.level||'--')+'</td></tr>';"
"h+='<tr><td>Sensor</td><td class='+(s.sensor=='OK'?'ok':'warn')+'>'+s.sensor+'</td></tr>';"
"h+='<tr><td>MQTT</td><td class='+(s.mqtt?'ok':'warn')+'>'+(s.mqtt?'conectado':'sin conexion')+'</td></tr>';"
"document.getElementById('st').innerHTML=h}"
"async function load(){const c=await(await fetch('/api/settings')).json();"
"for(const [k,v] of Object.entries(c)){const e=document.forms.f[k];if(e)e.value=v}}"
"document.getElementById('f').onsubmit=async ev=>{ev.preventDefault();"
"const d=Object.fromEntries(new FormData(ev.target));"
"const r=await fetch('/api/settings',{method:'POST',body:JSON.stringify(d)});"
"document.getElementById('msg').textContent=r.ok?'Guardado. Reiniciando...':'Error al guardar'};"
"async function post(u){const r=await fetch(u,{method:'POST'});"
"document.getElementById('msg').textContent=r.ok?'Hecho':'Error'}"
"async function ota(){const f=document.getElementById('fw').files[0];if(!f)return;"
"const m=document.getElementById('otamsg');m.textContent='Subiendo '+f.size+' bytes...';"
"const r=await fetch('/api/ota',{method:'POST',body:f});"
"m.textContent=r.ok?'Instalado, reiniciando...':'Error: '+await r.text()}"
"load();refresh();setInterval(refresh,5000);"
"</script></html>";

static esp_err_t h_root(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, k_page, HTTPD_RESP_USE_STRLEN);
}

// -------------------------------------------------------------------- estado
static esp_err_t h_state(httpd_req_t *req)
{
    air_sample_t s;
    air_sample_clear(&s);
    if (s_get_sample) s_get_sample(&s);

    uint32_t status = 0;
    char sensor[96];
    if (!sen66_present()) {
        snprintf(sensor, sizeof(sensor), "no detectado");
    } else if (sen66_read_status(&status) == ESP_OK) {
        sen66_status_text(status, sensor, sizeof(sensor));
    } else {
        snprintf(sensor, sizeof(sensor), "sin respuesta");
    }

    char json[720];
    int n = snprintf(json, sizeof(json),
        "{\"name\":\"%s\",\"version\":\"%s\",\"id\":\"%s\",\"ip\":\"%s\","
        "\"rssi\":%d,\"mqtt\":%s,\"sensor\":\"%s\",\"age_s\":%u,",
        settings_get()->device_name, APP_VERSION, net_device_id(),
        net_ip()[0] ? net_ip() : "sin IP", net_rssi(),
        ha_mqtt_connected() ? "true" : "false", sensor, (unsigned)s.age_s);

    for (int m = 0; m < AIR_METRIC_COUNT; m++) {
        const char *key = air_metric_key((air_metric_t)m);
        if (isnan(s.v[m])) {
            n += snprintf(json + n, sizeof(json) - n, "\"%s\":null,", key);
        } else {
            n += snprintf(json + n, sizeof(json) - n, "\"%s\":%.*f,", key,
                          air_metric_decimals((air_metric_t)m), s.v[m]);
        }
    }
    n += snprintf(json + n, sizeof(json) - n, "\"level\":\"%s\"}",
                  s.valid ? air_level_text(air_overall(&s)) : "");

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json, n);
}

// ------------------------------------------------------------- configuracion
static esp_err_t h_settings_get(httpd_req_t *req)
{
    const settings_t *c = settings_get();
    char json[640];
    // Las contrasenas nunca se devuelven; el formulario las deja en blanco y
    // solo se cambian si se escribe algo.
    int n = snprintf(json, sizeof(json),
        "{\"wifi_ssid\":\"%s\",\"mqtt_uri\":\"%s\",\"mqtt_user\":\"%s\","
        "\"mqtt_prefix\":\"%s\",\"device_name\":\"%s\",\"tz\":\"%s\",\"ntp\":\"%s\","
        "\"brightness\":%u,\"night_brightness\":%u,\"screen_timeout_s\":%u,"
        "\"page_dwell_s\":%u,\"chart_span_min\":%u,\"pages_mask\":%u,"
        "\"temp_offset\":%.1f,\"altitude_m\":%u,\"co2_asc\":%d}",
        c->wifi_ssid, c->mqtt_uri, c->mqtt_user, c->mqtt_prefix, c->device_name,
        c->tz, c->ntp, c->brightness, c->night_brightness, c->screen_timeout_s,
        c->page_dwell_s, c->chart_span_min, c->pages_mask,
        c->temp_offset_dc / 10.0f, c->altitude_m, c->co2_asc ? 1 : 0);

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json, n);
}

static void copy_str(const cJSON *root, const char *key, char *dst, size_t len,
                     bool skip_if_empty)
{
    const cJSON *it = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!cJSON_IsString(it) || !it->valuestring) return;
    if (skip_if_empty && it->valuestring[0] == '\0') return;
    strlcpy(dst, it->valuestring, len);
}

// Los <input> llegan como cadenas aunque sean numericos, asi que se aceptan
// las dos formas.
static bool get_num(const cJSON *root, const char *key, double *out)
{
    const cJSON *it = cJSON_GetObjectItemCaseSensitive(root, key);
    if (cJSON_IsNumber(it)) { *out = it->valuedouble; return true; }
    if (cJSON_IsString(it) && it->valuestring && it->valuestring[0]) {
        *out = atof(it->valuestring);
        return true;
    }
    return false;
}

static void reboot_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(700)); // deja salir la respuesta HTTP
    esp_restart();
}

static esp_err_t h_settings_post(httpd_req_t *req)
{
    if (req->content_len == 0 || req->content_len > 2048) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "cuerpo invalido");
        return ESP_FAIL;
    }
    char *body = malloc(req->content_len + 1);
    if (!body) return ESP_FAIL;

    int got = 0;
    while (got < (int)req->content_len) {
        int r = httpd_req_recv(req, body + got, req->content_len - got);
        if (r <= 0) { free(body); return ESP_FAIL; }
        got += r;
    }
    body[got] = '\0';

    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "JSON invalido");
        return ESP_FAIL;
    }

    settings_t *c = settings_get();
    copy_str(root, "wifi_ssid", c->wifi_ssid, sizeof(c->wifi_ssid), false);
    copy_str(root, "wifi_pass", c->wifi_pass, sizeof(c->wifi_pass), true);
    copy_str(root, "mqtt_uri", c->mqtt_uri, sizeof(c->mqtt_uri), false);
    copy_str(root, "mqtt_user", c->mqtt_user, sizeof(c->mqtt_user), false);
    copy_str(root, "mqtt_pass", c->mqtt_pass, sizeof(c->mqtt_pass), true);
    copy_str(root, "mqtt_prefix", c->mqtt_prefix, sizeof(c->mqtt_prefix), true);
    copy_str(root, "device_name", c->device_name, sizeof(c->device_name), true);
    copy_str(root, "tz", c->tz, sizeof(c->tz), true);
    copy_str(root, "ntp", c->ntp, sizeof(c->ntp), true);

    double d;
    if (get_num(root, "brightness", &d))       c->brightness = (uint8_t)(d < 1 ? 1 : (d > 255 ? 255 : d));
    if (get_num(root, "night_brightness", &d)) c->night_brightness = (uint8_t)(d < 0 ? 0 : (d > 255 ? 255 : d));
    if (get_num(root, "screen_timeout_s", &d)) c->screen_timeout_s = (uint16_t)(d < 0 ? 0 : d);
    if (get_num(root, "page_dwell_s", &d))     c->page_dwell_s = (uint16_t)(d < 0 ? 0 : d);
    if (get_num(root, "chart_span_min", &d))   c->chart_span_min = (uint16_t)(d < 5 ? 5 : (d > 1440 ? 1440 : d));
    if (get_num(root, "pages_mask", &d))       c->pages_mask = (uint8_t)(d < 1 ? 1 : (d > 31 ? 31 : d));
    if (get_num(root, "temp_offset", &d))      c->temp_offset_dc = (int16_t)lrint(d * 10.0);
    if (get_num(root, "altitude_m", &d))       c->altitude_m = (uint16_t)(d < 0 ? 0 : (d > 3000 ? 3000 : d));
    if (get_num(root, "co2_asc", &d))          c->co2_asc = (d != 0);
    cJSON_Delete(root);

    display_set_brightness(c->brightness); // esto si se nota al instante

    esp_err_t err = settings_save();
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no se pudo guardar");
        return ESP_FAIL;
    }
    httpd_resp_sendstr(req, "ok");
    // Reiniciar es la forma honesta de aplicar red, MQTT, paginas y ajustes
    // del sensor de una vez, sin medio estado a medias.
    xTaskCreate(reboot_task, "reboot", 2048, NULL, 5, NULL);
    return ESP_OK;
}

// ----------------------------------------------------------- mantenimiento
static esp_err_t h_fanclean(httpd_req_t *req)
{
    if (sen66_fan_clean() != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "el sensor no responde");
        return ESP_FAIL;
    }
    return httpd_resp_sendstr(req, "limpiando (10 s)");
}

static esp_err_t h_reboot(httpd_req_t *req)
{
    httpd_resp_sendstr(req, "ok");
    xTaskCreate(reboot_task, "reboot", 2048, NULL, 5, NULL);
    return ESP_OK;
}

// --------------------------------------------------------------------- OTA
static esp_err_t h_ota(httpd_req_t *req)
{
    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    if (!part) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "sin particion OTA");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "OTA a '%s', %d bytes", part->label, req->content_len);

    esp_ota_handle_t ota;
    if (esp_ota_begin(part, req->content_len, &ota) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "ota_begin");
        return ESP_FAIL;
    }

    char buf[1024];
    int remaining = req->content_len;
    while (remaining > 0) {
        int r = httpd_req_recv(req, buf, remaining < (int)sizeof(buf) ? remaining : (int)sizeof(buf));
        if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (r <= 0) {
            esp_ota_abort(ota);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "subida cortada");
            return ESP_FAIL;
        }
        if (esp_ota_write(ota, buf, r) != ESP_OK) {
            esp_ota_abort(ota);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "ota_write");
            return ESP_FAIL;
        }
        remaining -= r;
    }

    if (esp_ota_end(ota) != ESP_OK || esp_ota_set_boot_partition(part) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "imagen no valida");
        return ESP_FAIL;
    }
    httpd_resp_sendstr(req, "ok");
    xTaskCreate(reboot_task, "reboot", 2048, NULL, 5, NULL);
    return ESP_OK;
}

// -------------------------------------------------------------------- inicio
esp_err_t webcfg_start(webcfg_sample_fn get_sample)
{
    s_get_sample = get_sample;

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size = 8192;          // el OTA y cJSON necesitan holgura
    cfg.max_uri_handlers = 10;
    cfg.lru_purge_enable = true;
    cfg.recv_wait_timeout = 10;
    cfg.send_wait_timeout = 10;
    ESP_RETURN_ON_ERROR(httpd_start(&s_server, &cfg), TAG, "httpd");

    static const httpd_uri_t routes[] = {
        {.uri = "/",              .method = HTTP_GET,  .handler = h_root},
        {.uri = "/api/state",     .method = HTTP_GET,  .handler = h_state},
        {.uri = "/api/settings",  .method = HTTP_GET,  .handler = h_settings_get},
        {.uri = "/api/settings",  .method = HTTP_POST, .handler = h_settings_post},
        {.uri = "/api/fanclean",  .method = HTTP_POST, .handler = h_fanclean},
        {.uri = "/api/reboot",    .method = HTTP_POST, .handler = h_reboot},
        {.uri = "/api/ota",       .method = HTTP_POST, .handler = h_ota},
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_server, &routes[i]),
                            TAG, "ruta %s", routes[i].uri);
    }
    // Sin IP todavia: arrancamos antes de que el DHCP conteste. Decir una
    // direccion concreta aqui seria mentir (lo hacia: cantaba 192.168.4.1
    // incluso conectado a la red de casa). La IP real la canta net.c en
    // cuanto llega el GOT_IP; en el portal es siempre 192.168.4.1.
    ESP_LOGI(TAG, "servidor web escuchando en el puerto %d", cfg.server_port);
    return ESP_OK;
}
