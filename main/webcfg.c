#include "webcfg.h"
#include "display.h"
#include "ha_mqtt.h"
#include "mic.h"
#include "net.h"
#include "pmu_axp2101.h"
#include "sound.h"
#include "ui.h"
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
static webcfg_recal_fn s_recal_request;
static webcfg_recal_status_fn s_recal_status;
static webcfg_fan_fn s_fan;

void webcfg_set_fan(webcfg_fan_fn fn) { s_fan = fn; }

void webcfg_set_co2_recal(webcfg_recal_fn request, webcfg_recal_status_fn status)
{
    s_recal_request = request;
    s_recal_status = status;
}

// ------------------------------------------------------------------- pagina
// Una sola pagina, sin dependencias externas (no hay internet en el portal).
static const char k_page[] =
"<!doctype html><html lang=es><meta charset=utf-8>\n"
"<meta name=viewport content='width=device-width,initial-scale=1'>\n"
"<title>Monitor SEN66</title><style>\n"
"*{box-sizing:border-box}\n"
"body{margin:0;padding:16px;font:15px/1.55 system-ui,sans-serif;background:#0f172a;color:#e2e8f0;max-width:720px;margin-inline:auto}\n"
"h1{font-size:21px;margin:0 0 2px}\n"
"h2{font-size:13px;margin:24px 0 8px;color:#94a3b8;text-transform:uppercase;letter-spacing:.07em}\n"
".card{background:#1e293b;border-radius:14px;padding:16px;margin-bottom:12px}\n"
"label{display:block;margin:12px 0 4px;color:#cbd5e1;font-size:14px}\n"
"label:first-child{margin-top:0}\n"
".hint{color:#64748b;font-size:12.5px;margin-top:4px}\n"
"input,select{width:100%;padding:9px 10px;border-radius:9px;border:1px solid #334155;background:#0f172a;color:#e2e8f0;font-size:15px}\n"
"input[type=range]{padding:0;border:0;background:0;height:26px}\n"
"input[type=checkbox]{width:auto;margin:0 8px 0 0;transform:scale(1.25)}\n"
"button{margin-top:14px;padding:11px 18px;border:0;border-radius:9px;background:#2563eb;color:#fff;font-weight:600;font-size:15px;cursor:pointer}\n"
"button.alt{background:#334155}\n"
"button:hover{filter:brightness(1.12)}\n"
".grid{display:grid;grid-template-columns:1fr 1fr;gap:12px}\n"
"@media(max-width:540px){.grid{grid-template-columns:1fr}}\n"
"table{width:100%;border-collapse:collapse}\n"
"td{padding:6px 0;border-bottom:1px solid #334155}\n"
"td:last-child{text-align:right;font-variant-numeric:tabular-nums;font-weight:600}\n"
"tr:last-child td{border-bottom:0}\n"
".ok{color:#22c55e}.warn{color:#facc15}\n"
"#msg{margin-top:10px;color:#facc15}\n"
".pg{display:flex;align-items:center;margin:6px 0;color:#e2e8f0;font-size:14.5px}\n"
".val{float:right;color:#94a3b8;font-variant-numeric:tabular-nums}\n"
".row{display:flex;gap:10px;flex-wrap:wrap}\n"
".row button{margin-top:0}\n"
"</style>\n"
"<h1>Monitor SEN66</h1><div id=sub class=hint></div>\n"
"\n"
"<h2>Ahora mismo</h2><div class=card><table id=st></table></div>\n"
"\n"
"<h2>Red</h2><div class=card><form id=f>\n"
"<div class=grid>\n"
"<div><label>Red WiFi</label><input name=wifi_ssid autocomplete=off></div>\n"
"<div><label>Contrasena WiFi</label><input name=wifi_pass type=password placeholder='(sin cambios)' autocomplete=new-password></div>\n"
"</div>\n"
"<div class=grid>\n"
"<div><label>Zona horaria</label><select name=tz>\n"
"<option value='CET-1CEST,M3.5.0,M10.5.0/3'>Espana peninsular y Baleares</option>\n"
"<option value='WET0WEST,M3.5.0/1,M10.5.0'>Canarias</option>\n"
"<option value='CET-1CEST,M3.5.0,M10.5.0/3'>Europa central</option>\n"
"<option value='GMT0BST,M3.5.0/1,M10.5.0'>Reino Unido</option>\n"
"<option value='EST5EDT,M3.2.0,M11.1.0'>Nueva York</option>\n"
"<option value='UTC0'>UTC</option>\n"
"</select></div>\n"
"<div><label>Servidor de hora</label><input name=ntp></div>\n"
"</div>\n"
"<div class=hint>Si tu zona no esta en la lista, el campo acepta cualquier cadena POSIX TZ al restaurar una copia.</div>\n"
"\n"
"<h2 style='margin-top:22px'>Home Assistant y HomeKit</h2>\n"
"<label>Broker MQTT</label><input name=mqtt_uri placeholder='mqtt://192.168.1.10:1883'>\n"
"<div class=hint>Dejalo vacio si no quieres domotica: el aparato funciona solo igual.</div>\n"
"<div class=grid>\n"
"<div><label>Usuario</label><input name=mqtt_user autocomplete=off></div>\n"
"<div><label>Contrasena</label><input name=mqtt_pass type=password placeholder='(sin cambios)' autocomplete=new-password></div>\n"
"</div>\n"
"<div class=grid>\n"
"<div><label>Nombre del aparato</label><input name=device_name></div>\n"
"<div><label>Prefijo de descubrimiento</label><input name=mqtt_prefix placeholder=homeassistant></div>\n"
"</div>\n"
"<div class=hint>El prefijo hace que Home Assistant lo cree solo. Con Homebridge dejalo <b>vacio</b>.</div>\n"
"\n"
"<h2 style='margin-top:22px'>Pantalla</h2>\n"
"<label>Idioma</label><select name=lang>\n"
"<option value=es>Espanol</option><option value=en>English</option><option value=de>Deutsch</option>\n"
"</select>\n"
"<label>Brillo <span class=val id=bv></span></label>\n"
"<input name=brightness type=range min=1 max=255 oninput=vivo(this)>\n"
"<label>Brillo atenuado <span class=val id=nv></span></label>\n"
"<input name=night_brightness type=range min=0 max=255 oninput=vivo(this)>\n"
"<div class=hint>Se aplican al instante para que los veas; se guardan al pulsar Guardar.</div>\n"
"<div class=grid>\n"
"<div><label>Atenuar tras</label><select name=screen_timeout_s>\n"
"<option value=0>Nunca</option><option value=30>30 segundos</option>\n"
"<option value=45>45 segundos</option><option value=60>1 minuto</option>\n"
"<option value=300>5 minutos</option><option value=900>15 minutos</option>\n"
"</select></div>\n"
"<div><label>Ventana de las graficas</label><select name=chart_span_min>\n"
"<option value=30>30 minutos</option><option value=60>1 hora</option>\n"
"<option value=180>3 horas</option><option value=360>6 horas</option>\n"
"<option value=720>12 horas</option><option value=1440>24 horas</option>\n"
"</select></div>\n"
"</div>\n"
"<label>Paginas visibles</label>\n"
"<div class=pg><input type=checkbox id=pg0><span>Resumen</span></div>\n"
"<div class=pg><input type=checkbox id=pg1><span>CO2</span></div>\n"
"<div class=pg><input type=checkbox id=pg2><span>Particulas</span></div>\n"
"<div class=pg><input type=checkbox id=pg3><span>Gases (VOC y NOx)</span></div>\n"
"<div class=pg><input type=checkbox id=pg4><span>Clima</span></div>\n"
"<div class=pg><input type=checkbox id=pg5><span>Ruido</span></div>\n"
"\n"
"<h2 style='margin-top:22px'>Sensor</h2>\n"
"<div class=grid>\n"
"<div><label>Correccion de temperatura</label><input name=temp_offset type=number step=0.1></div>\n"
"<div><label>Altitud (m)</label><input name=altitude_m type=number min=0 max=3000></div>\n"
"</div>\n"
"<div class=hint>La correccion se SUMA a la lectura: si marca de mas, va en negativo. Antes de tocarla, comparala con un termometro que no sea otro medidor de aire.</div>\n"
"<label>Autocalibracion del CO2</label><select name=co2_asc>\n"
"<option value=1>Activada (recomendado)</option><option value=0>Desactivada</option>\n"
"</select>\n"
"<div class=hint>Da por hecho que el aparato ve aire fresco de vez en cuando. En un cuarto que nunca se ventila, desactivala.</div>\n"
"<label>Calibracion del ruido (dB)</label><input name=noise_offset_db type=number min=0 max=200>\n"
"<div class=hint>Se suma al nivel del microfono. Ajustala con un sonometro al lado y algo de ruido en la sala, no en silencio.</div>\n"
"\n"
"<h2 style='margin-top:22px'>Aviso sonoro</h2>\n"
"<label>Avisar cuando suba el CO2</label><select name=alarm_enabled>\n"
"<option value=1>Si</option><option value=0>No</option>\n"
"</select>\n"
"<div class=grid>\n"
"<div><label>Avisar por encima de (ppm)</label><input name=alarm_co2_ppm type=number min=400 max=5000></div>\n"
"<div><label>Callar por debajo de (ppm)</label><input name=alarm_clear_ppm type=number min=400 max=5000></div>\n"
"</div>\n"
"<label>Volumen <span class=val id=av></span></label>\n"
"<input name=alarm_volume type=range min=0 max=100 oninput=vivo(this)>\n"
"<div class=hint>El umbral para callar tiene que ser MENOR que el de avisar: esa diferencia evita que pite sin parar cuando el CO2 ronda el limite. Si los pones al reves, se corrige solo.</div>\n"
"\n"
"<button type=submit>Guardar y reiniciar</button></form><div id=msg></div></div>\n"
"\n"
"<h2>Mantenimiento</h2><div class=card>\n"
"<div class=row>\n"
"<button class=alt onclick=\"post('/api/fanclean')\">Limpiar ventilador</button>\n"
"<button class=alt onclick=\"post('/api/beep')\">Probar sonido</button>\n"
"<button class=alt onclick=\"post('/api/reboot')\">Reiniciar</button>\n"
"</div>\n"
"<label style='margin-top:16px'>Recalibrar el CO2 con una referencia</label>\n"
"<div class=grid><div><input id=frc type=number value=420 min=400 max=2000></div>\n"
"<div><button class=alt style='margin-top:0;width:100%' onclick=recal()>Recalibrar</button></div></div>\n"
"<div class=hint style='color:#facc15'>Solo con una referencia de verdad: saca el aparato al aire libre, lejos de personas, dejalo medir 5 minutos y usa 420 ppm. Con un valor inventado se estropea la medida en vez de arreglarla, y queda guardado en el sensor.</div>\n"
"<div id=frcmsg class=hint></div>\n"
"<label style='margin-top:16px'>Actualizar firmware (.bin)</label>\n"
"<input type=file id=fw accept='.bin'>\n"
"<button class=alt onclick=ota()>Subir e instalar</button>\n"
"<div id=otamsg class=hint></div></div>\n"
"\n"
"<h2>Copia de seguridad</h2><div class=card>\n"
"<div class=hint>Guarda los ajustes en un fichero para recuperarlos si alguna vez se borra la memoria.</div>\n"
"<div class=row style='margin-top:12px'>\n"
"<button type=button class=alt onclick=\"location='/api/backup'\">Descargar</button>\n"
"<button type=button class=alt onclick=\"location='/api/backup?secrets=1'\">Descargar con contrasenas</button>\n"
"</div>\n"
"<div class=hint style='color:#facc15'>El fichero con contrasenas lleva la de tu WiFi en claro: guardalo donde guardarias una contrasena.</div>\n"
"<label>Restaurar desde fichero</label><input type=file id=bk accept=.json>\n"
"<button type=button onclick=restore()>Restaurar</button>\n"
"<div id=bkmsg class=hint></div></div>\n"
"\n"
"<script>\n"
"const M={co2:['CO2','ppm'],pm25:['PM2.5','ug/m3'],pm10:['PM10','ug/m3'],\n"
"pm1:['PM1.0','ug/m3'],pm4:['PM4.0','ug/m3'],temperature:['Temperatura','C'],\n"
"humidity:['Humedad','%'],voc:['VOC',''],nox:['NOx',''],noise:['Ruido','dB']};\n"
"const NIV={good:'Bueno',fair:'Aceptable',moderate:'Regular',poor:'Malo',bad:'Muy malo'};\n"
"function vivo(e){\n"
" const s={brightness:'bv',night_brightness:'nv',alarm_volume:'av'}[e.name];\n"
" if(s)document.getElementById(s).textContent=e.value;\n"
" if(e.name=='brightness')fetch('/api/brightness?v='+e.value,{method:'POST'});\n"
"}\n"
"async function refresh(){const s=await(await fetch('/api/state')).json();\n"
" document.getElementById('sub').textContent=s.name+' - v'+s.version+' - '+s.id+' - '+s.ip;\n"
" let h='';for(const k in M){const v=s[k];\n"
"  h+='<tr><td>'+M[k][0]+'</td><td>'+(v==null?'--':v+' '+M[k][1])+'</td></tr>'}\n"
" h+='<tr><td>Calidad del aire</td><td>'+(NIV[s.level]||s.level||'--')+'</td></tr>';\n"
" h+='<tr><td>Sensor</td><td class='+(s.sensor=='OK'?'ok':'warn')+'>'+s.sensor+'</td></tr>';\n"
" h+='<tr><td>Home Assistant</td><td class='+(s.mqtt?'ok':'warn')+'>'+(s.mqtt?'conectado':'sin conexion')+'</td></tr>';\n"
" if(s.bat_pct!=null)h+='<tr><td>Bateria</td><td>'+s.bat_pct+' % '+(s.charging?'(cargando)':(s.usb?'(con USB)':'(en bateria)'))+'</td></tr>';\n"
" document.getElementById('st').innerHTML=h;\n"
" if(s.co2_recal)document.getElementById('frcmsg').textContent=s.co2_recal}\n"
"async function load(){const c=await(await fetch('/api/settings')).json();\n"
" for(const [k,v] of Object.entries(c)){\n"
"  if(k=='pages_mask'){for(let i=0;i<6;i++)document.getElementById('pg'+i).checked=!!(v>>i&1);continue}\n"
"  const e=document.forms.f[k];if(!e)continue;\n"
"  e.value=v;\n"
"  if(e.type=='select-one'&&e.selectedIndex<0){\n"
"   const o=document.createElement('option');o.value=v;o.textContent=v+' (actual)';\n"
"   e.appendChild(o);e.value=v}\n"
"  if(e.type=='range'){const s={brightness:'bv',night_brightness:'nv',alarm_volume:'av'}[k];\n"
"   if(s)document.getElementById(s).textContent=e.value}}}\n"
"document.getElementById('f').onsubmit=async ev=>{ev.preventDefault();\n"
" const d=Object.fromEntries(new FormData(ev.target));\n"
" let m=0;for(let i=0;i<6;i++)if(document.getElementById('pg'+i).checked)m|=1<<i;\n"
" d.pages_mask=m||1;\n"
" const r=await fetch('/api/settings',{method:'POST',body:JSON.stringify(d)});\n"
" document.getElementById('msg').textContent=r.ok?'Guardado. Reiniciando...':'Error al guardar'};\n"
"async function post(u){const r=await fetch(u,{method:'POST'});\n"
" document.getElementById('msg').textContent=r.ok?'Hecho':'Error'}\n"
"async function recal(){const p=document.getElementById('frc').value;\n"
" const m=document.getElementById('frcmsg');\n"
" if(!confirm('Recalibrar el CO2 a '+p+' ppm? Queda guardado en el sensor.'))return;\n"
" m.textContent='Pidiendo...';\n"
" const r=await fetch('/api/co2recal?ppm='+p,{method:'POST'});\n"
" m.textContent=await r.text()}\n"
"async function ota(){const f=document.getElementById('fw').files[0];if(!f)return;\n"
" const m=document.getElementById('otamsg');m.textContent='Subiendo '+f.size+' bytes...';\n"
" const r=await fetch('/api/ota',{method:'POST',body:f});\n"
" m.textContent=r.ok?'Instalado, reiniciando...':'Error: '+await r.text()}\n"
"async function restore(){const f=document.getElementById('bk').files[0];\n"
" const m=document.getElementById('bkmsg');if(!f){m.textContent='Elige un fichero';return}\n"
" let d;try{d=JSON.parse(await f.text())}catch(e){m.textContent='No es un JSON valido';return}\n"
" const r=await fetch('/api/settings',{method:'POST',body:JSON.stringify(d)});\n"
" m.textContent=r.ok?'Restaurado. Reiniciando...':'Error al restaurar'}\n"
"load();refresh();setInterval(refresh,5000);\n"
"</script></html>\n";

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

    char json[880];
    int n = snprintf(json, sizeof(json),
        "{\"name\":\"%s\",\"version\":\"%s\",\"id\":\"%s\",\"ip\":\"%s\","
        "\"rssi\":%d,\"mqtt\":%s,\"sensor\":\"%s\",\"age_s\":%u,",
        settings_get()->device_name, APP_VERSION, net_device_id(),
        net_ip()[0] ? net_ip() : "sin IP", net_rssi(),
        ha_mqtt_connected() ? "true" : "false", sensor, (unsigned)s.age_s);

    uint32_t idle_s = 0; bool dimmed = false, idle_shown = false;
    ui_idle_debug(&idle_s, &dimmed, &idle_shown);
    n += snprintf(json + n, sizeof(json) - n,
                  "\"idle_s\":%u,\"dimmed\":%s,\"idle_view\":%s,",
                  (unsigned)idle_s, dimmed ? "true" : "false",
                  idle_shown ? "true" : "false");

    // Bateria: es lo unico que se puede consultar con el USB fuera, asi que
    // es la herramienta para medir el consumo real en descarga.
    pmu_status_t b;
    if (pmu_available() && pmu_read(&b) == ESP_OK && b.present) {
        n += snprintf(json + n, sizeof(json) - n,
                      "\"bat_pct\":%d,\"bat_mv\":%u,\"charging\":%s,\"usb\":%s,",
                      b.percent, b.millivolts,
                      b.charging ? "true" : "false", b.vbus ? "true" : "false");
    }

    for (int m = 0; m < AIR_METRIC_COUNT; m++) {
        const char *key = air_metric_key((air_metric_t)m);
        if (isnan(s.v[m])) {
            n += snprintf(json + n, sizeof(json) - n, "\"%s\":null,", key);
        } else {
            n += snprintf(json + n, sizeof(json) - n, "\"%s\":%.*f,", key,
                          air_metric_decimals((air_metric_t)m), s.v[m]);
        }
    }
    n += snprintf(json + n, sizeof(json) - n, "\"level\":\"%s\"",
                  s.valid ? air_level_text(air_overall(&s)) : "");

    {
        const float lvl = mic_level_dbfs(), pk = mic_peak_dbfs();
        n += snprintf(json + n, sizeof(json) - n, ",\"mic\":\"%s\"", mic_status());
        if (!isnan(lvl)) {
            n += snprintf(json + n, sizeof(json) - n,
                          ",\"noise_dbfs\":%.1f,\"noise_peak_dbfs\":%.1f", lvl, pk);
        }
    }

    if (s_recal_status) {
        char recal[96];
        s_recal_status(recal, sizeof(recal));
        if (recal[0]) {
            n += snprintf(json + n, sizeof(json) - n, ",\"co2_recal\":\"%s\"", recal);
        }
    }
    n += snprintf(json + n, sizeof(json) - n, "}");

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json, n);
}

