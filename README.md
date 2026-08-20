# Monitor SEN66

Monitor de calidad del aire para la **Waveshare ESP32-S3-Touch-AMOLED-1.75**
(466×466 redonda) con un **Sensirion SEN66**, pantalla autónoma y
autodescubrimiento en **Home Assistant** por MQTT.

Inspirado en el [PowerDot Air](https://makerworld.com/es/models/3029930-powerdot-air-home-assistant-air-sensor)
de Scoolt96, que hace lo mismo sobre la Waveshare 1.46" LCD. Su
[firmware es cerrado](https://github.com/Scoolt96/PowerDot-fw) (el repo solo
publica los `.bin`), así que esto es una implementación nueva desde cero,
adaptada a la AMOLED redonda: driver CO5300 por QSPI, táctil CST9217, RTC
para el histórico y un simulador de escritorio para iterar la pantalla.

## Qué mide

El SEN66 da nueve magnitudes en un solo módulo: **CO₂**, **PM1.0 / PM2.5 /
PM4.0 / PM10**, **índice VOC**, **índice NOx**, **temperatura** y
**humedad**.

## Piezas

| Pieza | Notas |
|---|---|
| Waveshare ESP32-S3-Touch-AMOLED-1.75 | SKU 31261 (las variantes -B y -G también sirven) |
| Sensirion SEN66 | 3,3 V ±5 %, I2C, viene con cable JST GH de 6 hilos |
| 4 cables al header de 8 pines | 3V3, GND, SDA, SCL |
| Cable USB-C | alimentación y flasheo |

## Cableado — leer antes de conectar

**El SEN66 responde en la dirección I2C `0x6B`, que es exactamente la misma
que el IMU QMI8658 que la placa lleva soldado.** No pueden compartir bus. Por
eso el sensor va en un **segundo bus I2C** (puerto 1) sobre GPIOs del header
de expansión, a 100 kHz (el máximo que admite el SEN66), mientras el bus de a
bordo (táctil, PMU, RTC, IMU, audio) sigue a 400 kHz. Ventaja de propina: el
sensor no compite con el táctil.

**El esquema completo está en [docs/CABLEADO.md](docs/CABLEADO.md).** Resumen:

Se cablea por la **etiqueta serigrafiada** del header, no por numero de pin:

| SEN66 | Cable | → | Etiqueta en la placa |
|---|---|---|---|
| 1 VDD | rojo | → | **3V3** (¡no VBUS, que son 5 V!) |
| 2 GND | negro | → | **GND** |
| 3 SDA | verde | → | **IO17** |
| 4 SCL | amarillo | → | **IO18** |

Los cables azul y violeta (pines 5 y 6 del sensor) no se conectan. Ojo: el
orden real del header es `IO18 IO17 IO16 RXD TXD 3V3 GND VBUS` y **no** coincide
con la numeración 1..8 de la [referencia de hardware oficial](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75/blob/main/HARDWARE_REFERENCE.md).

Si se cablea de otra forma, no hace falta tocar nada a ciegas: al arrancar,
si el sensor no contesta en esos pines, el firmware **barre los pares
candidatos** del header y avisa por consola de cuál funciona:

```
W (1234) sen66: no responde en SDA=17 SCL=18, barriendo header
I (1500) sen66: producto: 'SEN66'
W (1500) sen66: encontrado en SDA=16 SCL=17 (no en los de board.h); fija esos valores en BOARD_SEN66_PIN_*
```

Se anota el par bueno en `board.h`, se recompila y listo. La comprobación no
se queda en el ACK de la dirección: lee el nombre de producto y exige que
empiece por `SEN6`, para no confundirse con otro chip.

El SEN66 tiene ventilador y **Sensirion avisa de picos de hasta 350 mA**.
Waveshare no especifica cuánta corriente da el 3V3 del header; en la práctica
el buck del AXP2101 va sobrado, pero conviene alimentar la placa con un
cargador USB-C de 1 A o más y no desde un puerto de hub. Si aparecen
reinicios por brownout, errores de CRC o `fan_error`, la causa es ésa y la
solución es una fuente de 3,3 V aparte para el sensor con GND común. Con
batería la autonomía será de horas, no de días.

## Compilar y flashear

Necesita **ESP-IDF 5.3 o superior** (probado con 5.5.2).

```bash
source ~/esp/esp-idf/export.sh
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/cu.usbmodem1101 flash monitor
```

El gestor de componentes descarga solo LVGL 9, `esp_lvgl_port`,
`esp_lcd_co5300` y `esp_lcd_touch_cst9217`.

La tabla de particiones tiene **dos slots de app de 4 MB**, así que las
actualizaciones OTA desde el navegador funcionan desde el primer flasheo.

## Primer arranque

1. Sin red configurada, el aparato abre un punto de acceso abierto llamado
   **`SEN66-XXXXXX`** (la pantalla lo indica).
2. Conectarse y abrir **http://192.168.4.1**.
3. Rellenar WiFi y broker MQTT (`mqtt://192.168.1.10:1883`), guardar. El
   aparato se reinicia y se conecta.
4. A partir de ahí el panel web está en la IP que muestre la pantalla.

Si la contraseña del WiFi cambia y falla 8 veces seguidas, el portal se
vuelve a abrir solo sin perder la configuración: no hay que reflashear.

## Home Assistant

Con el broker configurado no hay que tocar YAML. Al conectar, el firmware
publica mensajes de descubrimiento retenidos en
`homeassistant/sensor/sen66-xxxxxx/<clave>/config` y HA crea **un dispositivo
con 11 entidades**: las nueve del sensor, una de texto con el nivel global
("BUENO"…"MUY MALO") y la cobertura WiFi como diagnóstico.

- Estado: `sen66-xxxxxx/state`, un JSON cada 10 s.
- Disponibilidad: `sen66-xxxxxx/status` con *last will*, así que si el
  aparato se cae HA lo marca como no disponible en vez de dejar valores
  congelados.

Las clases de dispositivo van puestas (`carbon_dioxide`, `pm25`, `pm10`,
`temperature`, `humidity`…) para que las gráficas y las unidades salgan bien.
Los índices VOC/NOx no tienen clase porque en HA no existe: van con icono.

## La pantalla

Cinco páginas, **se pasan arrastrando el dedo y no rotan solas**. Al no tocar
nada durante `screen_timeout_s` la pantalla se atenúa y muestra una **vista de
reposo con las nueve magnitudes** a la vez; al tocar vuelve exactamente a la
página donde estabas.

| Página | Contenido |
|---|---|
| Resumen | Anillo semáforo con el peor de los contaminantes y qué métrica manda |
| CO₂ | Valor grande, anillo 400–2400 ppm y gráfica de la última hora |
| Partículas | PM2.5 en grande con anillo; PM1.0 / PM4.0 / PM10 debajo |
| Gases | Índices VOC y NOx en dos anillos |
| Clima | Temperatura y humedad, y ambas curvas superpuestas con escala propia |

En AMOLED el negro es píxel apagado, así que el fondo negro no consume y el
atenuado por inactividad ahorra de verdad.

### Umbrales

Cinco niveles: **BUENO / ACEPTABLE / REGULAR / MALO / MUY MALO**.

| Métrica | Fronteras |
|---|---|
| CO₂ (ppm) | 800 · 1000 · 1400 · 2000 |
| PM1.0 y PM2.5 (µg/m³) | 10 · 20 · 25 · 50 |
| PM4.0 y PM10 (µg/m³) | 20 · 40 · 50 · 100 |
| Índice VOC | 150 · 250 · 400 · 450 |
| Índice NOx | 20 · 150 · 300 · 400 |

Temperatura y humedad no son contaminación: se clasifican por distancia al
rango de confort (19–25 °C, 40–60 %RH) y **no entran en el semáforo global**.

Los umbrales están en un único sitio, la tabla `k_bands` de
[`main/air.c`](main/air.c).

## Simulador de escritorio

Compila **la misma UI y la misma lógica** que el firmware contra SDL2, con
datos sintéticos (el CO₂ sube, alguien ventila cada 15 min, de vez en cuando
se cocina). Iterar la pantalla aquí es mucho más rápido que flashear.

```bash
brew install sdl2                      # una vez
idf.py build                           # una vez, para que baje LVGL
cd sim && cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
./build/sen66_sim
```

Opciones útiles:

```bash
./build/sen66_sim --scenario bad        # aire malo
./build/sen66_sim --warp 60             # el tiempo corre 60x
./build/sen66_sim --offset 2400         # adelanta la fase del ciclo
./build/sen66_sim --shots /tmp/caps 5   # 5 capturas BMP y sale
```

## Ajustes (panel web)

Red y MQTT, nombre en HA, zona horaria y NTP, brillo normal y atenuado,
tiempo hasta atenuar, segundos por página, páginas visibles (máscara de
bits), ventana de las gráficas, corrección de temperatura, altitud y
autocalibración del CO₂.

Al guardar, **el aparato se reinicia**: es la forma limpia de aplicar de una
vez la red, el MQTT, las páginas y los ajustes del propio sensor, sin quedarse
a medias. El brillo sí se aplica al instante.

También hay botones para **limpiar el ventilador** del sensor (10 s, conviene
una vez al mes) y para **subir un `.bin`** y actualizar por OTA.

## Puesta a punto del sensor

- **Los índices VOC y NOx necesitan tiempo.** Son índices adaptativos: el
  sensor aprende el ambiente habitual y lo sitúa en 100 (VOC) y 1 (NOx). Las
  primeras horas los valores no significan gran cosa.
- **La temperatura leerá alto.** El sensor está dentro de una carcasa junto a
  electrónica que calienta. Se compara con un termómetro de referencia y se
  mete la diferencia en "Corrección de temperatura": el offset se programa
  **dentro del SEN66**, así que corrige también la humedad relativa.
- **Altitud**: afecta a la medida de CO₂. Poner los metros del sitio.
- **Autocalibración del CO₂ (ASC)**: activada por defecto. Asume que el
  aparato ve aire fresco (~400 ppm) de forma regular. En una habitación que
  nunca se ventila conviene desactivarla y hacer una recalibración forzada al
  aire libre.

## Estructura

```
main/
  app_main.c      orden de arranque, tareas y reloj
  board.h         pinout verificado de la placa (no re-derivar)
  display.c       CO5300 por QSPI + CST9217 + port de LVGL
  sen66.c         driver I2C del sensor (bus propio, CRC-8, autodetección)
  air.c           lógica pura: métricas, umbrales, niveles, colores
  history.c       histórico circular de 24 h en PSRAM
  ui.c            las cinco páginas (solo LVGL, sin ESP-IDF)
  net.c           WiFi estación + portal + SNTP
  ha_mqtt.c       autodescubrimiento y publicación
  webcfg.c        servidor web, API JSON y OTA
  settings.c      persistencia en NVS
  rtc_pcf85063.c  RTC (hora real sin red)
sim/              simulador SDL2 que reutiliza air.c, history.c y ui.c
```

`air.c`, `history.c` y `ui.c` no incluyen nada de ESP-IDF: es lo que permite
compilarlos igual en el PC.

## Estado

Compila limpio (ESP-IDF 5.5.2, 1,5 MB, sin warnings) y la UI está verificada
en el simulador página a página. **Todavía no se ha probado contra hardware
real** — el SEN66 está de camino. Al montarlo queda por ajustar la rotación
de pantalla `BOARD_LCD_ROTATION` según cómo quede el USB-C en la carcasa.
