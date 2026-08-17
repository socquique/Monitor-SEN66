#include "history.h"

#include <math.h>
#include <string.h>

#define HIST_EMPTY INT16_MIN

// Factor de escala por metrica al guardar en int16. El CO2 va sin escalar
// porque x10 se saldria del rango (el SEN66 llega a 40000 ppm).
static int16_t hist_scale(air_metric_t m)
{
    return (m == AIR_CO2) ? 1 : 10;
}

static int16_t to_raw(air_metric_t m, float v)
{
    if (isnan(v)) return HIST_EMPTY;
    float s = v * hist_scale(m);
    if (s > 32767.0f) s = 32767.0f;
    if (s < -32767.0f) s = -32767.0f;
    return (int16_t)lrintf(s);
}

static float from_raw(air_metric_t m, int16_t raw)
{
    if (raw == HIST_EMPTY) return NAN;
    return (float)raw / hist_scale(m);
}

void air_history_init(air_history_t *h, uint32_t slot_seconds)
{
    memset(h, 0, sizeof(*h));
    for (int m = 0; m < AIR_METRIC_COUNT; m++) {
        for (int i = 0; i < AIR_HIST_SLOTS; i++) h->v[m][i] = HIST_EMPTY;
    }
    h->slot_seconds = slot_seconds ? slot_seconds : 60;
    h->cur_slot = -1;
}

// Vuelca la media acumulada en la ranura en curso y limpia el acumulador.
static void commit(air_history_t *h)
{
    for (int m = 0; m < AIR_METRIC_COUNT; m++) {
        h->v[m][h->head] = h->nacc[m] ? to_raw(m, h->acc[m] / h->nacc[m]) : HIST_EMPTY;
        h->acc[m] = 0;
        h->nacc[m] = 0;
    }
    if (h->filled < AIR_HIST_SLOTS) h->filled++;
}

void air_history_push(air_history_t *h, const air_sample_t *s, time_t now)
{
    if (!s->valid) return;
    const int64_t slot = (int64_t)now / (int64_t)h->slot_seconds;

    if (h->cur_slot < 0) {
        h->cur_slot = slot;
        h->head = 0;
    } else if (slot < h->cur_slot) {
        // El reloj ha ido hacia atras (sincronizacion NTP tras arrancar con
        // el RTC desfasado). Reiniciar es mas honesto que mezclar epocas.
        air_history_init(h, h->slot_seconds);
        h->cur_slot = slot;
        h->head = 0;
    } else if (slot > h->cur_slot) {
        int64_t gap = slot - h->cur_slot;
        if (gap > AIR_HIST_SLOTS) gap = AIR_HIST_SLOTS; // hueco mayor que el anillo
        for (int64_t i = 0; i < gap; i++) {
            commit(h); // la primera cierra la ranura real, el resto quedan vacias
            h->head = (uint16_t)((h->head + 1) % AIR_HIST_SLOTS);
        }
        h->cur_slot = slot;
    }

    for (int m = 0; m < AIR_METRIC_COUNT; m++) {
        if (isnan(s->v[m])) continue;
        h->acc[m] += s->v[m];
        h->nacc[m]++;
    }
}

// Valor consolidado de la ranura absoluta `slot`, o NAN si cae fuera del
// anillo o no tiene dato. La ranura en curso se devuelve con la media
// parcial acumulada, para que la grafica no acabe siempre en un hueco.
static float slot_value(const air_history_t *h, air_metric_t m, int64_t slot)
{
    if (h->cur_slot < 0) return NAN;
    const int64_t back = h->cur_slot - slot;
    if (back < 0 || back >= AIR_HIST_SLOTS) return NAN;
    if (back == 0) return h->nacc[m] ? h->acc[m] / h->nacc[m] : NAN;
    int idx = (int)(((int)h->head - (int)back) % AIR_HIST_SLOTS);
    if (idx < 0) idx += AIR_HIST_SLOTS;
    return from_raw(m, h->v[m][idx]);
}

int air_history_series(const air_history_t *h, air_metric_t m,
                       uint32_t span_seconds, float *out, int out_len)
{
    for (int i = 0; i < out_len; i++) out[i] = NAN;
    if (out_len <= 0 || h->cur_slot < 0) return 0;

    int span_slots = (int)(span_seconds / h->slot_seconds);
    if (span_slots < out_len) span_slots = out_len;
    if (span_slots > AIR_HIST_SLOTS) span_slots = AIR_HIST_SLOTS;

    // Cada punto de salida promedia las ranuras que le tocan.
    int found = 0;
    for (int i = 0; i < out_len; i++) {
        const int lo = (int)((int64_t)span_slots * i / out_len);
        const int hi = (int)((int64_t)span_slots * (i + 1) / out_len);
        float sum = 0;
        int n = 0;
        for (int k = lo; k < hi; k++) {
            // i = 0 es el extremo mas antiguo de la ventana
            const int64_t slot = h->cur_slot - (span_slots - 1 - k);
            float v = slot_value(h, m, slot);
            if (!isnan(v)) { sum += v; n++; }
        }
        if (n) { out[i] = sum / n; found++; }
    }
    return found;
}

bool air_history_range(const air_history_t *h, air_metric_t m,
                       uint32_t span_seconds, float *out_min, float *out_max)
{
    if (h->cur_slot < 0) return false;
    int span_slots = (int)(span_seconds / h->slot_seconds);
    if (span_slots > AIR_HIST_SLOTS) span_slots = AIR_HIST_SLOTS;
    if (span_slots < 1) span_slots = 1;

    float lo = INFINITY, hi = -INFINITY;
    for (int k = 0; k < span_slots; k++) {
        float v = slot_value(h, m, h->cur_slot - k);
        if (isnan(v)) continue;
        if (v < lo) lo = v;
        if (v > hi) hi = v;
    }
    if (!isfinite(lo)) return false;
    *out_min = lo;
    *out_max = hi;
    return true;
}