// ------------------------------------------------------------- configuracion
static esp_err_t h_settings_get(httpd_req_t *req)
{
    const settings_t *c = settings_get();
    char json[900];
    // Las contrasenas nunca se devuelven; el formulario las deja en blanco y
    // solo se cambian si se escribe algo.
    int n = snprintf(json, sizeof(json),
        "{\"wifi_ssid\":\"%s\",\"mqtt_uri\":\"%s\",\"mqtt_user\":\"%s\","
        "\"mqtt_prefix\":\"%s\",\"device_name\":\"%s\",\"lang\":\"%s\","
        "\"tz\":\"%s\",\"ntp\":\"%s\","
        "\"brightness\":%u,\"night_brightness\":%u,\"screen_timeout_s\":%u,"
        "\"page_dwell_s\":%u,\"chart_span_min\":%u,\"pages_mask\":%u,"
        "\"temp_offset\":%.1f,\"altitude_m\":%u,\"co2_asc\":%d,"
        "\"alarm_enabled\":%d,\"alarm_co2_ppm\":%u,\"alarm_clear_ppm\":%u,"
        "\"alarm_volume\":%u,\"noise_offset_db\":%d}",
        c->wifi_ssid, c->mqtt_uri, c->mqtt_user, c->mqtt_prefix, c->device_name, c->lang,
        c->tz, c->ntp, c->brightness, c->night_brightness, c->screen_timeout_s,
        c->page_dwell_s, c->chart_span_min, c->pages_mask,
        c->temp_offset_dc / 10.0f, c->altitude_m, c->co2_asc ? 1 : 0,
        c->alarm_enabled ? 1 : 0, c->alarm_co2_ppm, c->alarm_clear_ppm,
        c->alarm_volume, c->noise_offset_db);

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
    copy_str(root, "lang", c->lang, sizeof(c->lang), true);
    copy_str(root, "tz", c->tz, sizeof(c->tz), true);
    copy_str(root, "ntp", c->ntp, sizeof(c->ntp), true);

    double d;
    if (get_num(root, "brightness", &d))       c->brightness = (uint8_t)(d < 1 ? 1 : (d > 255 ? 255 : d));
    if (get_num(root, "night_brightness", &d)) c->night_brightness = (uint8_t)(d < 0 ? 0 : (d > 255 ? 255 : d));
    if (get_num(root, "screen_timeout_s", &d)) c->screen_timeout_s = (uint16_t)(d < 0 ? 0 : d);
    if (get_num(root, "page_dwell_s", &d))     c->page_dwell_s = (uint16_t)(d < 0 ? 0 : d);
    if (get_num(root, "chart_span_min", &d))   c->chart_span_min = (uint16_t)(d < 5 ? 5 : (d > 1440 ? 1440 : d));
    if (get_num(root, "pages_mask", &d))       c->pages_mask = (uint8_t)(d < 1 ? 1 : (d > 63 ? 63 : d));
    if (get_num(root, "temp_offset", &d))      c->temp_offset_dc = (int16_t)lrint(d * 10.0);
    if (get_num(root, "altitude_m", &d))       c->altitude_m = (uint16_t)(d < 0 ? 0 : (d > 3000 ? 3000 : d));
    if (get_num(root, "co2_asc", &d))          c->co2_asc = (d != 0);
    if (get_num(root, "alarm_enabled", &d))    c->alarm_enabled = (d != 0);
    if (get_num(root, "alarm_co2_ppm", &d))    c->alarm_co2_ppm = (uint16_t)(d < 400 ? 400 : (d > 5000 ? 5000 : d));
    if (get_num(root, "alarm_clear_ppm", &d))  c->alarm_clear_ppm = (uint16_t)(d < 400 ? 400 : (d > 5000 ? 5000 : d));
    if (get_num(root, "alarm_volume", &d))     c->alarm_volume = (uint8_t)(d < 0 ? 0 : (d > 100 ? 100 : d));
    if (get_num(root, "noise_offset_db", &d)) c->noise_offset_db = (int16_t)(d < 0 ? 0 : (d > 200 ? 200 : d));
    cJSON_Delete(root);

    // La histeresis solo existe si el umbral de callar queda POR DEBAJO del de
    // avisar. Igualados o al reves, el aviso se dispararia y se cancelaria en
    // la misma muestra: mejor corregirlo aqui que dejar el aparato pitando.
    if (c->alarm_clear_ppm >= c->alarm_co2_ppm) {
        c->alarm_clear_ppm = (c->alarm_co2_ppm > 500) ? c->alarm_co2_ppm - 200 : 400;
        ESP_LOGW(TAG, "umbral de rearme por encima del de aviso: bajado a %u ppm",
                 c->alarm_clear_ppm);
    }

    display_set_brightness(c->brightness); // esto si se nota al instante
    sound_set_volume(c->alarm_volume);     // para que el boton de prueba use el nuevo

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
// Copia de seguridad de los ajustes. Por defecto SIN contrasenas: este panel
// no tiene clave, y cualquiera en la red podria descargarse la del WiFi. Con
// ?secrets=1 se incluyen, que es lo unico que permite restaurar de un tiron
// tras un borrado total, pero eso lo pide el usuario a sabiendas.
static esp_err_t h_backup(httpd_req_t *req)
{
    bool con_secretos = false;
    char q[32];
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK) {
        char v[8];
        if (httpd_query_key_value(q, "secrets", v, sizeof(v)) == ESP_OK && v[0] == '1') {
            con_secretos = true;
        }
    }

    const settings_t *c = settings_get();
    char json[900];
    int n = snprintf(json, sizeof(json),
        "{\"wifi_ssid\":\"%s\",\"mqtt_uri\":\"%s\",\"mqtt_user\":\"%s\","
        "\"mqtt_prefix\":\"%s\",\"device_name\":\"%s\",\"lang\":\"%s\","
        "\"tz\":\"%s\",\"ntp\":\"%s\",\"brightness\":%u,"
        "\"night_brightness\":%u,\"screen_timeout_s\":%u,\"chart_span_min\":%u,"
        "\"pages_mask\":%u,\"temp_offset\":%.1f,\"altitude_m\":%u,"
        "\"co2_asc\":%d,\"alarm_enabled\":%d,\"alarm_co2_ppm\":%u,"
        "\"alarm_clear_ppm\":%u,\"alarm_volume\":%u",
        c->wifi_ssid, c->mqtt_uri, c->mqtt_user, c->mqtt_prefix, c->device_name,
        c->lang, c->tz, c->ntp, c->brightness, c->night_brightness,
        c->screen_timeout_s, c->chart_span_min, c->pages_mask,
        c->temp_offset_dc / 10.0, c->altitude_m, c->co2_asc,
        c->alarm_enabled, c->alarm_co2_ppm, c->alarm_clear_ppm, c->alarm_volume);

    if (con_secretos) {
        n += snprintf(json + n, sizeof(json) - n,
                      ",\"wifi_pass\":\"%s\",\"mqtt_pass\":\"%s\"",
                      c->wifi_pass, c->mqtt_pass);
    }
    n += snprintf(json + n, sizeof(json) - n, "}");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Content-Disposition",
                       "attachment; filename=\"monitor-sen66.json\"");
    httpd_resp_send(req, json, n);
    return ESP_OK;
}

