#include "ui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "lvgl.h"

#define SCR_W 466
#define SCR_H 466
#define CHART_POINTS 60

// Margen para que nada caiga en el borde curvo del panel redondo.
#define EDGE 34

// Diametro de los anillos grandes. Antes era 322 para dejar sitio arriba a
// la hora y el estado de red; ahora esos van DENTRO del anillo, asi que el
// arco puede irse casi al borde. 380 deja 43 px hasta el cristal.
#define RING_D 380

// Hueco util dentro del anillo: radio exterior menos el grosor del arco.
// De aqui salen todas las coordenadas de las paginas.
#define RING_INNER_R ((RING_D) / 2 - 22)

enum { PAGE_SUMMARY = 0, PAGE_CO2, PAGE_PM, PAGE_GAS, PAGE_CLIMATE };

// Indicador circular reutilizable: arco de 270 grados + valor + unidad.
typedef struct {
    lv_obj_t *arc;
    lv_obj_t *value;
    lv_obj_t *unit;
    lv_obj_t *caption;
} gauge_t;

static air_history_t *s_hist;
static ui_config_t s_cfg;
static void (*s_set_brightness)(uint8_t);

static lv_obj_t *s_tv;
static lv_obj_t *s_status_time;
static lv_obj_t *s_status_msg;
static lv_obj_t *s_status_net;
static lv_obj_t *s_status_bat;
static lv_obj_t *s_dots[UI_PAGE_COUNT];

// pagina -> columna del tileview (-1 si esa pagina esta oculta)
static int s_page_col[UI_PAGE_COUNT];
static int s_col_page[UI_PAGE_COUNT];
static int s_cols;
static int s_cur_col;

static gauge_t s_g_summary, s_g_co2, s_g_pm, s_g_voc, s_g_nox;
static lv_obj_t *s_sum_center, *s_sum_sub;
static lv_obj_t *s_sum_name[4], *s_sum_val[4];
static lv_obj_t *s_pm_level, *s_pm_name[3], *s_pm_val[3];
static lv_obj_t *s_co2_level, *s_co2_chart;
static lv_chart_series_t *s_co2_ser;
static lv_obj_t *s_cl_temp, *s_cl_hum, *s_cl_chart;
static lv_chart_series_t *s_cl_ser, *s_cl_ser_hum;

static uint32_t s_idle_s;
static uint32_t s_dwell_s;
static bool s_dimmed;
static air_sample_t s_last;

// ------------------------------------------------------------------ helpers
static lv_color_t col(uint32_t rgb) { return lv_color_hex(rgb); }

static void fmt_value(char *buf, size_t len, air_metric_t m, float v)
{
    if (isnan(v)) { snprintf(buf, len, "--"); return; }
    snprintf(buf, len, "%.*f", air_metric_decimals(m), v);
}

static lv_obj_t *plain(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, col(color), 0);
    lv_label_set_text(l, "");
    return l;
}

