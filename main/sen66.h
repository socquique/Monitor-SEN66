// Driver del Sensirion SEN66 sobre I2C (direccion 0x6B, 100 kHz maximo).
//
// El SEN66 comparte direccion con el IMU QMI8658 de la placa, asi que este
// driver crea y gestiona su PROPIO bus I2C (puerto 1) sobre los pines del
// header de expansion. Ver board.h.
//
// Protocolo verificado contra el driver oficial de Sensirion
// (github.com/Sensirion/raspberry-pi-i2c-sen66, generado del modelo 1.7.3).
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "air.h"

// Bits de sen66_read_status()
#define SEN66_ST_FAN_ERROR    (1u << 4)
#define SEN66_ST_RHT_ERROR    (1u << 6)
#define SEN66_ST_GAS_ERROR    (1u << 7)
#define SEN66_ST_CO2_2_ERROR  (1u << 9)
#define SEN66_ST_HCHO_ERROR   (1u << 10)
#define SEN66_ST_PM_ERROR     (1u << 11)
#define SEN66_ST_CO2_1_ERROR  (1u << 12)
#define SEN66_ST_FAN_WARNING  (1u << 21)

// Levanta el bus y localiza el sensor. Si no responde en los pines de
// board.h y `autodetect` es true, barre los pares candidatos y se queda con
// el que conteste (avisando por consola de cual es, para fijarlo en board.h).
esp_err_t sen66_init(bool autodetect);
void sen66_pins(int *sda, int *scl);
bool sen66_present(void);

// Configuracion. Todas exigen el sensor parado (llamarlas antes de start).
esp_err_t sen66_set_temp_offset(float celsius);
esp_err_t sen66_set_altitude(uint16_t meters);
esp_err_t sen66_set_co2_asc(bool enabled);

esp_err_t sen66_start(void);
esp_err_t sen66_stop(void);

// ESP_OK si habia dato nuevo, ESP_ERR_NOT_FINISHED si el sensor aun no lo
// tiene listo (llega uno por segundo).
esp_err_t sen66_read(air_sample_t *out);

esp_err_t sen66_serial(char *buf, size_t len);
esp_err_t sen66_product_name(char *buf, size_t len);
esp_err_t sen66_version(uint8_t *major, uint8_t *minor);
esp_err_t sen66_read_status(uint32_t *status);
esp_err_t sen66_reset(void);

// Arranca la limpieza del ventilador (10 s a maxima velocidad). Recomendado
// una vez por semana; el sensor debe estar midiendo.
esp_err_t sen66_fan_clean(void);

// Recalibracion forzada del CO2 a un valor de referencia conocido (p.ej.
// 420 ppm al aire libre). Exige el sensor parado. `correction` devuelve la
// correccion aplicada; 0xFFFF significa que fallo.
esp_err_t sen66_forced_co2_recal(uint16_t target_ppm, uint16_t *correction);

// Texto ASCII con los errores activos, "OK" si no hay ninguno.
void sen66_status_text(uint32_t status, char *buf, size_t len);
