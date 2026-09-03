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
- **Esa banda inferior se come el texto que caiga dentro.** Con 90 px de alto
  centrados en +100, la gráfica ocupa de +55 a +145. Gases es la única página
  con gráfica que además lleva texto ahí (el aviso "100 = ambiente habitual",
  a +118) y la curva lo tachaba de lado a lado. Solución: fondo opaco del
  color de la pantalla detrás del texto, que es lo que ya hace el anillo en
  CO2. Antes de meter una gráfica en una página, mirar qué vive entre +55
  y +145.
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

### Publicar en el instalador web

`flash.html` de GitHub Pages es lo que instala a la gente que no compila. **No
hay CI**: el repo no tiene `.github/workflows`, así que nada se publica solo.
Todo vive en la rama `gh-pages` y se sube a mano:

1. Merge a `main` y **subir `APP_VERSION` en `main/version.h`**. El instalador
   sirve la versión que diga `firmware/manifest.json`; si no se toca, la web
   ofrece bytes distintos bajo el mismo número.
2. **El binario del instalador NO es el de la OTA.** El manifest declara una
   sola parte en `"offset": 0`, o sea imagen fusionada (bootloader + tabla de
   particiones + app). `build/monitor_sen66.bin` es solo la app: publicado a
   offset 0 da un aparato que no arranca. Hace falta `idf.py merge-bin`.
3. Commit en `gh-pages`: el `.bin` nuevo en `firmware/` y el `manifest.json`
   con `version` y `path` actualizados.
4. **Las capturas de `img/` también caducan.** Son las páginas de la UI, y
   `index.html` dice a mano cuántas hay ("Cinco páginas", "nueve magnitudes").
   Se regeneran con el simulador, que las da al mismo tamaño que el panel:

   ```bash
   ./build/sen66_sim --page N --warp 1 --offset 900 --scenario good --shots DIR 1
   ```

   Sin `--page` y con 4 capturas, la tercera cae ya en la vista de reposo.

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

1. Fotos del cableado en la ficha de MakerWorld. El texto ya está pegado
   (22-08-2026); falta una foto del header cableado, que quita la duda que el
   texto no quita del todo.

## Hecho, por si se busca

- **Sonometro (1.4.0)**. Los micros van a un **ES7210 en 0x40**, no al ES8311.
  Driver a mano (el oficial usa la API vieja de I2C). Tres trampas, una OTA
  cada una: sondear con un registro de ID inventado da NACK (usar
  `i2c_master_probe`); el canal I2S va habilitado ANTES de configurar el
  codec; y sobre todo **en full-duplex los relojes los genera el canal de
  TRANSMISION**, que `sound.c` solo encendia mientras sonaba un aviso — sin
  MCLK el ES7210 no entrega nada. Ahora queda habilitado siempre y el
  amplificador se sigue apagando entre avisos.
- **El ruido del ventilador del SEN66 NO es medible** por encima del ruido de
  la sala. Primera medida: +5,2 dB. Repetida en silencio: −5,6 dB, o sea
  imposible. Lo que vale para un suelo es el minimo, y ahi sale −69,3 dBFS con
  ventilador contra −67,0 sin el. **Corregido: la primera cifra era ruido de
  la sala, no del ventilador.** Enesima version de la misma leccion: una
  diferencia entre dos ventanas cortas no es una medida.

- **El flujo de aire de la carcasa AirRing está verificado** (22-08-2026), no
  supuesto. El SEN66 tiene dos entradas (hueco cuadrado y membrana) y una
  salida (el ventilador), y Sensirion pide separarlas entre sí y aislarlas del
  interior del aparato. Ambas comprobadas midiendo: +0,4 °C contra termómetro
  independiente descarta que aspire de dentro de la caja, y una prueba de
  incienso da una constante de bajada de PM2.5 de **9,7 min contra 8,7** del
  sensor de referencia, lo que descarta que reinspire su propia salida. Datos
  en la DietPi, `/root/prueba-humo-20260822.csv`.

- **No se aplica NINGÚN offset, ni de temperatura ni de CO₂, y es una decisión
  medida** (22-08-2026): 22 h de registro contra un Qingping Air Monitor 2 más
  un termómetro independiente, los tres en la misma mesa. El razonamiento
  entero está en el README, en "Sobre medir el desvío", y los datos crudos en
  la DietPi, `/root/comparacion-20260821.csv`. En corto:
  - Contra la referencia el SEN66 está a **+0,4 °C**, dentro de su propia
    tolerancia (±0,5 °C). El desvío de humedad que veíamos era **del Qingping**
    (+8,9 %), no del SEN66.
  - La diferencia de temperatura **no es constante**: va de +0,20 a +1,30 °C
    según la hora. Un offset fijo acierta a una hora y falla a otra.
  - En CO₂ la discrepancia es de **pendiente, no de cero**:
    `SEN66 = 1,20 × Qingping − 138`, cruzándose en 675 ppm. Por debajo el SEN66
    marca menos y por encima más.
  - La ASC del CO₂ **está bien**: los 403 ppm de una tarde parecían un cero
    anclado, pero la curva nocturna (403 → 831 al amanecer) demuestra que la
    tarde estaba ventilada de verdad.
- **Tres lecciones de método, todas pagadas en esta sesión**: comparar dos
  aparatos da la diferencia y nunca quién acierta; hay que medir un ciclo de
  24 h porque una diferencia "constante" suele serlo solo de día; y hay que
  comprobar a varias concentraciones, porque un tramo suelto convierte una
  diferencia de pendiente en un falso error de cero.

- **Alarma de CO₂ y recalibración forzada** salieron al panel web en la 1.2.0.
  La recalibración no se ejecuta en el hilo del servidor: el comando exige la
  medición parada, así que el panel la encola y la corre `sensor_task`. Dos
  detalles de la documentación de Sensirion que no se deducen del código: la
  corrección devuelta lleva un **desplazamiento de 0x8000**, y hay que esperar
  **600 ms entre parar y recalibrar**.
- **El camino de la recalibración no está probado contra hardware**: dispararla
  escribe en la EEPROM del sensor y solo tiene sentido al aire libre a 420 ppm.
  Validación, encolado y rechazo de referencias imposibles sí están probados.
- **Nunca editar los accesorios de mqttthing desde el formulario de Homebridge**:
  borra el bloque `topics` entero, porque no sabe representar las funciones
  `apply`. Pasó, y dejó los cuatro accesorios sin leer nada. Ver `docs/HOMEBRIDGE.md`.
- **El nivel que va por MQTT es un token en inglés** (`good`…`bad`), no la
  cadena traducida. Al cambiarlo se rompió el mapeo de Homebridge y el README
  se quedó diciendo lo viejo.
