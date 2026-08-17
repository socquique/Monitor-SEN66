# Cableado del SEN66

Cuatro cables. Nada más: el SEN66 no tiene pin de selección de interfaz (eso
era el SEN5x), habla I2C directamente.

```
   Waveshare ESP32-S3-Touch-AMOLED-1.75              Sensirion SEN66
   header H2 (2.54 mm, 8 pines)                      conector de 6 pines
   ┌───────────────────────────────┐                 ┌───────────────────┐
   │ 1  VBUS  (5V del USB)         │                 │ 1  VDD    rojo    │
   │ 2  GND  ──────────────────────┼── negro ────────┼──────────► 2 GND  │
   │ 3  3V3  ──────────────────────┼── rojo  ────────┼──────────► 1 VDD  │
   │ 4  GPIO44 / U0RXD             │                 │ 3  SDA    verde   │
   │ 5  GPIO43 / U0TXD             │                 │ 4  SCL    amarillo│
   │ 6  GPIO17 ────────────────────┼── verde ────────┼──────────► 3 SDA  │
   │ 7  GPIO18 ────────────────────┼── amarillo ─────┼──────────► 4 SCL  │
   │ 8  GPIO16  (libre)            │                 │ 5  NC (= GND)     │
   └───────────────────────────────┘                 │ 6  NC (= VDD)     │
                                                     └───────────────────┘
```

## Tabla

| SEN66 | Color del cable Sensirion | → | Header H2 | GPIO |
|---|---|---|---|---|
| 1 VDD | rojo | → | pin 3 | 3V3 |
| 2 GND | negro | → | pin 2 | GND |
| 3 SDA | verde | → | pin 6 | **GPIO17** |
| 4 SCL | amarillo | → | pin 7 | **GPIO18** |
| 5 NC | — | | *sin conectar* | internamente unido a GND |
| 6 NC | — | | *sin conectar* | internamente unido a VDD |

Los pines 5 y 6 del sensor **no se conectan**: están unidos internamente a
GND y a VDD respectivamente, y puentearlos no aporta nada.

## Avisos que importan

- **A 3V3, nunca a VBUS.** El SEN66 quiere **3,3 V ±5 %** — margen estrecho.
  El pin 1 del header es VBUS (5 V del USB) y está justo al lado del 3V3:
  es el error fácil de cometer. Los GPIO de la placa **no toleran 5 V**.
- **La dirección I2C del SEN66 es `0x6B`, la misma que el IMU QMI8658** de a
  bordo. Por eso el sensor va a GPIO17/18 en un bus I2C aparte (puerto 1) y
  no al bus principal (GPIO14/15): en el mismo bus se pisarían.
- **Ojo con los GPIO que parecen libres y no lo están**: GPIO13 es la señal
  LCD_TE del display y GPIO21 es la interrupción INT2 del IMU. Los únicos
  realmente libres del header son **16, 17 y 18** (más el UART0 en 43/44, si
  no se necesita la consola serie).
- **Cables cortos.** El bus va a 100 kHz con las pull-ups internas del
  ESP32-S3 (~45 kΩ, flojas). Con 10–15 cm de Dupont funciona; si se alargan
  mucho y aparecen errores de CRC, poner pull-ups externas de 4,7 kΩ a 3V3
  en SDA y SCL.
- **Consumo**: el ventilador del sensor pide picos al arrancar. Alimentando
  la placa por USB-C no hay problema; con batería, la autonomía baja a horas.

## Comprobación al arrancar

Si algo no está donde toca, el firmware lo dice por consola
(`idf.py monitor`):

```
I (1500) sen66: producto: 'SEN66'
I (1500) sen66: listo en SDA=17 SCL=18
I (1520) app: SEN66 serie 0123456789ABCDEF, firmware 2.3
```

Si no contesta en GPIO17/18, barre los otros pares del header y avisa de cuál
funciona. Y si no aparece en ninguno, es cableado: revisar 3V3, GND y que los
cables no estén cruzados.

## Fuentes

- [Referencia de hardware oficial de la placa](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75/blob/main/HARDWARE_REFERENCE.md)
  (header H2 y mapa completo de GPIO).
- [Driver oficial del SEN66](https://github.com/Sensirion/raspberry-pi-i2c-sen66)
  (pinout del sensor, colores de cable y tensión de alimentación).
