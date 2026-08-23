#include "mic.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "board.h"
#include "sound.h"

#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "mic";

#define ES7210_ADDR   0x40
#define SAMPLE_RATE   16000   // el mismo que sound.c: comparten reloj
#define VENTANA_MS    125     // ponderacion "rapida" de un sonometro
#define TRAMAS_LEIDAS 256     // 256 tramas x 2 canales x 2 bytes = 1 KB

static i2c_master_dev_handle_t s_dev;
static i2s_chan_handle_t s_rx;
static bool s_ready;

static volatile float s_level = NAN;
static volatile float s_peak = NAN;
static const char *s_status = "sin arrancar";
static volatile uint32_t s_reads, s_fails;

static esp_err_t reg_write(uint8_t reg, uint8_t val)
{
    const uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(s_dev, buf, sizeof(buf), 100);
}

// Secuencia copiada del componente oficial espressif/es7210 v1.0.1, que no se
// puede usar tal cual porque habla la API VIEJA de I2C (i2c_port_t) y este
// proyecto usa i2c_master_bus_handle_t. Mismo caso que el ES8311.
//
// Los coeficientes de reloj son los de su tabla para MCLK 4,096 MHz con
// LRCK 16 kHz, que es justo lo que sale de sound.c (16 kHz x 256).
static esp_err_t codec_init(void)
{
    // Reset.
    ESP_RETURN_ON_ERROR(reg_write(0x00, 0xFF), TAG, "reset");
    ESP_RETURN_ON_ERROR(reg_write(0x00, 0x32), TAG, "reset2");
    // Tiempos de arranque.
    ESP_RETURN_ON_ERROR(reg_write(0x09, 0x30), TAG, "t0");
    ESP_RETURN_ON_ERROR(reg_write(0x0A, 0x30), TAG, "t1");
    // Filtro paso alto de los cuatro ADC: quita la continua, que si no se
    // come el margen y falsea el RMS.
    ESP_RETURN_ON_ERROR(reg_write(0x23, 0x2A), TAG, "hpf");
    ESP_RETURN_ON_ERROR(reg_write(0x22, 0x0A), TAG, "hpf");
    ESP_RETURN_ON_ERROR(reg_write(0x21, 0x2A), TAG, "hpf");
    ESP_RETURN_ON_ERROR(reg_write(0x20, 0x0A), TAG, "hpf");
    // Formato I2S normal, 16 bits. Sin TDM: salen dos ranuras, y en este
    // chip la primera es MIC1 y la segunda MIC3, que NO es un microfono sino
    // la referencia de reproduccion para cancelar eco. Por eso mas abajo se
    // usa solo la izquierda.
    ESP_RETURN_ON_ERROR(reg_write(0x11, 0x60), TAG, "fmt");
    ESP_RETURN_ON_ERROR(reg_write(0x12, 0x00), TAG, "fmt2");
    // Alimentacion analogica y VMID.
    ESP_RETURN_ON_ERROR(reg_write(0x40, 0xC3), TAG, "analog");
    // Polarizacion de los micros, 2,87 V.
    ESP_RETURN_ON_ERROR(reg_write(0x41, 0x70), TAG, "bias");
    ESP_RETURN_ON_ERROR(reg_write(0x42, 0x70), TAG, "bias");
    // Ganancia 30 dB en los cuatro (valor 10 | 0x10).
    for (uint8_t r = 0x43; r <= 0x46; r++) {
        ESP_RETURN_ON_ERROR(reg_write(r, 0x1A), TAG, "gain");
    }
    // Encender MIC1-4.
    for (uint8_t r = 0x47; r <= 0x4A; r++) {
        ESP_RETURN_ON_ERROR(reg_write(r, 0x08), TAG, "mic on");
    }
    // Relojes para 4,096 MHz / 16 kHz: osr 0x20, adc_div 1, doubler 1, dll 1.
    ESP_RETURN_ON_ERROR(reg_write(0x07, 0x20), TAG, "osr");
    ESP_RETURN_ON_ERROR(reg_write(0x02, 0x01 | (1 << 6) | (1 << 7)), TAG, "clk");
    ESP_RETURN_ON_ERROR(reg_write(0x04, 0x01), TAG, "lrckh");
    ESP_RETURN_ON_ERROR(reg_write(0x05, 0x00), TAG, "lrckl");
    // Apagar la DLL y encender bias, ADC y PGA.
    ESP_RETURN_ON_ERROR(reg_write(0x06, 0x04), TAG, "dll");
    ESP_RETURN_ON_ERROR(reg_write(0x4B, 0x0F), TAG, "pwr");
    ESP_RETURN_ON_ERROR(reg_write(0x4C, 0x0F), TAG, "pwr");
    // Arrancar.
    ESP_RETURN_ON_ERROR(reg_write(0x00, 0x71), TAG, "start");
    ESP_RETURN_ON_ERROR(reg_write(0x00, 0x41), TAG, "start2");
    return ESP_OK;
}

