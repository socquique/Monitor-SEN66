idf_component_register(
    SRCS
        "app_main.c"
        "air.c"
        "display.c"
        "ha_mqtt.c"
        "history.c"
        "net.c"
        "rtc_pcf85063.c"
        "sen66.c"
        "settings.c"
        "ui.c"
        "webcfg.c"
    INCLUDE_DIRS
        "."
    PRIV_REQUIRES
        driver
        esp_lcd
        esp_timer
        esp_wifi
        esp_netif
        esp_http_server
        esp_app_format
        app_update
        mqtt
        json
        nvs_flash
)

# LVGL genera/espera el include condicional estandar; en ESP-IDF el header es
# "lvgl.h" a secas.
target_compile_definitions(${COMPONENT_LIB} PRIVATE LV_LVGL_H_INCLUDE_SIMPLE)
