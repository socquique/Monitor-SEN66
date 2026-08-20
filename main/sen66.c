#include "sen66.h"
#include "board.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_rom_sys.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "sen66";

#define SEN66_ADDR 0x6B

// Comandos (ver cabecera para la fuente)
#define CMD_START_MEASUREMENT   0x0021
#define CMD_STOP_MEASUREMENT    0x0104
#define CMD_GET_DATA_READY      0x0202
#define CMD_READ_VALUES         0x0300
#define CMD_FAN_CLEAN           0x5607
#define CMD_TEMP_OFFSET         0x60B2
#define CMD_FORCED_CO2_RECAL    0x6707
#define CMD_CO2_ASC             0x6711
#define CMD_SENSOR_ALTITUDE     0x6736
#define CMD_PRODUCT_NAME        0xD014
#define CMD_SERIAL_NUMBER       0xD033
#define CMD_GET_VERSION         0xD100
#define CMD_READ_STATUS         0xD206
#define CMD_DEVICE_RESET        0xD304

// Valores "desconocido" que devuelve el sensor mientras calienta
#define UNKNOWN_U16 0xFFFF
#define UNKNOWN_I16 0x7FFF

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_dev;
static int s_sda = -1, s_scl = -1;
static bool s_present;

// ------------------------------------------------------------------ CRC-8
// Polinomio 0x31, inicio 0xFF, sin reflejar (Sensirion)
static uint8_t crc8(const uint8_t *d, size_t n)
{
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < n; i++) {
        crc ^= d[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

// --------------------------------------------------------------- transporte
// Escribe un comando de 16 bits seguido de `nargs` palabras, cada una con su
// CRC, y espera `delay_ms` (el SEN66 NO usa clock stretching: responde NACK
// mientras esta ocupado, de ahi la espera explicita de la hoja de datos).
static esp_err_t cmd_write(uint16_t cmd, const uint16_t *args, int nargs, int delay_ms)
{
    ESP_RETURN_ON_FALSE(s_dev, ESP_ERR_INVALID_STATE, TAG, "sin init");

    uint8_t buf[2 + 8 * 3];
    ESP_RETURN_ON_FALSE(nargs <= 8, ESP_ERR_INVALID_ARG, TAG, "demasiados args");

    int n = 0;
    buf[n++] = (uint8_t)(cmd >> 8);
    buf[n++] = (uint8_t)(cmd & 0xFF);
    for (int i = 0; i < nargs; i++) {
        buf[n]     = (uint8_t)(args[i] >> 8);
        buf[n + 1] = (uint8_t)(args[i] & 0xFF);
        buf[n + 2] = crc8(&buf[n], 2);
        n += 3;
    }

    esp_err_t err = i2c_master_transmit(s_dev, buf, n, 100);
    if (err != ESP_OK) return err;
    if (delay_ms > 0) vTaskDelay(pdMS_TO_TICKS(delay_ms));
    return ESP_OK;
}

// Lee `nwords` palabras (3 bytes cada una en el bus: dato + dato + CRC).
static esp_err_t cmd_read(uint16_t *out, int nwords)
{
    ESP_RETURN_ON_FALSE(s_dev, ESP_ERR_INVALID_STATE, TAG, "sin init");
    ESP_RETURN_ON_FALSE(nwords <= 16, ESP_ERR_INVALID_ARG, TAG, "lectura larga");

    uint8_t buf[16 * 3];
    ESP_RETURN_ON_ERROR(i2c_master_receive(s_dev, buf, nwords * 3, 200), TAG, "rx");

    for (int i = 0; i < nwords; i++) {
        const uint8_t *w = &buf[i * 3];
        if (crc8(w, 2) != w[2]) {
            ESP_LOGW(TAG, "CRC malo en la palabra %d", i);
            return ESP_ERR_INVALID_CRC;
        }
        out[i] = (uint16_t)((w[0] << 8) | w[1]);
    }
    return ESP_OK;
}

static esp_err_t cmd_query(uint16_t cmd, int delay_ms, uint16_t *out, int nwords)
{
    ESP_RETURN_ON_ERROR(cmd_write(cmd, NULL, 0, delay_ms), TAG, "cmd %04x", cmd);
    return cmd_read(out, nwords);
}

// Convierte palabras a texto ASCII terminado en cero (nombre / numero de serie)
static void words_to_ascii(const uint16_t *w, int nwords, char *buf, size_t len)
{
    // El sensor ya devuelve la cadena terminada en cero y rellena el resto
    // con ceros, asi que basta con copiar y garantizar el terminador.
    size_t n = 0;
    for (int i = 0; i < nwords && n + 2 < len; i++) {
        buf[n++] = (char)(w[i] >> 8);
        buf[n++] = (char)(w[i] & 0xFF);
    }
    buf[n] = '\0';
}

// -------------------------------------------------------------------- init
// Si el ESP32 se reinicia a mitad de una transaccion (reflasheo, watchdog,
// boton de reset), el SEN66 se queda enviando su byte: sujeta SDA a masa
// esperando unos pulsos de reloj que ya no llegan, y el bus queda muerto.
// Se libera dandole a mano los pulsos que le faltan y cerrando con un STOP.
// Hay que hacerlo ANTES de instalar el driver, que se queda con los pines.
static void bus_recover(int sda, int scl)
{
    const gpio_config_t io = {
        .pin_bit_mask = (1ULL << sda) | (1ULL << scl),
        .mode = GPIO_MODE_INPUT_OUTPUT_OD, // drenaje abierto: nadie fuerza el alto
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    if (gpio_config(&io) != ESP_OK) return;

    gpio_set_level(sda, 1);
    gpio_set_level(scl, 1);
    esp_rom_delay_us(10);

    int pulses = 0;
    while (gpio_get_level(sda) == 0 && pulses < 9) { // 9 = byte + ACK
        gpio_set_level(scl, 0);
        esp_rom_delay_us(5);
        gpio_set_level(scl, 1);
        esp_rom_delay_us(5);
        pulses++;
    }
    // STOP: SDA sube mientras SCL esta alto
    gpio_set_level(sda, 0);
    esp_rom_delay_us(5);
    gpio_set_level(scl, 1);
    esp_rom_delay_us(5);
    gpio_set_level(sda, 1);
    esp_rom_delay_us(5);

    if (pulses) ESP_LOGW(TAG, "bus I2C bloqueado, liberado con %d pulsos", pulses);
    gpio_reset_pin(sda);
    gpio_reset_pin(scl);
}

static esp_err_t bus_open(int sda, int scl)
{
    const i2c_master_bus_config_t bus_cfg = {
        .i2c_port = BOARD_SEN66_I2C_PORT,
        .sda_io_num = sda,
        .scl_io_num = scl,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &s_bus), TAG, "bus %d/%d", sda, scl);

    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = SEN66_ADDR,
        .scl_speed_hz = 100000, // maximo del SEN66
    };
    esp_err_t err = i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev);
    if (err != ESP_OK) {
        i2c_del_master_bus(s_bus);
        s_bus = NULL;
    }
    return err;
}

