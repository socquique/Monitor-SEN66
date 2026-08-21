#include "fonts.h"

lv_font_t ui_font_14, ui_font_16, ui_font_22, ui_font_28, ui_font_48;

void ui_fonts_init(void)
{
    ui_font_14 = lv_font_montserrat_14; ui_font_14.fallback = &font_de_14;
    ui_font_16 = lv_font_montserrat_16; ui_font_16.fallback = &font_de_16;
    ui_font_22 = lv_font_montserrat_22; ui_font_22.fallback = &font_de_22;
    ui_font_28 = lv_font_montserrat_28; ui_font_28.fallback = &font_de_28;
    ui_font_48 = lv_font_montserrat_48; ui_font_48.fallback = &font_de_48;
}