static void mic_task(void *arg)
{
    (void)arg;
    static int16_t buf[TRAMAS_LEIDAS * 2]; // estatico: no cabe en la pila

    const int por_ventana = SAMPLE_RATE * VENTANA_MS / 1000;
    int64_t suma = 0;      // suma de cuadrados
    int n = 0;
    int32_t pico = 0;

    for (;;) {
        size_t leidos = 0;
        if (i2s_channel_read(s_rx, buf, sizeof(buf), &leidos, 500) != ESP_OK) {
            s_fails++;
            continue;
        }
        s_reads++;
        const int tramas = leidos / (2 * sizeof(int16_t));
        for (int i = 0; i < tramas; i++) {
            // Solo la ranura izquierda: la derecha es la referencia de
            // reproduccion, no un microfono.
            const int32_t m = buf[i * 2];
            suma += (int64_t)m * m;
            const int32_t abs_m = m < 0 ? -m : m;
            if (abs_m > pico) pico = abs_m;
            if (++n >= por_ventana) {
                const double rms = sqrt((double)suma / n);
                s_level = (rms > 0.5) ? 20.0f * log10f((float)rms / 32768.0f) : -96.0f;
                s_peak = (pico > 0) ? 20.0f * log10f((float)pico / 32768.0f) : -96.0f;
                suma = 0; n = 0; pico = 0;
            }
        }
    }
}

esp_err_t mic_init(i2c_master_bus_handle_t bus)
{
    s_rx = sound_i2s_rx();
    if (!s_rx) {
        s_status = "sin canal I2S de recepcion";
        return ESP_ERR_INVALID_STATE;
    }

    const i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = ES7210_ADDR,
        .scl_speed_hz = 400000,
    };
    // Se comprueba con un sondeo de direccion, no leyendo un registro de ID:
    // el ES7210 no documenta ninguno, y el driver oficial de
    // Espressif tampoco lo lee. Preguntar por un registro inventado da un
    // NACK y parece que el chip no esta cuando si esta.
    if (i2c_master_probe(bus, ES7210_ADDR, 100) != ESP_OK) {
        ESP_LOGW(TAG, "el ES7210 no contesta en 0x%02x", ES7210_ADDR);
        s_status = "el ES7210 no contesta en 0x40";
        return ESP_ERR_NOT_FOUND;
    }
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &cfg, &s_dev), TAG, "add dev");
    ESP_LOGI(TAG, "ES7210 presente en 0x%02x", ES7210_ADDR);

    // El canal PRIMERO y el codec despues. Habilitar el canal es lo que pone
    // en marcha MCLK, BCLK y WS, y el ES7210 engancha sus divisores y la DLL
    // con el reloj presente: configurado a oscuras se queda mudo.
    esp_err_t e = i2s_channel_enable(s_rx);
    if (e != ESP_OK) { s_status = "no se pudo habilitar la recepcion I2S"; return e; }
    vTaskDelay(pdMS_TO_TICKS(20));
    e = codec_init();
    if (e != ESP_OK) { s_status = "fallo configurando el ES7210"; return e; }

    // En el nucleo 0 con la red y el audio: el 1 lleva pantalla y sensor.
    xTaskCreatePinnedToCore(mic_task, "mic", 3072, NULL, 4, NULL, 0);

    s_ready = true;
    s_status = "ok";
    ESP_LOGI(TAG, "microfono listo (%d Hz, ventana %d ms)", SAMPLE_RATE, VENTANA_MS);
    return ESP_OK;
}

bool mic_available(void) { return s_ready; }
float mic_level_dbfs(void) { return s_level; }
float mic_peak_dbfs(void) { return s_peak; }

const char *mic_status(void)
{
    static char buf[64];
    if (!s_ready) return s_status;
    snprintf(buf, sizeof(buf), "ok (%u lecturas, %u fallos)",
             (unsigned)s_reads, (unsigned)s_fails);
    return buf;
}