// Prueba del altavoz: sin esto no hay forma de saber si el audio funciona
// sin esperar a que el CO2 pase del umbral.
static esp_err_t h_beep(httpd_req_t *req)
{
    if (!sound_available()) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "sin audio");
        return ESP_FAIL;
    }
    sound_play(SOUND_TEST);
    httpd_resp_sendstr(req, "ok");
    return ESP_OK;
}

static esp_err_t h_fanclean(httpd_req_t *req)
{
    if (sen66_fan_clean() != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "el sensor no responde");
        return ESP_FAIL;
    }
    return httpd_resp_sendstr(req, "limpiando (10 s)");
}

// Recalibracion forzada de CO2. Aqui solo se valida y se encola: el comando
// exige la medicion parada, y pararla desde este hilo chocaria con la lectura
// que hace la tarea del sensor cada segundo.
static esp_err_t h_co2recal(httpd_req_t *req)
{
    char q[48], v[8];
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) != ESP_OK ||
        httpd_query_key_value(q, "ppm", v, sizeof(v)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "falta ppm");
        return ESP_FAIL;
    }
    const int ppm = atoi(v);
    if (ppm < 400 || ppm > 2000) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                            "la referencia tiene que estar entre 400 y 2000 ppm");
        return ESP_FAIL;
    }
    if (!s_recal_request || !s_recal_request((uint16_t)ppm)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "ahora no se puede: el sensor no esta midiendo o ya hay una en curso");
        return ESP_FAIL;
    }
    return httpd_resp_sendstr(req, "recalibrando, tarda unos segundos...");
}

