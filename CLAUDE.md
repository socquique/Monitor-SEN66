# Monitor SEN66 — notas para trabajar en este repo

Monitor de calidad del aire: Waveshare ESP32-S3-Touch-AMOLED-1.75 (466×466
redonda, CO5300 + CST9217) + Sensirion SEN66, con MQTT autodiscovery para
Home Assistant. ESP-IDF 5.5 + LVGL 9. Ver [README.md](README.md) para el uso.

Existe la skill `esp32-desk-gadget` con la chuleta de hardware de esta placa
y las lecciones de rendimiento de LVGL: **consultarla antes de tocar pantalla,
táctil, PMU, RTC o audio**, y añadirle lo que se aprenda.

## Invariantes que no hay que romper

- **`main/board.h` es la fuente de verdad del pinout** y está verificado en
  cuatro proyectos. No re-derivar pines ni "corregirlos" a ojo. La única
  excepción marcada son `BOARD_SEN66_PIN_SDA/SCL`, que dependen de la
  serigrafía del header y las autodetecta `sen66_init()`.
- **El SEN66 va en el bus I2C 1, nunca en el 0.** Comparte dirección `0x6B`
  con el IMU QMI8658 de la placa. Y a 100 kHz: es su máximo.
- **`air.c`, `history.c` y `ui.c` no pueden incluir nada de ESP-IDF.** Es lo
  que permite que el simulador compile la misma UI y la misma lógica. Si algo
  de la UI necesita la plataforma (brillo, hora), entra como callback o como
  parámetro, no con un `#include "esp_*.h"`.
- **La pantalla se inicializa ANTES de levantar WiFi** (`app_main`). Los
  buffers con DMA necesitan RAM interna contigua y TLS también; si WiFi va
  primero, se la queda él.
- **Solo ASCII en los textos de la UI.** Las fuentes Montserrat de LVGL no
  llevan acentos, ni `¿`, ni el símbolo micro. Por eso en pantalla se lee
  "Particulas" y "ug/m3" mientras que en MQTT sí van "µg/m³" y "°C"
  (`air_metric_unit_ui` frente a `air_metric_unit_ha`).
- **Nada de escribir en NVS desde el bucle de la UI**: congela ~1 s los dos
  núcleos. `settings_save()` solo se llama desde el servidor web.

## Cosas del panel redondo que ya se han pagado

- Las ventanas de volcado tienen que estar **alineadas a 2 px** o el AMOLED
  pinta líneas verdes en los bordes (`round_area_cb` en `display.c`).
- Al arrancar hay que **barrer los 480×480 de GRAM a negro**: la memoria del
  panel es mayor que los 466 visibles y `set_gap` no cubre todos los casos.
- **Radio útil**: el anillo grande del resumen es de 322 px de diámetro
  (`RING_D`) y no de 378 a propósito. Con 378 el arco pasa justo por donde va
  la línea de estado y el texto verde quedaba invisible sobre el arco verde.
  Cualquier elemento que se ponga a menos de ~70 px del borde superior va a
  chocar con la chrome.
- Las gráficas dentro de un anillo tienen que caber en el círculo: a
  y = +115 del centro el ancho libre dentro de `RING_D` es ~200 px, no 240.
- `lv_chart` trabaja con enteros. Para métricas con decimales (temperatura,
  PM) hay que **escalar ×10** antes de meterlas o la curva sale escalonada
  (`chart_refresh`).

## Flujo de trabajo

Iterar la UI en el simulador, no flasheando:

```bash
cd sim && cmake --build build -j && ./build/sen66_sim --scenario bad
./build/sen66_sim --shots /tmp/caps 5   # capturas para revisar sin ojos
```

El simulador necesita que `idf.py build` se haya ejecutado antes una vez, para
que el gestor de componentes deje LVGL en `managed_components/`.

Firmware:

```bash
source ~/esp/esp-idf/export.sh && idf.py build
idf.py -p /dev/cu.usbmodem1101 flash monitor
```

## Pendiente al recibir el hardware

1. Confirmar los GPIOs del header con la salida de `sen66_init()` y fijarlos
   en `board.h`.
2. Ajustar `BOARD_LCD_ROTATION` según cómo quede el USB-C en la carcasa
   (recordar: el táctil lleva la transformación **inversa** a la del panel;
   los dos `#if` de `display.c` ya están emparejados, cambiar solo la macro).
3. Medir el offset real de temperatura contra un termómetro y meterlo en el
   panel web.
4. Revisar el mayor bloque DMA interno libre que informa `display.c` al
   arrancar; si baja de ~180 KB, el buffer de 185 filas no cabrá y habrá que
   reducirlo.
