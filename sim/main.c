// Simulador de escritorio: ventana SDL de 466x466 con la UI real del
// firmware y datos sinteticos plausibles. Sirve para iterar la pantalla sin
// tener el sensor delante.
//
//   ./sen66_sim                        interactivo (arrastra para cambiar de pagina)
//   ./sen66_sim --warp 60              el tiempo corre 60x (1 min por segundo)
//   ./sen66_sim --scenario bad         aire malo en vez de normal
//   ./sen66_sim --offset 700           adelanta el reloj virtual (fase del ciclo)
//   ./sen66_sim --shots dir 5          vuelca 5 capturas BMP en dir y sale
//
// Con --shots conviene dejar warp a 1: la rotacion de pagina va con el reloj
// virtual, asi que a warp alto las paginas cambian mas rapido que el disparo.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "lvgl.h"
#include <SDL2/SDL.h>

#include "air.h"
#include "history.h"
#include "i18n.h"
#include "ui.h"

#define HOR_RES 466
#define VER_RES 466

enum { SC_GOOD = 0, SC_NORMAL, SC_BAD };

static air_history_t s_hist;

// ------------------------------------------------------------- datos falsos
// Una habitacion con gente: el CO2 sube, alguien abre la ventana cada 15 min,
// y de vez en cuando se cocina (pico de particulas y de VOC).
static void synth(air_sample_t *s, long t, int scenario)
{
    const float bias = (scenario == SC_GOOD) ? 0.45f : (scenario == SC_BAD ? 2.2f : 1.0f);

    const long cycle = t % 900;                // ciclo de ventilacion
    const float occupancy = 1.0f - expf(-cycle / 420.0f);
    float co2 = 430.0f + 900.0f * occupancy * bias;
    co2 += 12.0f * sinf(t / 37.0f);            // respiracion / ruido

    const long cook = t % 3600;                // pico de cocina cada hora
    const float spike = expf(-powf((cook - 2400) / 180.0f, 2.0f));
    float pm25 = (3.0f + 22.0f * spike) * bias + 0.8f * sinf(t / 23.0f);
    if (pm25 < 0) pm25 = 0;

    float voc = 90.0f + 120.0f * spike * bias + 18.0f * sinf(t / 900.0f) * bias;
    float nox = 1.0f + 40.0f * spike * bias;

    s->v[AIR_PM1] = pm25 * 0.72f;
    s->v[AIR_PM25] = pm25;
    s->v[AIR_PM4] = pm25 * 1.12f;
    s->v[AIR_PM10] = pm25 * 1.30f;
    s->v[AIR_HUM] = 46.0f + 9.0f * sinf(t / 2400.0f) + 6.0f * spike;
    s->v[AIR_TEMP] = 22.2f + 1.6f * sinf(t / 3600.0f) + 0.8f * spike;
    s->v[AIR_VOC] = voc;
    // Ruido: suelo de casa mas el trajin de la cocina, con variacion rapida.
    s->v[AIR_NOISE] = 38.0f + 16.0f * spike * bias + 4.0f * sinf(t / 11.0f);
    s->v[AIR_NOX] = nox;
    s->v[AIR_CO2] = co2;
    s->valid = true;
    s->age_s = 0;
}

// --------------------------------------------------------------- capturas
static void save_bmp(const char *path, const lv_draw_buf_t *buf)
{
    // BMP 32bpp top-down; el XRGB8888 de LVGL ya esta como B,G,R,X en memoria
    const uint32_t w = buf->header.w, h = buf->header.h;
    const uint32_t img_size = w * h * 4;
    const uint32_t off = 14 + 40;

    uint8_t hdr[54] = {0};
    hdr[0] = 'B'; hdr[1] = 'M';
    uint32_t file_size = off + img_size;
    memcpy(hdr + 2, &file_size, 4);
    memcpy(hdr + 10, &off, 4);
    uint32_t ih_size = 40;
    memcpy(hdr + 14, &ih_size, 4);
    memcpy(hdr + 18, &w, 4);
    int32_t neg_h = -(int32_t)h;
    memcpy(hdr + 22, &neg_h, 4);
    uint16_t planes = 1, bpp = 32;
    memcpy(hdr + 26, &planes, 2);
    memcpy(hdr + 28, &bpp, 2);
    memcpy(hdr + 34, &img_size, 4);

    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return; }
    fwrite(hdr, 1, sizeof(hdr), f);
    for (uint32_t y = 0; y < h; y++) {
        fwrite(buf->data + y * buf->header.stride, 1, w * 4, f);
    }
    fclose(f);
    printf("captura: %s\n", path);
}