// Diagnostico: parar o arrancar la medicion del sensor. Parado el ventilador
// se detiene, que es lo que permite medir cuanto ruido mete. No se mide nada
// mientras tanto.
static esp_err_t h_fan(httpd_req_t *req)
{
    char q[32], v[8];
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) != ESP_OK ||
        httpd_query_key_value(q, "on", v, sizeof(v)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "falta on=0|1");
        return ESP_FAIL;
    }
    if (!s_fan || !s_fan(v[0] == '1')) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "el sensor no esta disponible");
        return ESP_FAIL;
    }
    return httpd_resp_sendstr(req, v[0] == '1' ? "midiendo" : "parado (no se mide nada)");
}

// Brillo en vivo mientras se arrastra el deslizador. NO guarda: lo guardado
// sigue siendo lo que haya en NVS hasta que se pulse Guardar. Sin esto el
// deslizador es a ciegas y hay que reiniciar para ver el resultado.
static esp_err_t h_brightness(httpd_req_t *req)
{
    char q[24], v[8];
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) != ESP_OK ||
        httpd_query_key_value(q, "v", v, sizeof(v)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "falta v");
        return ESP_FAIL;
    }
    int b = atoi(v);
    if (b < 1) b = 1;
    if (b > 255) b = 255;
    display_set_brightness((uint8_t)b);
    return httpd_resp_sendstr(req, "ok");
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
    cfg.max_uri_handlers = 16;
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
        {.uri = "/api/beep",      .method = HTTP_POST, .handler = h_beep},
        {.uri = "/api/co2recal",  .method = HTTP_POST, .handler = h_co2recal},
        {.uri = "/api/fan",       .method = HTTP_POST, .handler = h_fan},
        {.uri = "/api/brightness",.method = HTTP_POST, .handler = h_brightness},
        {.uri = "/api/backup",    .method = HTTP_GET,  .handler = h_backup},
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
