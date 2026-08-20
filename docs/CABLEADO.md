# Cableado del SEN66

Cuatro cables. El sensor viene con el juego JST GH de **seis** conductores,
pero dos no se usan: el SEN66 no tiene pin de seleccion de interfaz (eso era
el SEN5x) y habla I2C directamente.

```
 Header de la placa, VISTO POR DETRAS con el USB-C a la derecha.
 Cada pin lleva su nombre serigrafiado al lado: se cablea por ETIQUETA,
 no contando pines.

  ┌──────┬──────┬──────┬─────┬─────┬─────┬─────┬──────┐
  │ IO18 │ IO17 │ IO16 │ RXD │ TXD │ 3V3 │ GND │ VBUS │
  └──┬───┴──┬───┴──────┴─────┴─────┴──┬──┴──┬──┴──────┘
     │      │                         │     │        ^^^^
  amarillo verde                    rojo  negro     no tocar
     SCL    SDA                      VDD    GND       (5 V)
      │      │                         │     │
      └──────┴────────► SEN66 ◄────────┴─────┘

 Conector del SEN66 (JST GH de 6), cable saliendo hacia arriba:
 el pin 1 es el extremo del cable ROJO.

  1 rojo VDD · 2 negro GND · 3 verde SDA · 4 amarillo SCL
  5 azul NC  · 6 violeta NC          (5 y 6 sin conectar)
```

## Tabla

| Pin SEN66 | Color del cable | → | Etiqueta en la placa |
|---|---|---|---|
| 1 VDD | rojo | → | **3V3** |
| 2 GND | negro | → | **GND** |
| 3 SDA | verde | → | **IO17** |
| 4 SCL | amarillo | → | **IO18** |
| 5 NC | azul | | *sin conectar* (unido por dentro a GND) |
| 6 NC | violeta | | *sin conectar* (unido por dentro a VDD) |

**Como orientar el conector del sensor**: con el cable saliendo hacia arriba,
el pin 1 es el extremo del **cable rojo**, y el orden es rojo, negro, verde,
amarillo, azul, violeta. El azul y el violeta se cortan o se aislan con
termorretractil: estan unidos internamente a GND y VDD y no aportan nada.

**No te fies de los numeros de pin del header.** La serigrafia de la placa
lee `IO18 IO17 IO16 RXD TXD 3V3 GND VBUS` de izquierda a derecha (por
detras, USB-C a la derecha), que NO es el orden que da la numeracion 1..8 de
la `HARDWARE_REFERENCE.md` oficial. Manda lo que esta impreso en el cobre.

## Avisos que importan

- **A 3V3, nunca a VBUS.** El SEN66 quiere **3,3 V ±5 %** — margen estrecho.
  El VBUS (5 V del USB) es el pin del **extremo derecho**, pegado al GND y a
  dos posiciones del 3V3: es el error facil de cometer. Los GPIO de la placa
  **no toleran 5 V**.
- **Corriente: picos de hasta 350 mA.** Es el aviso de Sensirion en su propio
  driver, y la documentacion de Waveshare no especifica cuanta corriente
  puede dar el 3V3 del header. En la practica el buck de 3,3 V del AXP2101
  va sobrado, pero hay que **alimentar la placa con un cargador USB-C de 1 A
  o mas**, no desde un puerto de hub. Sintomas de que no llega: reinicios por
  brownout, errores de CRC en el I2C o `fan_error` en el estado del sensor.
  Solucion en ese caso: fuente de 3,3 V aparte para el sensor, GND comun.
- **La direccion I2C del SEN66 es `0x6B`, la misma que el IMU QMI8658** de a
  bordo. Por eso el sensor va a GPIO17/18 en un bus I2C aparte (puerto 1) y
  no al bus principal (GPIO14/15): en el mismo bus se pisarian.
- **Ojo con los GPIO que parecen libres y no lo estan**: GPIO13 es la senal
  LCD_TE del display y GPIO21 es la interrupcion INT2 del IMU. Los unicos
  realmente libres del header son **16, 17 y 18** (mas el UART0 en 43/44, si
  no se necesita la consola serie).
- **Cables cortos.** El bus va a 100 kHz con las pull-ups internas del
  ESP32-S3 (~45 kΩ, flojas). Con 10–15 cm de Dupont funciona; si se alargan
  mucho y aparecen errores de CRC, poner pull-ups externas de 4,7 kΩ a 3V3
  en SDA y SCL.

## Comprobacion al arrancar

Si algo no esta donde toca, el firmware lo dice por consola
(`idf.py monitor`):

```
I (1500) sen66: producto: 'SEN66'
I (1500) sen66: listo en SDA=17 SCL=18
I (1520) app: SEN66 serie 0123456789ABCDEF, firmware 2.3
```

Si no contesta en GPIO17/18, barre los otros pares del header y avisa de cual
funciona. Y si no aparece en ninguno, es cableado: revisar 3V3, GND y que los
cables no esten cruzados.

## Fuentes

- [Referencia de hardware oficial de la placa](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75/blob/main/HARDWARE_REFERENCE.md)
  (header H2 y mapa completo de GPIO).
- [Driver oficial del SEN66](https://github.com/Sensirion/arduino-i2c-sen66)
  (pinout del sensor, colores de cable, tension y el aviso de los 350 mA).