static void take_shot(const char *dir, int idx)
{
    lv_draw_buf_t *snap = lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_XRGB8888);
    if (!snap) { fprintf(stderr, "snapshot fallo\n"); return; }
    char path[512];
    snprintf(path, sizeof(path), "%s/sen66_%d.bmp", dir, idx);
    save_bmp(path, snap);
    lv_draw_buf_destroy(snap);
}

int main(int argc, char **argv)
{
    const char *shots_dir = NULL;
    int shots_count = 0;
    int warp = 1;
    int page = -1;
    int scenario = SC_NORMAL;
    long offset = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--shots") == 0 && i + 2 < argc) {
            shots_dir = argv[++i];
            shots_count = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--page") == 0 && i + 1 < argc) {
            page = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--lang") == 0 && i + 1 < argc) {
            i18n_set_lang(i18n_lang_from_code(argv[++i]));
        } else if (strcmp(argv[i], "--warp") == 0 && i + 1 < argc) {
            warp = atoi(argv[++i]);
            if (warp < 1) warp = 1;
        } else if (strcmp(argv[i], "--offset") == 0 && i + 1 < argc) {
            offset = atol(argv[++i]);
        } else if (strcmp(argv[i], "--scenario") == 0 && i + 1 < argc) {
            const char *v = argv[++i];
            scenario = (strcmp(v, "good") == 0) ? SC_GOOD
                     : (strcmp(v, "bad") == 0)  ? SC_BAD : SC_NORMAL;
        } else {
            fprintf(stderr, "uso: %s [--lang es|en|de] [--page 0-4] [--warp N] [--scenario good|normal|bad]"
                            " [--offset SEG] [--shots dir N]\n", argv[0]);
            return 1;
        }
    }

    lv_init();
    lv_tick_set_cb(SDL_GetTicks);

    lv_display_t *disp = lv_sdl_window_create(HOR_RES, VER_RES);
    lv_sdl_window_set_title(disp, "Monitor SEN66 - sim");
    lv_sdl_mouse_create();

    air_history_init(&s_hist, 60);

    // Precarga 3 h de historico para que las graficas no salgan vacias.
    const long t0 = 3 * 3600 + offset;
    air_sample_t s;
    air_sample_clear(&s);
    for (long t = -t0; t < 0; t += 20) {
        synth(&s, t + t0, scenario);
        air_history_push(&s_hist, &s, (time_t)(t0 + t));
    }

    const ui_config_t cfg = {
        
        .pages_mask = (1 << UI_PAGE_COUNT) - 1,
        .chart_span_min = 60,
        .brightness = 200,
        .night_brightness = 20,
        .screen_timeout_s = (shots_dir && page < 0) ? 6 : 20, // reposo solo si no se pide pagina
    };
    ui_init(&s_hist, &cfg, NULL);
    if (page >= 0) ui_goto_page(page);

    // En modo capturas la pagina cambia cada 3 s: disparamos 1,5 s despues de
    // cada cambio, con la animacion del tileview ya terminada.
    uint32_t next_shot_ms = 1500;
    int shot_idx = 0;
    long last_vtick = -1;

    for (;;) {
        uint32_t sleep_ms = lv_timer_handler();
        if (sleep_ms == LV_NO_TIMER_READY) sleep_ms = 5;
        SDL_Delay(sleep_ms > 20 ? 20 : sleep_ms);

        const uint32_t now = SDL_GetTicks();
        const long vnow_s = t0 + (long)((uint64_t)now * warp / 1000);
        if (vnow_s != last_vtick) {
            last_vtick = vnow_s;
            synth(&s, vnow_s, scenario);
            air_history_push(&s_hist, &s, (time_t)vnow_s);

            char hhmm[8];
            snprintf(hhmm, sizeof(hhmm), "%02ld:%02ld",
                     (vnow_s / 3600) % 24, (vnow_s / 60) % 60);
            ui_update(&s);
            // Bateria simulada. En interactivo va bajando para poder ver los
            // tramos de color; en capturas se fija, o las imagenes de la
            // documentacion salen con la bateria en rojo y parece una averia.
            int bat = shots_dir ? 82 : 100 - (int)((vnow_s / 40) % 105);
            ui_set_ip("192.168.1.42");
            ui_set_status(hhmm, true, true, "", bat < 0 ? 0 : bat, false);
            ui_tick_1s();
        }

        if (shots_dir && now >= next_shot_ms) {
            take_shot(shots_dir, shot_idx++);
            next_shot_ms += 3000;
            if (shot_idx >= shots_count) break;
        }
    }
    return 0;
}
