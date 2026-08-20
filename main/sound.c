#include "sound.h"
#include "board.h"

#include <math.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "sound";

#define ES8311_ADDR 0x18

#define SAMPLE_RATE 16000
#define MCLK_MULT   256

static i2s_chan_handle_t s_tx;
static i2c_master_dev_handle_t s_codec;
static QueueHandle_t s_queue;
static bool s_ready;
static uint8_t s_volume = 60;

// --------------------------------------------------------------- codec
// El componente espressif/es8311 usa la API ANTIGUA de I2C (i2c_port_t) y
// nosotros la nueva: no pueden convivir en el mismo puerto. Asi que la
// secuencia va a mano, copiada de ese mismo componente, con los divisores
// de reloj fijos para nuestra unica frecuencia (16 kHz con MCLK a 4,096 MHz:
// fila {4096000, 16000, pre_div 1, mult 0, adc/dac_div 1, bclk_div 4,
// osr 0x10, lrck 0x00FF} de su tabla).
static esp_err_t reg_write(uint8_t reg, uint8_t val)
{
    const uint8_t b[2] = {reg, val};
    return i2c_master_transmit(s_codec, b, 2, 100);
}

static esp_err_t reg_read(uint8_t reg, uint8_t *val)
{
    return i2c_master_transmit_receive(s_codec, &reg, 1, val, 1, 100);
}

static esp_err_t codec_init(void)
{
    uint8_t v;
    ESP_RETURN_ON_ERROR(reg_write(0x00, 0x1F), TAG, "reset");
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_RETURN_ON_ERROR(reg_write(0x00, 0x00), TAG, "reset2");
    ESP_RETURN_ON_ERROR(reg_write(0x00, 0x80), TAG, "power on");

    // Relojes: todos activos, MCLK desde el pin MCLK (esta placa lo cablea en
    // GPIO42; la nota antigua de derivarlo de BCLK era de otra revision).
    ESP_RETURN_ON_ERROR(reg_write(0x01, 0x3F), TAG, "clk");
    if (reg_read(0x02, &v) == ESP_OK) reg_write(0x02, v & 0x07);
    reg_write(0x03, 0x10);   // fs_mode 0, adc_osr 0x10
    reg_write(0x04, 0x10);   // dac_osr
    reg_write(0x05, 0x00);   // adc_div y dac_div a 1
    if (reg_read(0x06, &v) == ESP_OK) reg_write(0x06, (uint8_t)((v & 0xE0) | 0x03)); // bclk_div 4
    if (reg_read(0x07, &v) == ESP_OK) reg_write(0x07, (uint8_t)(v & 0xC0));          // lrck_h
    reg_write(0x08, 0xFF);   // lrck_l

    // Formato: esclavo, I2S, 16 bits
    if (reg_read(0x00, &v) == ESP_OK) reg_write(0x00, (uint8_t)(v & 0xBF));
    reg_write(0x09, 3 << 2);
    reg_write(0x0A, 3 << 2);

    reg_write(0x0D, 0x01);   // alimentar la parte analogica
    reg_write(0x0E, 0x02);
    reg_write(0x12, 0x00);   // encender el DAC
    reg_write(0x13, 0x10);   // salida hacia el amplificador
    reg_write(0x1C, 0x6A);
    reg_write(0x37, 0x08);
    return ESP_OK;
}

// Un tono: frecuencia, duracion y silencio posterior.
typedef struct { uint16_t hz, ms, gap_ms; } tone_t;

static void play_tone(const tone_t *t)
{
    if (t->hz) {
        // Onda cuadrada: mas fuerte que una senoidal a igual amplitud y
        // trivial de generar. En un avisador es justo lo que se quiere.
        const int samples = SAMPLE_RATE * t->ms / 1000;
        const int period = SAMPLE_RATE / t->hz;
        static int16_t buf[512];
        int done = 0;
        while (done < samples) {
            const int n = (samples - done > 512) ? 512 : samples - done;
            for (int i = 0; i < n; i++) {
                buf[i] = (((done + i) % period) < period / 2) ? 6000 : -6000;
            }
            size_t written = 0;
            i2s_channel_write(s_tx, buf, n * sizeof(int16_t), &written, 200);
            done += n;
        }
    }
    if (t->gap_ms) {
        static int16_t silencio[256] = {0};
        int samples = SAMPLE_RATE * t->gap_ms / 1000;
        while (samples > 0) {
            const int n = samples > 256 ? 256 : samples;
            size_t written = 0;
            i2s_channel_write(s_tx, silencio, n * sizeof(int16_t), &written, 200);
            samples -= n;
        }
    }
}

