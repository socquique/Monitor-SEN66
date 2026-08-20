#include "pmu_axp2101.h"

#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "axp2101";

#define AXP_ADDR              0x34
#define REG_STATUS1           0x00  // bit3: bateria conectada
#define REG_STATUS2           0x01  // bits6:5 == 01 cargando; bit3 vbus
#define REG_IC_TYPE           0x03
#define REG_GAUGE_WDT_CTRL    0x18  // bit3: medidor de carga
#define REG_ADC_CHANNEL_CTRL  0x30  // bit0: medir tension de bateria
#define REG_ADC_DATA0         0x34  // tension: H5 en 0x34, L8 en 0x35
#define REG_BAT_DET_CTRL      0x68  // bit0: deteccion de bateria
#define REG_BAT_PERCENT       0xA4

#define CACHE_US (2 * 1000 * 1000)

static i2c_master_dev_handle_t s_dev;
static pmu_status_t s_cache;
static int64_t s_cache_us;

static esp_err_t rd(uint8_t reg, uint8_t *val)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, val, 1, 100);
}

static esp_err_t wr(uint8_t reg, uint8_t val)
{
    const uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(s_dev, buf, 2, 100);
}

static esp_err_t set_bit(uint8_t reg, int bit)
{
    uint8_t v;
    ESP_RETURN_ON_ERROR(rd(reg, &v), TAG, "rd %02x", reg);
    return wr(reg, (uint8_t)(v | (1u << bit)));
}

esp_err_t pmu_init(i2c_master_bus_handle_t bus)
{
    const i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = AXP_ADDR,
        .scl_speed_hz = 400000,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &cfg, &s_dev), TAG, "add dev");

    uint8_t id = 0;
    if (rd(REG_IC_TYPE, &id) != ESP_OK) {
        ESP_LOGW(TAG, "no responde en 0x%02x", AXP_ADDR);
        s_dev = NULL;
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGI(TAG, "PMU presente (IC type 0x%02x)", id);

    // Solo encendemos lo necesario para medir. NO tocamos raíles: BLDO1
    // alimenta el AMOLED y apagarlo deja la pantalla negra sin error.
    set_bit(REG_BAT_DET_CTRL, 0);      // deteccion de bateria
    set_bit(REG_ADC_CHANNEL_CTRL, 0);  // ADC de tension de bateria
    set_bit(REG_GAUGE_WDT_CTRL, 3);    // medidor de carga (el % de 0xA4)
    s_cache_us = 0;
    return ESP_OK;
}

bool pmu_available(void) { return s_dev != NULL; }

esp_err_t pmu_read(pmu_status_t *out)
{
    if (!s_dev) return ESP_ERR_INVALID_STATE;

    const int64_t now = esp_timer_get_time();
    if (s_cache_us && now - s_cache_us < CACHE_US) {
        *out = s_cache;
        return ESP_OK;
    }

    uint8_t st1 = 0, st2 = 0, pct = 0, vh = 0, vl = 0;
    ESP_RETURN_ON_ERROR(rd(REG_STATUS1, &st1), TAG, "status1");
    ESP_RETURN_ON_ERROR(rd(REG_STATUS2, &st2), TAG, "status2");

    pmu_status_t s = {0};
    s.present = (st1 >> 3) & 1;
    s.charging = ((st2 >> 5) & 0x03) == 0x01;
    s.vbus = !((st2 >> 3) & 1);
    s.percent = -1;

    if (s.present) {
        if (rd(REG_BAT_PERCENT, &pct) == ESP_OK && pct <= 100) s.percent = (int8_t)pct;
        if (rd(REG_ADC_DATA0, &vh) == ESP_OK && rd(REG_ADC_DATA0 + 1, &vl) == ESP_OK) {
            s.millivolts = (uint16_t)(((vh & 0x1F) << 8) | vl);
        }
    }

    s_cache = s;
    s_cache_us = now;
    *out = s;
    return ESP_OK;
}
