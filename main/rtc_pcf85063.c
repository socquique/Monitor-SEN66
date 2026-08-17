#include "rtc_pcf85063.h"

#include "esp_check.h"

static const char *TAG = "pcf85063";

#define PCF85063_ADDR       0x51
#define PCF85063_REG_TIME   0x04 // segundos, minutos, horas, día, día-semana, mes, año
#define PCF85063_OS_FLAG    0x80 // bit 7 de segundos: oscilador parado

static i2c_master_dev_handle_t s_dev;

static uint8_t bcd2dec(uint8_t v) { return (v >> 4) * 10 + (v & 0x0f); }
static uint8_t dec2bcd(uint8_t v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

esp_err_t rtc_pcf85063_init(i2c_master_bus_handle_t bus)
{
    const i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = PCF85063_ADDR,
        .scl_speed_hz = 100000,
    };
    return i2c_master_bus_add_device(bus, &cfg, &s_dev);
}

esp_err_t rtc_pcf85063_get(struct tm *out)
{
    ESP_RETURN_ON_FALSE(s_dev, ESP_ERR_INVALID_STATE, TAG, "sin init");

    const uint8_t reg = PCF85063_REG_TIME;
    uint8_t b[7];
    ESP_RETURN_ON_ERROR(i2c_master_transmit_receive(s_dev, &reg, 1, b, sizeof(b), 100),
                        TAG, "lectura");

    *out = (struct tm){
        .tm_sec = bcd2dec(b[0] & 0x7f),
        .tm_min = bcd2dec(b[1] & 0x7f),
        .tm_hour = bcd2dec(b[2] & 0x3f),
        .tm_mday = bcd2dec(b[3] & 0x3f),
        .tm_wday = b[4] & 0x07,
        .tm_mon = bcd2dec(b[5] & 0x1f) - 1,
        .tm_year = 100 + bcd2dec(b[6]), // el RTC cuenta desde 2000
        .tm_isdst = -1,
    };

    if (b[0] & PCF85063_OS_FLAG) return ESP_ERR_INVALID_STATE;
    return ESP_OK;
}

esp_err_t rtc_pcf85063_set(const struct tm *t)
{
    ESP_RETURN_ON_FALSE(s_dev, ESP_ERR_INVALID_STATE, TAG, "sin init");

    const uint8_t buf[8] = {
        PCF85063_REG_TIME,
        dec2bcd((uint8_t)t->tm_sec), // bit7 = 0: limpia el flag OS
        dec2bcd((uint8_t)t->tm_min),
        dec2bcd((uint8_t)t->tm_hour),
        dec2bcd((uint8_t)t->tm_mday),
        (uint8_t)(t->tm_wday & 0x07),
        dec2bcd((uint8_t)(t->tm_mon + 1)),
        dec2bcd((uint8_t)(t->tm_year - 100)),
    };
    return i2c_master_transmit(s_dev, buf, sizeof(buf), 100);
}
