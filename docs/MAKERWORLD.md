# Ficha para MakerWorld

Texto listo para copiar cuando exista la carcasa. **Los huecos marcados con
`[...]` hay que rellenarlos con datos reales del modelo impreso**: no los
inventes, que la gente imprime lo que pongas.

---

## Título

```
Monitor SEN66 | Calidad del aire para Home Assistant y HomeKit
```

En inglés, si publicas en esa lengua primero:

```
Monitor SEN66 | Air quality monitor for Home Assistant and HomeKit
```

MakerWorld traduce solo, pero la traducción automática parte mejor del inglés
que del español, sobre todo hacia el alemán, que es buena parte de su público.

---

## Descripción (español)

Un medidor de calidad del aire para tener encima de la mesa. Lleva un
**Sensirion SEN66**, que mide nueve cosas a la vez, y una pantalla redonda
AMOLED donde se leen sin acercarse.

Habla con **Home Assistant** y con **HomeKit**, pero no depende de ninguno de
los dos: si se cae la red, la pantalla sigue midiendo. No manda nada fuera de
tu casa: ni nube, ni cuenta, ni telemetría.

**Qué mide**

- **CO₂** en ppm — el dato que dice cuándo abrir la ventana
- **Partículas** PM1.0, PM2.5, PM4.0 y PM10 en µg/m³
- **Índices VOC y NOx** — compuestos volátiles y óxidos de nitrógeno
- **Temperatura y humedad**

**Qué hace**

- Cinco páginas que se pasan con el dedo, y una vista de reposo con las nueve
  magnitudes a la vez. Al dejar de tocarla se apaga sola.
- Aparece **solo** en Home Assistant: se anuncia por MQTT y salen sus once
  entidades sin escribir una línea de YAML.
- Funciona con **HomeKit** a través de Homebridge. Hay guía y configuración
  lista para pegar.
- **Aviso sonoro** cuando el CO₂ pasa del umbral, con histéresis para que no
  pite cada dos por tres.
- Se puede alimentar con **batería**: unas 6 horas medidas con una celda de
  1000 mAh, con perfil de ahorro que se activa solo al desenchufar.
- La pantalla habla **español, inglés y alemán**.
- Se actualiza **por WiFi** desde su propio panel web.

**Código abierto**

El firmware está entero en GitHub y se instala **desde el navegador**: conectas
la placa por USB, pulsas un botón y listo. No hace falta instalar ESP-IDF, ni
Python, ni drivers. (Necesita Chrome, Edge u Opera en un ordenador; Safari y
Firefox no implementan WebSerial.)

- Instalador: https://socquique.github.io/Monitor-SEN66/flash.html
- Código y documentación: https://github.com/socquique/Monitor-SEN66

---

## Piezas necesarias

| Pieza | Notas |
|---|---|
| Waveshare ESP32-S3-Touch-AMOLED-1.75 | SKU 31261. La pantalla ya viene montada |
| Sensirion SEN66 | Trae su cable de 6 hilos, solo se usan 4 |
| 4 cables Dupont hembra-hembra | Para el conector de expansión |
| Cable USB-C **de datos** | Los de solo carga no valen |
| `[tornillos]` | Rellenar según la carcasa |

Opcional: batería LiPo 1S con conector MX1.25 y un altavoz pequeño para el
aviso sonoro.

---

## Impresión

**Rellenar con lo que hayas probado de verdad**, no con lo que suele
recomendarse:

- Material: `[...]`
- Altura de capa: `[...]`
- Relleno: `[...]`
- Soportes: `[...]`
- Tiempo aproximado: `[...]`

> Si la carcasa se diseña para la contracción de un material concreto, decirlo
> bien claro: imprimirla en otro hará que no encaje.

---

## Montaje

1. Imprime las piezas.
2. **Conecta el sensor a la placa** por el conector de expansión, siguiendo la
   etiqueta serigrafiada y no el número de pin:

   | Cable del SEN66 | Va a |
   |---|---|
   | rojo (VDD) | **3V3** |
   | negro (GND) | **GND** |
   | verde (SDA) | **IO17** |
   | amarillo (SCL) | **IO18** |

   Los cables azul y violeta no se conectan.

