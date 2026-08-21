// Fuentes de la interfaz: las Montserrat de LVGL con una reserva encadenada
// que aporta los glifos alemanes (ÄÖÜäöüß), que las de serie no traen.
//
// No se puede tocar lv_font_montserrat_XX directamente: son const y viven en
// flash. Lo que hacemos es una copia en RAM de cada una con el campo
// `fallback` relleno, y la interfaz usa esas copias. Son ~30 bytes cada una.
#pragma once

#include "lvgl.h"

extern const lv_font_t font_de_14, font_de_16, font_de_22, font_de_28, font_de_48;

// Copias con reserva. Usar SIEMPRE estas en la UI, no las de LVGL.
extern lv_font_t ui_font_14, ui_font_16, ui_font_22, ui_font_28, ui_font_48;

void ui_fonts_init(void);
