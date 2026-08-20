// Interfaz LVGL para la AMOLED redonda de 466x466.
//
// Este modulo NO incluye nada de ESP-IDF: solo LVGL y la logica pura de
// air.h / history.h. Asi el simulador de escritorio compila la misma UI.
// El control de brillo entra como callback porque en la placa es un comando
// del panel y en el PC no existe.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "air.h"
#include "history.h"

#define UI_PAGE_COUNT 5

typedef struct {
    uint16_t page_dwell_s;     // 0 = sin rotacion automatica
    uint8_t pages_mask;        // bit i = pagina i visible
    uint16_t chart_span_min;   // ventana de las graficas
    uint8_t brightness;        // 1..255
    uint8_t night_brightness;
    uint16_t screen_timeout_s; // 0 = nunca atenuar
} ui_config_t;

// `hist` debe seguir vivo mientras exista la UI (la lee al redibujar).
void ui_init(air_history_t *hist, const ui_config_t *cfg,
             void (*set_brightness)(uint8_t));

// Refresca los valores. Llamar con el mutex de LVGL cogido.
void ui_update(const air_sample_t *s);

// Linea de estado: hora "HH:MM", indicadores de red y un mensaje opcional
// (p.ej. "calentando", "sin sensor" o el SSID del portal).
// `bat_pct` a -1 cuando no hay bateria conectada: entonces no se dibuja nada,
// que un aparato enchufado no necesita indicador.
void ui_set_status(const char *time_str, bool wifi, bool mqtt, const char *msg,
                   int bat_pct, bool charging);

// Llamar una vez por segundo: rotacion de pagina y atenuado por inactividad.
void ui_tick_1s(void);

// Fuerza el brillo normal y reinicia los temporizadores (lo llama el propio
// tactil, pero tambien sirve al despertar por otra via).
// Con bateria la pantalla se apaga del todo (en AMOLED apagar es apagar) y
// mucho antes de lo configurado. Enchufado manda lo que diga el usuario.
void ui_set_on_battery(bool on_battery);

void ui_wake(void);