static void bus_close(void)
{
    if (s_dev) { i2c_master_bus_rm_device(s_dev); s_dev = NULL; }
    if (s_bus) { i2c_del_master_bus(s_bus); s_bus = NULL; }
}

// Comprueba que quien contesta en 0x6B es de verdad un SEN6x y no otra cosa.
static bool looks_like_sen66(void)
{
    char name[32] = {0};
    if (sen66_product_name(name, sizeof(name)) != ESP_OK) return false;
    ESP_LOGI(TAG, "producto: '%s'", name);
    return strncmp(name, "SEN6", 4) == 0;
}

esp_err_t sen66_init(bool autodetect)
{
    static const int candidates[][2] = BOARD_SEN66_CANDIDATES;
    const int n = autodetect ? (int)(sizeof(candidates) / sizeof(candidates[0])) : 1;

    for (int i = 0; i < n; i++) {
        const int sda = (i == 0) ? BOARD_SEN66_PIN_SDA : candidates[i][0];
        const int scl = (i == 0) ? BOARD_SEN66_PIN_SCL : candidates[i][1];
        if (i > 0 && sda == BOARD_SEN66_PIN_SDA && scl == BOARD_SEN66_PIN_SCL) continue;

        bus_recover(sda, scl);
        if (bus_open(sda, scl) != ESP_OK) continue;

        // Reintentos: tras un reinicio el sensor puede tardar en volver a
        // atender, y una sola pasada lo daba por ausente (visto en la placa).
        bool found = false;
        for (int attempt = 0; attempt < 3 && !found; attempt++) {
            if (attempt) vTaskDelay(pdMS_TO_TICKS(60));
            found = (i2c_master_probe(s_bus, SEN66_ADDR, 100) == ESP_OK) && looks_like_sen66();
        }

        if (found) {
            s_sda = sda;
            s_scl = scl;
            s_present = true;
            if (i > 0) {
                ESP_LOGW(TAG, "encontrado en SDA=%d SCL=%d (no en los de board.h);"
                              " fija esos valores en BOARD_SEN66_PIN_*", sda, scl);
            } else {
                ESP_LOGI(TAG, "listo en SDA=%d SCL=%d", sda, scl);
            }
            return ESP_OK;
        }
        bus_close();
        if (i == 0 && autodetect) ESP_LOGW(TAG, "no responde en SDA=%d SCL=%d, barriendo header", sda, scl);
    }

    s_present = false;
    ESP_LOGE(TAG, "sensor no encontrado (revisa cableado I2C, 3V3 y GND)");
    return ESP_ERR_NOT_FOUND;
}

