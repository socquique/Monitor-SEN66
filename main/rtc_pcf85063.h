// Mini-driver del RTC PCF85063 (I2C 0x51). Solo firmware.
//
// Convención: el RTC guarda HORA LOCAL (no UTC). Más simple para un gadget
// sin red; el cambio de horario verano/invierno requiere re-poner en hora
// (o llegará solo cuando integremos WiFi/NTP).
#pragma once

#include <time.h>
#include "driver/i2c_master.h"
#include "esp_err.h"

esp_err_t rtc_pcf85063_init(i2c_master_bus_handle_t bus);

// Lee la hora. Devuelve ESP_ERR_INVALID_STATE si el RTC perdió la hora
// (bit OS: oscilador parado en algún momento, p.ej. sin batería).
esp_err_t rtc_pcf85063_get(struct tm *out);

// Escribe la hora (y limpia el bit OS).
esp_err_t rtc_pcf85063_set(const struct tm *t);
