#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "lvgl.h"

// Levanta el AMOLED CO5300 (QSPI), el tactil CST9217 y el port de LVGL.
esp_err_t display_init(void);

lv_display_t *display_get(void);

// Bus I2C de a bordo (compartido con RTC/IMU/PMU). El SEN66 NO va aqui.
i2c_master_bus_handle_t display_i2c_bus(void);

// Brillo 0..255 por comando de panel 0x51 (esta placa no tiene PWM de
// retroiluminacion). 0 apaga del todo.
void display_set_brightness(uint8_t level);