void sen66_pins(int *sda, int *scl) { *sda = s_sda; *scl = s_scl; }
bool sen66_present(void) { return s_present; }

// ------------------------------------------------------------ configuracion
esp_err_t sen66_set_temp_offset(float celsius)
{
    // offset en 1/200 C, pendiente en 1/10000, constante de tiempo en s,
    // ranura 0..4. Solo tocamos el offset fijo de la ranura 0.
    const uint16_t args[4] = {
        (uint16_t)(int16_t)lrintf(celsius * 200.0f), 0, 0, 0,
    };
    return cmd_write(CMD_TEMP_OFFSET, args, 4, 20);
}

esp_err_t sen66_set_altitude(uint16_t meters)
{
    return cmd_write(CMD_SENSOR_ALTITUDE, &meters, 1, 20);
}

esp_err_t sen66_set_co2_asc(bool enabled)
{
    const uint16_t v = enabled ? 1 : 0;
    return cmd_write(CMD_CO2_ASC, &v, 1, 20);
}

// -------------------------------------------------------------- medicion
esp_err_t sen66_start(void)
{
    return cmd_write(CMD_START_MEASUREMENT, NULL, 0, 50);
}

esp_err_t sen66_stop(void)
{
    return cmd_write(CMD_STOP_MEASUREMENT, NULL, 0, 1400);
}

static float scaled_u16(uint16_t raw, float div)
{
    return (raw == UNKNOWN_U16) ? NAN : (float)raw / div;
}

static float scaled_i16(uint16_t raw, float div)
{
    const int16_t v = (int16_t)raw;
    return (v == UNKNOWN_I16) ? NAN : (float)v / div;
}

