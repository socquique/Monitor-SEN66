// Mini-driver del PMU AXP2101 (I2C 0x34) para leer el estado de la bateria.
//
// Solo lectura: la placa arranca con los raíles ya configurados por el
// firmware de fabrica y tocarlos desde aqui es la via rapida a una pantalla
// negra (BLDO1 alimenta el AMOLED).
//
// Registros verificados contra XPowersLib, que es lo que usa el ejemplo
// oficial de Waveshare para esta placa.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

typedef struct {
    bool present;       // hay bateria conectada
    bool charging;      // esta cargando ahora mismo
    bool vbus;          // hay alimentacion externa por USB
    int8_t percent;     // 0..100, -1 si no se sabe
    uint16_t millivolts;// tension de la celda
} pmu_status_t;

esp_err_t pmu_init(i2c_master_bus_handle_t bus);

// Lecturas cacheadas ~2 s: consultarlo por I2C en cada frame provoca
// parpadeos de brillo (lección de CapsuleRadar).
esp_err_t pmu_read(pmu_status_t *out);
bool pmu_available(void);
