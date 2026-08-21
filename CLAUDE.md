# Monitor SEN66 — notas para trabajar en este repo

Monitor de calidad del aire: Waveshare ESP32-S3-Touch-AMOLED-1.75 (466×466
redonda, CO5300 + CST9217) + Sensirion SEN66, con MQTT autodiscovery para
Home Assistant. ESP-IDF 5.5 + LVGL 9. Ver [README.md](README.md) para el uso.

Existe la skill `esp32-desk-gadget` con la chuleta de hardware de esta placa
y las lecciones de rendimiento de LVGL: **consultarla antes de tocar pantalla,
táctil, PMU, RTC o audio**, y añadirle lo que se aprenda.

## Invariantes que no hay que romper

- **`main/board.h` es la fuente de verdad del pinout**, verificado en cuatro
  proyectos y contra la [referencia de hardware oficial](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75/blob/main/HARDWARE_REFERENCE.md).
  No re-derivar pines ni "corregirlos" a ojo.
- **Del header solo están libres GPIO16, 17 y 18** (más UART0 en 43/44).
  GPIO13 es `LCD_TE` y GPIO21 es `QMI_INT2`: parecen libres y no lo están.
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
- **Radio útil**: `RING_D` es 380 y la hora y el estado de red van DENTRO
  del anillo (`LV_ALIGN_CENTER, -146` y `-124`). Antes el anillo era de 322
  justamente porque a más diámetro el arco pasaba por donde iba la línea de
  estado y el texto quedaba ilegible sobre el color; meter la chrome dentro
  es lo que desbloquea que el arco llegue casi al borde.
- **El hueco libre dentro del anillo es `RING_INNER_R` = 168**, no 190:
  `RING_D/2` es el borde exterior y hay que restarle los 22 de grosor del
  arco. De ahí salen todas las coordenadas de las páginas.
- **Las gráficas van a sangre y por debajo de todo**: 430 px de ancho (más
  que el propio anillo), creadas ANTES que el resto para que queden en el
  fondo. La línea pasa por detrás de los brazos del arco y la recorta el
  cristal, que es el efecto del PowerDot. Ojo: cruzándola por el centro tapa
  el número — tiene que ir de banda inferior (y≈+100), probado.
- **Los dos anillos no son la misma cosa.** El del resumen es un semáforo que
  se pinta entero y lleva indicador del mismo grosor que la pista; los de
  medida llevan el indicador 8 px más fino, porque a igual grosor un valor
  pequeño dibuja un pegote suelto que parece un fallo de render.
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

## Ya verificado contra el hardware (20-08-2026)

Primer arranque real: pantalla, táctil, RTC y SEN66 (serie 0123456789ABCDEF,
firmware 4.1) funcionando en GPIO17/18. Lo que se aprendió:

- **El buffer de LVGL no puede ser un número fijo de filas.** En esta placa el
  mayor bloque contiguo interno con DMA es de **144 KB**, no los ~192 KB de
  Hamlet, así que las 185 filas heredadas no caben y `lvgl_port_add_disp()`
  falla en bootloop. `display.c` lo calcula ahora en tiempo de ejecución
  reservando 56 KB para el resto del sistema. No volver a fijarlo a mano.
- **Vigilar el heap interno libre que imprime `app_main` al terminar.** Con
  el buffer al máximo quedaban 26 KB, muy justo para WiFi en modo estación +
  MQTT + OTA. Con 96 filas quedan 42 KB. Si hay que recortar, se recorta de
  la pantalla: 96 y 114 filas vuelcan los mismos 5 trozos por frame.
- **La serigrafía del header manda sobre la documentación.** Ver `board.h`.
- **Recuperar el bus I2C antes de abrirlo** (`bus_recover()` en `sen66.c`).
  Un reset del ESP32 a mitad de transacción deja al SEN66 sujetando SDA a
  masa y el sensor sale como "no detectado" hasta el siguiente ciclo de
  alimentación. Pasa en cada `idf.py flash`. Pulsos manuales de SCL + STOP.
- **Un log optimista no es una comprobación.** Dos veces en la misma sesión:
  `net.c` cantaba "portal abierto" con la radio apagada y `webcfg.c`
  anunciaba 192.168.4.1 estando en la red de casa. Loguear lo que se ha
  verificado, no lo que se pretendía hacer.

## Decisiones tomadas

- **El panel web va sin contraseña a propósito.** Red doméstica de confianza,
  y a cambio actualizar por OTA es trivial. Queda anotado en el README para
  que sea una decisión y no un descuido.
- **El ventilador NO se cicla con batería.** Sería el mayor ahorro, pero
  parado no se mide nada (ver el comentario en `app_main.c`). Autonomía
  medida con todo funcionando: ~5 h con una celda de 1000 mAh.

## Pendiente

1. Aplicar el offset de temperatura: contra el Qingping salen **+1,2 °C** de
   autocalentamiento (y la humedad, −8,8 %, es consecuencia de eso). Mejor
   confirmarlo antes con un termómetro independiente.
2. Sacar al panel web los ajustes de la alarma (`alarm_*`), que solo se
   pueden cambiar recompilando.
3. Exponer la recalibración forzada de CO₂. Ahora mismo no hace falta: el
   SEN66 y el Qingping coinciden al ppm.