esp_err_t sen66_read(air_sample_t *out)
{
    uint16_t ready[1];
    ESP_RETURN_ON_ERROR(cmd_query(CMD_GET_DATA_READY, 20, ready, 1), TAG, "data ready");
    // palabra alta = relleno, palabra baja = bandera
    if ((ready[0] & 0xFF) == 0) return ESP_ERR_NOT_FINISHED;

    uint16_t w[9];
    ESP_RETURN_ON_ERROR(cmd_query(CMD_READ_VALUES, 20, w, 9), TAG, "read values");

    out->v[AIR_PM1]  = scaled_u16(w[0], 10.0f);
    out->v[AIR_PM25] = scaled_u16(w[1], 10.0f);
    out->v[AIR_PM4]  = scaled_u16(w[2], 10.0f);
    out->v[AIR_PM10] = scaled_u16(w[3], 10.0f);
    out->v[AIR_HUM]  = scaled_i16(w[4], 100.0f);
    out->v[AIR_TEMP] = scaled_i16(w[5], 200.0f);
    out->v[AIR_VOC]  = scaled_i16(w[6], 10.0f);
    out->v[AIR_NOX]  = scaled_i16(w[7], 10.0f);
    out->v[AIR_CO2]  = (w[8] == UNKNOWN_U16) ? NAN : (float)w[8];
    out->valid = true;
    out->age_s = 0;
    return ESP_OK;
}

// ------------------------------------------------------------ informacion
esp_err_t sen66_serial(char *buf, size_t len)
{
    uint16_t w[16];
    ESP_RETURN_ON_ERROR(cmd_query(CMD_SERIAL_NUMBER, 20, w, 16), TAG, "serial");
    words_to_ascii(w, 16, buf, len);
    return ESP_OK;
}

esp_err_t sen66_product_name(char *buf, size_t len)
{
    uint16_t w[16];
    ESP_RETURN_ON_ERROR(cmd_query(CMD_PRODUCT_NAME, 20, w, 16), TAG, "nombre");
    words_to_ascii(w, 16, buf, len);
    return ESP_OK;
}

esp_err_t sen66_version(uint8_t *major, uint8_t *minor)
{
    uint16_t w[1];
    ESP_RETURN_ON_ERROR(cmd_query(CMD_GET_VERSION, 20, w, 1), TAG, "version");
    *major = (uint8_t)(w[0] >> 8);
    *minor = (uint8_t)(w[0] & 0xFF);
    return ESP_OK;
}

esp_err_t sen66_read_status(uint32_t *status)
{
    uint16_t w[2];
    ESP_RETURN_ON_ERROR(cmd_query(CMD_READ_STATUS, 20, w, 2), TAG, "status");
    *status = ((uint32_t)w[0] << 16) | w[1];
    return ESP_OK;
}

esp_err_t sen66_reset(void)
{
    return cmd_write(CMD_DEVICE_RESET, NULL, 0, 1200);
}

esp_err_t sen66_fan_clean(void)
{
    return cmd_write(CMD_FAN_CLEAN, NULL, 0, 20);
}

esp_err_t sen66_forced_co2_recal(uint16_t target_ppm, uint16_t *correction)
{
    ESP_RETURN_ON_ERROR(cmd_write(CMD_FORCED_CO2_RECAL, &target_ppm, 1, 500),
                        TAG, "recal");
    uint16_t w[1];
    ESP_RETURN_ON_ERROR(cmd_read(w, 1), TAG, "recal rx");
    if (correction) *correction = w[0];
    return (w[0] == 0xFFFF) ? ESP_FAIL : ESP_OK;
}

void sen66_status_text(uint32_t status, char *buf, size_t len)
{
    static const struct { uint32_t bit; const char *txt; } k[] = {
        {SEN66_ST_FAN_ERROR,   "ventilador"},
        {SEN66_ST_RHT_ERROR,   "temp/hum"},
        {SEN66_ST_GAS_ERROR,   "VOC/NOx"},
        {SEN66_ST_CO2_1_ERROR, "CO2"},
        {SEN66_ST_CO2_2_ERROR, "CO2(2)"},
        {SEN66_ST_HCHO_ERROR,  "HCHO"},
        {SEN66_ST_PM_ERROR,    "particulas"},
        {SEN66_ST_FAN_WARNING, "aviso ventilador"},
    };
    buf[0] = '\0';
    size_t n = 0;
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); i++) {
        if (!(status & k[i].bit)) continue;
        int w = snprintf(buf + n, len - n, "%s%s", n ? ", " : "", k[i].txt);
        if (w <= 0 || (size_t)w >= len - n) break;
        n += (size_t)w;
    }
    if (!n) snprintf(buf, len, "OK");
}
