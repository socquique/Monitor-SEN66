// Historico circular de las metricas, en RAM. Logica pura (sin ESP-IDF ni
// LVGL): la usan el firmware y el simulador.
//
// Una ranura por minuto y 1440 ranuras = 24 h exactas. Cada ranura guarda la
// MEDIA de las muestras de ese minuto, en int16 escalado (~26 KB en total,
// que en el firmware van a PSRAM).
//
// Las ranuras se indexan por tiempo absoluto (epoch / segundos_por_ranura),
// asi que un corte de reloj o un salto tras sincronizar con NTP no descoloca
// la grafica: los huecos salen como huecos.
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "air.h"

#define AIR_HIST_SLOTS 1440

typedef struct {
    int16_t v[AIR_METRIC_COUNT][AIR_HIST_SLOTS];
    float acc[AIR_METRIC_COUNT];
    uint16_t nacc[AIR_METRIC_COUNT];
    uint32_t slot_seconds;
    int64_t cur_slot;   // ranura absoluta en curso; -1 = aun sin datos
    uint16_t head;      // posicion de cur_slot dentro del anillo
    uint32_t filled;    // ranuras escritas desde el arranque (tope SLOTS)
} air_history_t;

void air_history_init(air_history_t *h, uint32_t slot_seconds);

// Acumula una muestra. Cierra y promedia las ranuras que hayan vencido.
void air_history_push(air_history_t *h, const air_sample_t *s, time_t now);

// Rellena `out` con `out_len` puntos que cubren los ultimos `span_seconds`,
// del mas antiguo al mas reciente. Los tramos sin datos salen como NAN.
// Devuelve cuantos puntos tienen dato.
int air_history_series(const air_history_t *h, air_metric_t m,
                       uint32_t span_seconds, float *out, int out_len);

// Minimo y maximo con dato en la ventana. Devuelve false si esta vacia.
bool air_history_range(const air_history_t *h, air_metric_t m,
                       uint32_t span_seconds, float *out_min, float *out_max);
