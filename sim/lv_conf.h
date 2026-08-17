// Configuracion de LVGL para el simulador de escritorio (SDL).
// Solo se cambia lo que difiere de los valores por defecto.
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 32

// malloc del sistema: sin el limite de 64KB (capturas de pantalla completa)
#define LV_USE_STDLIB_MALLOC LV_STDLIB_CLIB

#define LV_USE_SDL 1
#define LV_SDL_INCLUDE_PATH <SDL2/SDL.h>

#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_22 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_48 1

#define LV_USE_SNAPSHOT 1

#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF 1

#endif // LV_CONF_H