static void sound_task(void *arg)
{
    (void)arg;
    sound_effect_t fx;
    for (;;) {
        if (xQueueReceive(s_queue, &fx, portMAX_DELAY) != pdTRUE) continue;

        static const tone_t alerta[] = {{880, 180, 90}, {880, 180, 0}, {0, 0, 0}};
        static const tone_t despejado[] = {{880, 120, 20}, {660, 160, 0}, {0, 0, 0}};
        static const tone_t prueba[] = {{660, 120, 40}, {880, 120, 40}, {1100, 160, 0}, {0, 0, 0}};
        const tone_t *seq = fx == SOUND_ALERT ? alerta
                          : fx == SOUND_CLEAR ? despejado : prueba;

        // El amplificador solo se enciende mientras suena: dejarlo activo
        // mete un siseo constante por el altavoz.
        gpio_set_level(BOARD_I2S_PIN_PA_EN, 1);
        i2s_channel_enable(s_tx);
        for (const tone_t *t = seq; t->hz || t->gap_ms; t++) play_tone(t);
        i2s_channel_disable(s_tx);
        gpio_set_level(BOARD_I2S_PIN_PA_EN, 0);
    }
}

esp_err_t sound_init(i2c_master_bus_handle_t bus)
{
    const gpio_config_t pa = {
        .pin_bit_mask = 1ULL << BOARD_I2S_PIN_PA_EN,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&pa), TAG, "pa_en");
    gpio_set_level(BOARD_I2S_PIN_PA_EN, 0);

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &s_tx, NULL), TAG, "i2s chan");

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                        I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = BOARD_I2S_PIN_MCLK,
            .bclk = BOARD_I2S_PIN_BCLK,
            .ws   = BOARD_I2S_PIN_LRCLK,
            .dout = BOARD_I2S_PIN_DOUT,
            .din  = I2S_GPIO_UNUSED,
        },
    };
    std_cfg.clk_cfg.mclk_multiple = MCLK_MULT;
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_tx, &std_cfg), TAG, "i2s std");

    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = ES8311_ADDR,
        .scl_speed_hz = 400000,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &dev_cfg, &s_codec), TAG, "add codec");

    uint8_t chip = 0;
    if (reg_read(0xFD, &chip) != ESP_OK) {
        ESP_LOGW(TAG, "el codec no responde en 0x%02x", ES8311_ADDR);
        return ESP_ERR_NOT_FOUND;
    }
    ESP_RETURN_ON_ERROR(codec_init(), TAG, "codec");
    sound_set_volume(s_volume);

    s_queue = xQueueCreate(4, sizeof(sound_effect_t));
    ESP_RETURN_ON_FALSE(s_queue, ESP_ERR_NO_MEM, TAG, "cola");
    // En el nucleo 0 con la red: el 1 lleva la pantalla y el sensor.
    xTaskCreatePinnedToCore(sound_task, "sound", 3072, NULL, 4, NULL, 0);

    s_ready = true;
    ESP_LOGI(TAG, "audio listo (ES8311, %d Hz)", SAMPLE_RATE);
    return ESP_OK;
}

bool sound_available(void) { return s_ready; }

void sound_play(sound_effect_t fx)
{
    if (!s_ready) return;
    xQueueSend(s_queue, &fx, 0); // sin esperar: un aviso perdido no es grave
}

void sound_set_volume(uint8_t percent)
{
    if (percent > 100) percent = 100;
    s_volume = percent;
    if (s_codec) reg_write(0x32, percent ? (uint8_t)((percent * 256 / 100) - 1) : 0);
}
