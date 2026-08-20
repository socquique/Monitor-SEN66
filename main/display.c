#include "display.h"
#include "board.h"

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_co5300.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch_cst9217.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "display";

#define LCD_CMD_BRIGHTNESS 0x51 // write display brightness (MIPI DCS)

static lv_display_t *s_disp;
static i2c_master_bus_handle_t s_i2c_bus;
static esp_lcd_panel_io_handle_t s_panel_io;

// Los AMOLED QSPI (CO5300) exigen ventanas de volcado alineadas a 2 px; con
// bordes impares el panel pinta basura (lineas verdes) en los limites del
// area. Redondeamos toda area invalidada a inicio par / fin impar.
static void round_area_cb(lv_event_t *e)
{
    lv_area_t *a = lv_event_get_param(e);
    a->x1 &= ~1;
    a->y1 &= ~1;
    a->x2 |= 1;
    a->y2 |= 1;
}

// La GRAM del panel es mayor que los 466 px visibles y las zonas que LVGL
// nunca pinta muestran basura. set_gap no cubre todas las combinaciones con
// rotacion: barremos toda la GRAM a negro una vez (en AMOLED negro = pixel
// apagado, asi que no vuelve a verse).
static void panel_clear_gram(esp_lcd_panel_handle_t panel)
{
    const int gram = 480, stripe = 24;
    uint8_t *black = heap_caps_calloc(1, gram * stripe * 2,
                                      MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!black) return;
    for (int y = 0; y < gram; y += stripe) {
        esp_lcd_panel_draw_bitmap(panel, 0, y, gram, y + stripe, black);
    }
    vTaskDelay(pdMS_TO_TICKS(100)); // las transferencias van por DMA en cola
    free(black);
}

static esp_err_t panel_init(esp_lcd_panel_handle_t *out_panel)
{
    const spi_bus_config_t bus_cfg = CO5300_PANEL_BUS_QSPI_CONFIG(
        BOARD_LCD_PIN_PCLK,
        BOARD_LCD_PIN_DATA0, BOARD_LCD_PIN_DATA1,
        BOARD_LCD_PIN_DATA2, BOARD_LCD_PIN_DATA3,
        BOARD_LCD_H_RES * BOARD_LCD_V_RES * 2);
    ESP_RETURN_ON_ERROR(spi_bus_initialize(BOARD_LCD_QSPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO),
                        TAG, "spi bus");

    esp_lcd_panel_io_spi_config_t io_cfg =
        CO5300_PANEL_IO_QSPI_CONFIG(BOARD_LCD_PIN_CS, NULL, NULL);
    io_cfg.pclk_hz = 80 * 1000 * 1000; // el macro trae 40; el panel aguanta 80
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)BOARD_LCD_QSPI_HOST,
                                                 &io_cfg, &s_panel_io),
                        TAG, "panel io");

    co5300_vendor_config_t vendor_cfg = {
        .flags = { .use_qspi_interface = 1 },
    };
    const esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = BOARD_LCD_PIN_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = &vendor_cfg,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_co5300(s_panel_io, &panel_cfg, out_panel),
                        TAG, "new panel");

    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(*out_panel), TAG, "reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(*out_panel), TAG, "init");
    // La rotacion (MADCTL) la aplica esp_lvgl_port via disp_cfg.rotation;
    // llamarla aqui no sirve, el port la sobreescribe al anadir el display.
    panel_clear_gram(*out_panel);
    ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(*out_panel, 0, 0), TAG, "gap");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(*out_panel, true), TAG, "disp on");
    return ESP_OK;
}

static esp_err_t touch_init(esp_lcd_touch_handle_t *out_touch)
{
    const i2c_master_bus_config_t i2c_cfg = {
        .i2c_port = BOARD_I2C_PORT,
        .sda_io_num = BOARD_I2C_PIN_SDA,
        .scl_io_num = BOARD_I2C_PIN_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&i2c_cfg, &s_i2c_bus), TAG, "i2c bus");

    esp_lcd_panel_io_handle_t tp_io;
    esp_lcd_panel_io_i2c_config_t tp_io_cfg = ESP_LCD_TOUCH_IO_I2C_CST9217_CONFIG();
    // El macro no rellena la frecuencia y la API I2C nueva la exige (si no,
    // bootloop). Ver la chuleta de la placa.
    tp_io_cfg.scl_speed_hz = 400000;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(s_i2c_bus, &tp_io_cfg, &tp_io),
                        TAG, "touch io");

    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = BOARD_LCD_H_RES,
        .y_max = BOARD_LCD_V_RES,
        .rst_gpio_num = BOARD_TOUCH_PIN_RST,
        .int_gpio_num = BOARD_TOUCH_PIN_INT, // sin INT, leer por I2C puede colgar ~1s
        .levels = { .reset = 0, .interrupt = 0 },
        // El tactil aplica la transformacion INVERSA a la del panel
        .flags = {
#if BOARD_LCD_ROTATION == 90
            .swap_xy = 1, .mirror_x = 1,
#elif BOARD_LCD_ROTATION == 180
            .mirror_x = 1, .mirror_y = 1,
#elif BOARD_LCD_ROTATION == 270
            .swap_xy = 1, .mirror_y = 1,
#endif
        },
    };
    return esp_lcd_touch_new_i2c_cst9217(tp_io, &tp_cfg, out_touch);
}