// Valor secundario: rotulo pequeno con la unidad arriba y la cifra sola
// debajo. Separar unidad y numero deja la cifra mas grande en el mismo ancho,
// que dentro de un anillo escasea.
static void mini_create(lv_obj_t *parent, lv_obj_t **name, lv_obj_t **value,
                        int dx, int dy, const lv_font_t *font_value)
{
    *name = make_label(parent, &lv_font_montserrat_14, 0x64748b);
    lv_obj_set_style_text_align(*name, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(*name, LV_ALIGN_CENTER, dx, dy);
    *value = make_label(parent, font_value, 0xffffff);
    lv_obj_set_style_text_align(*value, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(*value, LV_ALIGN_CENTER, dx, dy + 22);
}

static void gauge_create(gauge_t *g, lv_obj_t *parent, int size, int thickness,
                         const lv_font_t *font_value, const lv_font_t *font_small)
{
    g->arc = lv_arc_create(parent);
    lv_obj_set_size(g->arc, size, size);
    lv_obj_center(g->arc);
    lv_arc_set_rotation(g->arc, 0);
    lv_arc_set_bg_angles(g->arc, 135, 45); // 270 grados, hueco abajo
    lv_arc_set_range(g->arc, 0, 1000);
    lv_arc_set_value(g->arc, 0);
    lv_obj_remove_style(g->arc, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(g->arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(g->arc, thickness, LV_PART_MAIN);
    // El indicador, mas fino que la pista: asi un valor pequeno se lee como
    // el arranque de un relleno dentro de una ranura y no como un pegote
    // suelto, que es lo que pasaba con ambos al mismo grosor.
    lv_obj_set_style_arc_width(g->arc, thickness > 16 ? thickness - 8 : thickness - 4,
                               LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(g->arc, col(0x1e293b), LV_PART_MAIN);
    lv_obj_set_style_arc_color(g->arc, col(0x22c55e), LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(g->arc, true, LV_PART_INDICATOR);

    g->value = make_label(parent, font_value, 0xffffff);
    lv_obj_align(g->value, LV_ALIGN_CENTER, 0, -10);
    g->unit = make_label(parent, font_small, 0x94a3b8);
    lv_obj_align_to(g->unit, g->value, LV_ALIGN_OUT_BOTTOM_MID, 0, 2);
    g->caption = make_label(parent, font_small, 0x64748b);
    lv_obj_align(g->caption, LV_ALIGN_CENTER, 0, -size / 2 + thickness + 22);
}

static void gauge_update(gauge_t *g, air_metric_t m, float v)
{
    const air_level_t lvl = air_level(m, v);
    lv_obj_set_style_arc_color(g->arc, col(air_level_color(lvl)), LV_PART_INDICATOR);

    const float lo = air_gauge_min(m), hi = air_gauge_max(m);
    int32_t pos = 0;
    if (!isnan(v) && hi > lo) {
        float f = (v - lo) / (hi - lo);
        if (f < 0) f = 0;
        if (f > 1) f = 1;
        pos = (int32_t)(f * 1000.0f);
    }
    lv_arc_set_value(g->arc, pos);

    char buf[16];
    fmt_value(buf, sizeof(buf), m, v);
    lv_label_set_text(g->value, buf);
    lv_label_set_text(g->unit, air_metric_unit_ui(m));
    lv_label_set_text(g->caption, air_metric_label(m));
    // el valor cambia de anchura: realinear la unidad debajo
    lv_obj_align_to(g->unit, g->value, LV_ALIGN_OUT_BOTTOM_MID, 0, 2);
}

static void chart_create(lv_obj_t **out_chart, lv_chart_series_t **out_ser,
                         lv_obj_t *parent, int w, int h, uint32_t color)
{
    lv_obj_t *c = lv_chart_create(parent);
    lv_obj_set_size(c, w, h);
    lv_chart_set_type(c, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(c, CHART_POINTS);
    lv_chart_set_div_line_count(c, 0, 0); // sin rejilla: la linea sola
    lv_chart_set_update_mode(c, LV_CHART_UPDATE_MODE_SHIFT);
    lv_obj_set_style_bg_opa(c, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_pad_all(c, 0, 0);
    lv_obj_set_style_line_color(c, col(0x1e293b), LV_PART_MAIN);
    lv_obj_set_style_line_width(c, 2, LV_PART_ITEMS);
    lv_obj_set_style_width(c, 0, LV_PART_INDICATOR);  // sin puntos
    lv_obj_set_style_height(c, 0, LV_PART_INDICATOR);
    *out_ser = lv_chart_add_series(c, col(color), LV_CHART_AXIS_PRIMARY_Y);
    *out_chart = c;
}

// Segunda curva sobre la misma grafica, con su propia escala vertical: sirve
// para superponer magnitudes de rango distinto (temperatura y humedad).
static lv_chart_series_t *chart_add_secondary(lv_obj_t *c, uint32_t color)
{
    return lv_chart_add_series(c, col(color), LV_CHART_AXIS_SECONDARY_Y);
}

// Vuelca la ventana del historico en la serie, con huecos donde no hay dato.
static void chart_refresh_axis(lv_obj_t *chart, lv_chart_series_t *ser,
                               air_metric_t m, lv_chart_axis_t axis)
{
    if (!s_hist) return;
    const uint32_t span = (uint32_t)s_cfg.chart_span_min * 60;

    float pts[CHART_POINTS];
    air_history_series(s_hist, m, span, pts, CHART_POINTS);

    float lo, hi;
    if (!air_history_range(s_hist, m, span, &lo, &hi)) {
        lo = air_gauge_min(m);
        hi = air_gauge_max(m);
    }
    // lv_chart trabaja con enteros: para las metricas con decimales (la
    // temperatura se mueve en decimas) escalamos x10, o la curva sale
    // escalonada.
    const float k = air_metric_decimals(m) ? 10.0f : 1.0f;

    // margen del 10% y un minimo para que una linea plana no salga pegada
    // al borde
    float pad = (hi - lo) * 0.1f;
    if (pad < 0.5f) pad = 0.5f;
    lv_chart_set_range(chart, axis,
                       (int32_t)floorf((lo - pad) * k),
                       (int32_t)ceilf((hi + pad) * k));

    for (int i = 0; i < CHART_POINTS; i++) {
        lv_chart_set_value_by_id(chart, ser, i,
                                 isnan(pts[i]) ? LV_CHART_POINT_NONE
                                               : (int32_t)lrintf(pts[i] * k));
    }
}

static void chart_refresh(lv_obj_t *chart, lv_chart_series_t *ser, air_metric_t m)
{
    chart_refresh_axis(chart, ser, m, LV_CHART_AXIS_PRIMARY_Y);
}

// -------------------------------------------------------------------- paginas
static void build_summary(lv_obj_t *tile)
{
    gauge_create(&s_g_summary, tile, RING_D, 22,
                 &lv_font_montserrat_28, &lv_font_montserrat_16);
    // en el resumen el centro lleva texto, no numero
    lv_obj_add_flag(s_g_summary.value, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_g_summary.unit, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_g_summary.caption, LV_OBJ_FLAG_HIDDEN);
    // Este anillo no mide: es el semaforo y se pinta entero. Devolvemos al
    // indicador el grosor de la pista, o al llenarlo asoma el fondo por fuera
    // y parece un fallo de dibujo.
    lv_obj_set_style_arc_width(s_g_summary.arc, 22, LV_PART_INDICATOR);

    s_sum_center = make_label(tile, &lv_font_montserrat_28, 0xffffff);
    lv_obj_align(s_sum_center, LV_ALIGN_CENTER, 0, -66);
    s_sum_sub = make_label(tile, &lv_font_montserrat_16, 0x94a3b8);
    lv_obj_align(s_sum_sub, LV_ALIGN_CENTER, 0, -32);

    // Rejilla 2x2 de secundarios. Con el anillo a 380 el hueco libre tiene
    // radio 168 y a y=+101 quedan 134 px a cada lado, sitio de sobra para
    // separarlos mas que antes.
    static const int dx[4] = {-74, 74, -74, 74};
    static const int dy[4] = {8, 8, 68, 68};
    for (int i = 0; i < 4; i++) {
        mini_create(tile, &s_sum_name[i], &s_sum_val[i], dx[i], dy[i],
                    &lv_font_montserrat_22);
    }
}

static void build_co2(lv_obj_t *tile)
{
    // La grafica va PRIMERO para quedar por debajo de todo lo demas: cruza la
    // pantalla de lado a lado y el anillo y el numero se pintan encima. El
    // anillo la tapa donde la cruza, asi que la linea parece pasar por debajo
    // y salir por los dos bordes.
    chart_create(&s_co2_chart, &s_co2_ser, tile, 430, 90, 0x38bdf8);
    lv_obj_align(s_co2_chart, LV_ALIGN_CENTER, 0, 100);

    gauge_create(&s_g_co2, tile, RING_D, 22,
                 &lv_font_montserrat_48, &lv_font_montserrat_16);
    lv_obj_align(s_g_co2.value, LV_ALIGN_CENTER, 0, -44);
    lv_obj_align(s_g_co2.caption, LV_ALIGN_CENTER, 0, -96);

    s_co2_level = make_label(tile, &lv_font_montserrat_22, 0xffffff);
    lv_obj_align(s_co2_level, LV_ALIGN_CENTER, 0, 26);
}

static void build_pm(lv_obj_t *tile)
{
    // Misma gramatica visual que el CO2: anillo y cifra grande. Antes eran
    // cuatro barras identicas en letra de 16 px, sin jerarquia y dejando
    // vacio el tercio inferior. La PM2.5 es la que importa para salud, asi
    // que se lleva el centro y las otras tres acompanan.
    gauge_create(&s_g_pm, tile, RING_D, 22,
                 &lv_font_montserrat_48, &lv_font_montserrat_16);
    lv_obj_align(s_g_pm.value, LV_ALIGN_CENTER, 0, -48);
    lv_obj_align(s_g_pm.caption, LV_ALIGN_CENTER, 0, -100);

    s_pm_level = make_label(tile, &lv_font_montserrat_22, 0xffffff);
    lv_obj_align(s_pm_level, LV_ALIGN_CENTER, 0, 22);

    static const int dx[3] = {-95, 0, 95};
    for (int i = 0; i < 3; i++) {
        mini_create(tile, &s_pm_name[i], &s_pm_val[i], dx[i], 64,
                    &lv_font_montserrat_22);
    }
}

static void build_gas(lv_obj_t *tile)
{
    lv_obj_t *title = make_label(tile, &lv_font_montserrat_16, 0x94a3b8);
    lv_label_set_text(title, "Gases");
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -96);

    // Dos esferas de 168 centradas en x=+-88: la esquina exterior queda a 194
    // px del centro de pantalla, dentro de los ~199 utiles. Con 176 se salian.
    lv_obj_t *left = plain(tile);
    lv_obj_set_size(left, 176, 176);
    lv_obj_align(left, LV_ALIGN_CENTER, -88, 14);
    gauge_create(&s_g_voc, left, 168, 16, &lv_font_montserrat_48, &lv_font_montserrat_14);

    lv_obj_t *right = plain(tile);
    lv_obj_set_size(right, 176, 176);
    lv_obj_align(right, LV_ALIGN_CENTER, 88, 14);
    gauge_create(&s_g_nox, right, 168, 16, &lv_font_montserrat_48, &lv_font_montserrat_14);

    lv_obj_t *hint = make_label(tile, &lv_font_montserrat_14, 0x64748b);
    lv_label_set_text(hint, "100 = ambiente habitual");
    lv_obj_align(hint, LV_ALIGN_CENTER, 0, 134);
}

static void build_climate(lv_obj_t *tile)
{
    lv_obj_t *title = make_label(tile, &lv_font_montserrat_16, 0x94a3b8);
    lv_label_set_text(title, "Clima");
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -96);

    // Las dos cifras al mismo tamano: la humedad tambien es un dato, no un
    // pie de foto. Cada una del color de su curva, que asi la grafica se
    // explica sola y no hace falta leyenda.
    // Lado a lado en vez de apiladas: deja sitio abajo para que la curva
    // ocupe todo el ancho, que es el efecto que se buscaba.
    s_cl_temp = make_label(tile, &lv_font_montserrat_48, 0xf59e0b);
    lv_obj_align(s_cl_temp, LV_ALIGN_CENTER, -88, -40);
    s_cl_hum = make_label(tile, &lv_font_montserrat_48, 0x38bdf8);
    lv_obj_align(s_cl_hum, LV_ALIGN_CENTER, 88, -40);

    // Sin anillo esta pagina puede permitirse una grafica ancha. Las dos
    // curvas comparten dibujo pero no escala: la humedad va al eje secundario
    // porque se mueve por el 50 % y la temperatura por los 25 grados.
    chart_create(&s_cl_chart, &s_cl_ser, tile, 430, 110, 0xf59e0b);
    s_cl_ser_hum = chart_add_secondary(s_cl_chart, 0x38bdf8);
    lv_obj_align(s_cl_chart, LV_ALIGN_CENTER, 0, 70);
}

// ------------------------------------------------------------------ estado
static void build_chrome(lv_obj_t *scr)
{
    // Hora y estado de red DENTRO del anillo, no encima: es lo que permite
    // que el arco crezca hasta el borde sin que el texto caiga sobre el color.
    s_status_time = make_label(scr, &lv_font_montserrat_16, 0xe2e8f0);
    lv_obj_align(s_status_time, LV_ALIGN_CENTER, 0, -146);

    s_status_net = make_label(scr, &lv_font_montserrat_14, 0x475569);
    lv_obj_align(s_status_net, LV_ALIGN_CENTER, 0, -124);

    // La bateria va abajo, en el hueco que el anillo deja libre: ahi no
    // estorba a ninguna pagina y se ve de un vistazo.
    s_status_bat = make_label(scr, &lv_font_montserrat_16, 0x94a3b8);
    lv_obj_align(s_status_bat, LV_ALIGN_CENTER, 0, 148);

    s_status_msg = make_label(scr, &lv_font_montserrat_14, 0xfacc15);
    lv_obj_align(s_status_msg, LV_ALIGN_BOTTOM_MID, 0, -52);

    for (int i = 0; i < UI_PAGE_COUNT; i++) {
        s_dots[i] = lv_obj_create(scr);
        lv_obj_remove_style_all(s_dots[i]);
        lv_obj_set_size(s_dots[i], 7, 7);
        lv_obj_set_style_radius(s_dots[i], 4, 0);
        lv_obj_set_style_bg_opa(s_dots[i], LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(s_dots[i], col(0x334155), 0);
        lv_obj_add_flag(s_dots[i], LV_OBJ_FLAG_HIDDEN);
    }
}

static void layout_dots(void)
{
    const int gap = 16;
    const int x0 = -(s_cols - 1) * gap / 2;
    for (int c = 0; c < s_cols; c++) {
        lv_obj_t *d = s_dots[c];
        lv_obj_remove_flag(d, LV_OBJ_FLAG_HIDDEN);
        lv_obj_align(d, LV_ALIGN_BOTTOM_MID, x0 + c * gap, -28);
    }
}

static void update_dots(void)
{
    for (int c = 0; c < s_cols; c++) {
        lv_obj_set_style_bg_color(s_dots[c], col(c == s_cur_col ? 0xe2e8f0 : 0x334155), 0);
    }
}

static void on_tile_change(lv_event_t *e)
{
    (void)e;
    lv_obj_t *act = lv_tileview_get_tile_active(s_tv);
    for (int c = 0; c < s_cols; c++) {
        if (lv_obj_get_child(s_tv, c) == act) { s_cur_col = c; break; }
    }
    update_dots();
    s_dwell_s = 0;
}

static void on_activity(lv_event_t *e)
{
    (void)e;
    ui_wake();
}

// -------------------------------------------------------------------- api
void ui_init(air_history_t *hist, const ui_config_t *cfg,
             void (*set_brightness)(uint8_t))
{
    s_hist = hist;
    s_cfg = *cfg;
    s_set_brightness = set_brightness;
    air_sample_clear(&s_last);

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, col(0x000000), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(scr, on_activity, LV_EVENT_PRESSED, NULL);

    s_tv = lv_tileview_create(scr);
    lv_obj_set_size(s_tv, SCR_W, SCR_H);
    lv_obj_set_style_bg_opa(s_tv, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_tv, 0, 0);
    lv_obj_set_scrollbar_mode(s_tv, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(s_tv, on_tile_change, LV_EVENT_VALUE_CHANGED, NULL);

    // Solo se crean las paginas habilitadas, en su orden natural. Cambiar la
    // mascara requiere reiniciar (el servidor web reinicia al guardar).
    s_cols = 0;
    for (int p = 0; p < UI_PAGE_COUNT; p++) {
        s_page_col[p] = -1;
        if (!((s_cfg.pages_mask >> p) & 1)) continue;
        lv_obj_t *tile = lv_tileview_add_tile(s_tv, s_cols, 0, LV_DIR_HOR);
        lv_obj_remove_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
        switch (p) {
        case PAGE_SUMMARY: build_summary(tile); break;
        case PAGE_CO2:     build_co2(tile);     break;
        case PAGE_PM:      build_pm(tile);      break;
        case PAGE_GAS:     build_gas(tile);     break;
        case PAGE_CLIMATE: build_climate(tile); break;
        default: break;
        }
        s_page_col[p] = s_cols;
        s_col_page[s_cols] = p;
        s_cols++;
    }
    if (s_cols == 0) { // mascara vacia: al menos el resumen
        lv_obj_t *tile = lv_tileview_add_tile(s_tv, 0, 0, LV_DIR_HOR);
        build_summary(tile);
        s_page_col[PAGE_SUMMARY] = 0;
        s_col_page[0] = PAGE_SUMMARY;
        s_cols = 1;
    }

    build_chrome(scr);
    layout_dots();
    update_dots();
    if (s_set_brightness) s_set_brightness(s_cfg.brightness);
}

void ui_update(const air_sample_t *s)
{
    s_last = *s;
    char buf[48], buf2[16];

    for (int p = 0; p < UI_PAGE_COUNT; p++) {
        if (s_page_col[p] < 0) continue;
        switch (p) {
        case PAGE_SUMMARY: {
            const air_level_t lvl = air_overall(s);
            const air_metric_t drv = air_overall_driver(s);
            lv_obj_set_style_arc_color(s_g_summary.arc, col(air_level_color(lvl)),
                                       LV_PART_INDICATOR);
            // El anillo del resumen no mide nada: es el semaforo. Se pinta
            // entero del color del nivel (vacio solo mientras no hay datos).
            lv_arc_set_value(s_g_summary.arc, lvl == AIR_LVL_UNKNOWN ? 0 : 1000);
            lv_label_set_text(s_sum_center, air_level_text(lvl));
            lv_obj_set_style_text_color(s_sum_center, col(air_level_color(lvl)), 0);
            if (lvl == AIR_LVL_UNKNOWN) {
                lv_label_set_text(s_sum_sub, "esperando datos");
            } else if (lvl == AIR_LVL_GOOD) {
                lv_label_set_text(s_sum_sub, "todo en orden");
            } else {
                snprintf(buf, sizeof(buf), "manda el %s", air_metric_label(drv));
                lv_label_set_text(s_sum_sub, buf);
            }
            static const air_metric_t mini[4] = {AIR_CO2, AIR_PM25, AIR_TEMP, AIR_HUM};
            // Rotulos abreviados a proposito: "Temperatura" no cabe a 62 px
            // del eje sin salirse del hueco del anillo.
            static const char *mininame[4] = {"CO2 ppm", "PM2.5 ug/m3", "Temp C", "Humedad %"};
            for (int i = 0; i < 4; i++) {
                lv_label_set_text(s_sum_name[i], mininame[i]);
                fmt_value(buf2, sizeof(buf2), mini[i], s->v[mini[i]]);
                lv_label_set_text(s_sum_val[i], buf2);
                lv_obj_set_style_text_color(s_sum_val[i],
                    col(air_level_color(air_level(mini[i], s->v[mini[i]]))), 0);
            }
            break;
        }
        case PAGE_CO2: {
            gauge_update(&s_g_co2, AIR_CO2, s->v[AIR_CO2]);
            lv_obj_align(s_g_co2.value, LV_ALIGN_CENTER, 0, -44);
            lv_obj_align_to(s_g_co2.unit, s_g_co2.value, LV_ALIGN_OUT_BOTTOM_MID, 0, 2);
            const air_level_t lvl = air_level(AIR_CO2, s->v[AIR_CO2]);
            lv_label_set_text(s_co2_level, air_level_text(lvl));
            lv_obj_set_style_text_color(s_co2_level, col(air_level_color(lvl)), 0);
            chart_refresh(s_co2_chart, s_co2_ser, AIR_CO2);
            break;
        }
        case PAGE_PM: {
            gauge_update(&s_g_pm, AIR_PM25, s->v[AIR_PM25]);
            lv_obj_align(s_g_pm.value, LV_ALIGN_CENTER, 0, -48);
            lv_obj_align_to(s_g_pm.unit, s_g_pm.value, LV_ALIGN_OUT_BOTTOM_MID, 0, 2);
            const air_level_t lvl = air_level(AIR_PM25, s->v[AIR_PM25]);
            lv_label_set_text(s_pm_level, air_level_text(lvl));
            lv_obj_set_style_text_color(s_pm_level, col(air_level_color(lvl)), 0);

            static const air_metric_t otras[3] = {AIR_PM1, AIR_PM4, AIR_PM10};
            for (int i = 0; i < 3; i++) {
                const float v = s->v[otras[i]];
                lv_label_set_text(s_pm_name[i], air_metric_label(otras[i]));
                fmt_value(buf2, sizeof(buf2), otras[i], v);
                lv_label_set_text(s_pm_val[i], buf2);
                lv_obj_set_style_text_color(s_pm_val[i],
                    col(air_level_color(air_level(otras[i], v))), 0);
            }
            break;
        }
        case PAGE_GAS:
            gauge_update(&s_g_voc, AIR_VOC, s->v[AIR_VOC]);
            gauge_update(&s_g_nox, AIR_NOX, s->v[AIR_NOX]);
            break;
        case PAGE_CLIMATE: {
            fmt_value(buf2, sizeof(buf2), AIR_TEMP, s->v[AIR_TEMP]);
            snprintf(buf, sizeof(buf), "%s C", buf2);
            lv_label_set_text(s_cl_temp, buf);
            fmt_value(buf2, sizeof(buf2), AIR_HUM, s->v[AIR_HUM]);
            snprintf(buf, sizeof(buf), "%s %%", buf2);
            lv_label_set_text(s_cl_hum, buf);
            chart_refresh_axis(s_cl_chart, s_cl_ser, AIR_TEMP, LV_CHART_AXIS_PRIMARY_Y);
            chart_refresh_axis(s_cl_chart, s_cl_ser_hum, AIR_HUM, LV_CHART_AXIS_SECONDARY_Y);
            break;
        }
        default: break;
        }
    }
}

void ui_set_status(const char *time_str, bool wifi, bool mqtt, const char *msg,
                   int bat_pct, bool charging)
{
    if (!s_status_time) return;
    lv_label_set_text(s_status_time, time_str ? time_str : "");

    char net[32];
    snprintf(net, sizeof(net), "%s%s%s",
             wifi ? "wifi" : "sin wifi",
             (wifi && mqtt) ? " + " : "",
             (wifi && mqtt) ? "mqtt" : "");
    lv_label_set_text(s_status_net, net);
    lv_obj_set_style_text_color(s_status_net, col(wifi ? (mqtt ? 0x22c55e : 0x94a3b8)
                                                      : 0x64748b), 0);

    lv_label_set_text(s_status_msg, msg ? msg : "");

    if (bat_pct < 0) {
        lv_label_set_text(s_status_bat, "");
    } else {
        // Los simbolos de bateria son glifos de las propias fuentes de LVGL,
        // asi que no rompen la regla de solo-ASCII de los textos.
        const char *icono = charging      ? LV_SYMBOL_CHARGE
                          : bat_pct >= 80 ? LV_SYMBOL_BATTERY_FULL
                          : bat_pct >= 55 ? LV_SYMBOL_BATTERY_3
                          : bat_pct >= 30 ? LV_SYMBOL_BATTERY_2
                          : bat_pct >= 12 ? LV_SYMBOL_BATTERY_1
                                          : LV_SYMBOL_BATTERY_EMPTY;
        char buf[24];
        snprintf(buf, sizeof(buf), "%s %d%%", icono, bat_pct);
        lv_label_set_text(s_status_bat, buf);
        const uint32_t color = charging     ? 0x22c55e
                             : bat_pct < 12 ? 0xef4444
                             : bat_pct < 30 ? 0xf97316
                                            : 0x94a3b8;
        lv_obj_set_style_text_color(s_status_bat, col(color), 0);
    }
}

void ui_wake(void)
{
    s_idle_s = 0;
    if (s_dimmed) {
        s_dimmed = false;
        if (s_set_brightness) s_set_brightness(s_cfg.brightness);
    }
    // un toque tambien pausa la rotacion automatica durante un ciclo completo
    s_dwell_s = 0;
}

void ui_tick_1s(void)
{
    // Atenuado por inactividad (en AMOLED bajar el brillo ahorra de verdad).
    if (s_cfg.screen_timeout_s) {
        s_idle_s++;
        if (!s_dimmed && s_idle_s >= s_cfg.screen_timeout_s) {
            s_dimmed = true;
            if (s_set_brightness) s_set_brightness(s_cfg.night_brightness);
        }
    }

    if (s_cfg.page_dwell_s && s_cols > 1) {
        if (++s_dwell_s >= s_cfg.page_dwell_s) {
            s_dwell_s = 0;
            s_cur_col = (s_cur_col + 1) % s_cols;
            lv_tileview_set_tile_by_index(s_tv, s_cur_col, 0, LV_ANIM_ON);
            update_dots();
        }
    }
}