3. Instala el firmware desde el navegador (enlace arriba).
4. Al arrancar abre un punto de acceso llamado `SEN66-XXXXXX`. Conéctate,
   entra en `192.168.4.1` y pon tu WiFi y tu broker MQTT.

**Cuidado con dos cosas:**

- El SEN66 funciona a **3,3 V** y en ese mismo conector, a dos pines de
  distancia, hay 5 V. Equivocarse se carga el sensor.
- El sensor **comparte dirección I2C** con el acelerómetro que la placa lleva
  soldado. Por eso va en los pines IO17/IO18 y no en el bus principal.

Esquema completo con dibujo:
https://github.com/socquique/Monitor-SEN66/blob/main/docs/CABLEADO.md

---

## Licencia sugerida para el modelo

**CC BY-NC-SA**: se puede usar, modificar y compartir, no vender, y las obras
derivadas mantienen la misma licencia. Es coherente con el firmware, que va
bajo PolyForm Noncommercial.

Ojo con la licencia exclusiva de MakerWorld: prohíbe publicar el modelo o sus
derivados en otras plataformas. Si quieres que tu carcasa se pueda subir
también a Printables o Thingiverse, no la elijas.

---

## Créditos que conviene poner

La idea vino del [PowerDot Air](https://makerworld.com/es/models/3029930-powerdot-air-home-assistant-air-sensor)
de Scoolt96, que hace algo parecido con la Waveshare de 1,46" y pantalla LCD.
Su firmware es cerrado; éste está escrito desde cero para otra pantalla.

Decirlo tú primero es lo honesto, y además evita que alguien lo plantee en los
comentarios como si lo hubieras copiado.

---

## Al diseñar la carcasa

Tres cosas que condicionan el diseño y es mejor saber antes de empezar:

1. **El SEN66 necesita que le entre y le salga aire.** Tiene un ventilador que
   aspira por un lado y expulsa por otro. Si las dos aberturas quedan juntas
   volverá a medir su propio aire y las lecturas serán planas y falsas.
2. **Separa el sensor del calor de la placa.** La pantalla y el ESP32 calientan,
   y eso sube la temperatura medida. En este montaje son ya **+1,2 °C** con los
   cables al aire; encerrarlo todo junto lo empeora.
3. **Deja llegar al USB-C y al botón lateral**, y prevé el hueco de la batería
   y el altavoz si los vas a poner.

---

## Descripción (English)

An air quality meter to sit on your desk. It uses a **Sensirion SEN66**, which
measures nine things at once, and a round AMOLED display you can read from
across the room.

It talks to **Home Assistant** and **HomeKit**, but depends on neither: if the
network goes down, the display keeps measuring. Nothing leaves your house — no
cloud, no account, no telemetry.

**What it measures**

- **CO₂** in ppm — the number that tells you to open a window
- **Particulates** PM1.0, PM2.5, PM4.0 and PM10 in µg/m³
- **VOC and NOx indices**
- **Temperature and humidity**

**What it does**

- Five pages you swipe through, plus an idle view showing all nine readings at
  once. It turns itself off when you stop touching it.
- **Shows up by itself** in Home Assistant: it announces over MQTT and eleven
  entities appear without writing a line of YAML.
- Works with **HomeKit** through Homebridge. Guide and ready-made config
  included.
- **Beeps** when CO₂ crosses your threshold, with hysteresis so it doesn't nag.
- Runs on **battery**: about 6 hours measured with a 1000 mAh cell, with a
  power-saving profile that kicks in when you unplug it.
- The display speaks **Spanish, English and German**.
- Updates **over WiFi** from its own web panel.

**Open source**

The firmware is all on GitHub and installs **straight from your browser**: plug
the board in over USB, press a button, done. No ESP-IDF, no Python, no drivers.
(Needs Chrome, Edge or Opera on a computer; Safari and Firefox don't implement
WebSerial.)

- Installer: https://socquique.github.io/Monitor-SEN66/flash.html
- Source and docs: https://github.com/socquique/Monitor-SEN66
