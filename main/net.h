// WiFi (estacion + portal de configuracion) y hora por SNTP.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef enum {
    NET_IDLE = 0,
    NET_CONNECTING,
    NET_CONNECTED,
    NET_PORTAL,   // punto de acceso propio, esperando que configuren la red
} net_state_t;

// Levanta netif, el bucle de eventos y el driver WiFi. Debe llamarse una vez
// y ANTES de que haga falta memoria interna contigua para TLS.
esp_err_t net_init(void);

// Arranca la conexion segun los ajustes: si hay SSID, modo estacion; si no,
// abre el punto de acceso de configuracion.
esp_err_t net_start(void);

net_state_t net_state(void);
const char *net_ip(void);       // "192.168.1.42" o "" si no hay
int net_rssi(void);             // dBm, 0 si no aplica
const char *net_device_id(void);// "sen66-a1b2c3", estable por MAC
const char *net_ap_ssid(void);  // SSID del portal

// Callback opcional cuando SNTP sincroniza (para poner en hora el RTC).
void net_set_time_sync_cb(void (*cb)(void));