esp_err_t display_init(void)
{
    esp_lcd_panel_handle_t panel;
    ESP_RETURN_ON_ERROR(panel_init(&panel), TAG, "panel");

    esp_lcd_touch_handle_t touch;
    ESP_RETURN_ON_ERROR(touch_init(&touch), TAG, "touch");

    const lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_RETURN_ON_ERROR(lvgl_port_init(&port_cfg), TAG, "lvgl port");

    // El buffer de volcado tiene que estar en RAM interna con DMA: el driver
    // SPI rechaza color desde PSRAM (probado: transmit failed + watchdog).
    // Cuanto mas grande, menos trozos por frame y menos tearing, pero el
    // mayor bloque contiguo libre depende de lo que haya arrancado antes y
    // varia entre proyectos y entre revisiones de IDF (aqui ~144 KB, no los
    // ~192 KB de Hamlet). Lo dimensionamos con lo que hay de verdad en vez
    // de con un numero fijo, que es como se llego a un bootloop.
    const size_t largest =
        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    // 56 KB de margen: con 144 KB contiguos salen 96 filas, que se vuelcan
    // en los mismos 5 trozos por frame que 114, pero dejan 16 KB mas de heap
    // interno para WiFi en modo estacion, MQTT y OTA.
    const size_t reserve = 56 * 1024;
    const size_t usable = largest > reserve ? largest - reserve : largest / 2;
    int rows = (int)(usable / (BOARD_LCD_H_RES * sizeof(uint16_t)));
    if (rows > 185) rows = 185; // por encima no aporta: son 466 lineas
    if (rows < 40) rows = 40;   // por debajo el refresco se nota a tirones
    ESP_LOGI(TAG, "bloque DMA interno libre %u KB -> buffer LVGL de %d filas (%u KB)",
             (unsigned)(largest / 1024), rows,
             (unsigned)(rows * BOARD_LCD_H_RES * 2 / 1024));

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = s_panel_io,
        .panel_handle = panel,
        .buffer_size = BOARD_LCD_H_RES * rows,
        .double_buffer = false,
        .hres = BOARD_LCD_H_RES,
        .vres = BOARD_LCD_V_RES,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .rotation = {
#if BOARD_LCD_ROTATION == 90
            .swap_xy = true, .mirror_y = true,
#elif BOARD_LCD_ROTATION == 180
            .mirror_x = true, .mirror_y = true,
#elif BOARD_LCD_ROTATION == 270
            .swap_xy = true, .mirror_x = true,
#endif
        },
        .flags = {
            .buff_dma = true,
            .swap_bytes = true, // el QSPI AMOLED espera RGB565 big-endian
        },
    };
    s_disp = lvgl_port_add_disp(&disp_cfg);
    ESP_RETURN_ON_FALSE(s_disp, ESP_FAIL, TAG, "add disp");

    lvgl_port_lock(0);
    lv_display_add_event_cb(s_disp, round_area_cb, LV_EVENT_INVALIDATE_AREA, NULL);
    lvgl_port_unlock();

    const lvgl_port_touch_cfg_t touch_cfg = { .disp = s_disp, .handle = touch };
    ESP_RETURN_ON_FALSE(lvgl_port_add_touch(&touch_cfg), ESP_FAIL, TAG, "add touch");

    ESP_LOGI(TAG, "pantalla %dx%d lista", BOARD_LCD_H_RES, BOARD_LCD_V_RES);
    return ESP_OK;
}

lv_display_t *display_get(void) { return s_disp; }
i2c_master_bus_handle_t display_i2c_bus(void) { return s_i2c_bus; }

void display_set_brightness(uint8_t level)
{
    if (!s_panel_io) return;
    esp_lcd_panel_io_tx_param(s_panel_io, LCD_CMD_BRIGHTNESS, (uint8_t[]){level}, 1);
}
