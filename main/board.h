// Mapa de pines — Waveshare ESP32-S3-Touch-AMOLED-1.75 (SKU 31261)
//
// Los pines de la placa estan VERIFICADOS en cuatro proyectos previos
// (Hamlet, TamaPoke, CapsuleRadar-AIS, PlaneRadar2.0). No re-derivarlos.
// Fuentes: https://devices.esphome.io/devices/waveshare-esp32-s3-touch-amoled-175/
//          https://www.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-1.75
#pragma once

// ---------------------------------------------------------------- pantalla
// AMOLED CO5300, QSPI, 466x466 redonda
#define BOARD_LCD_H_RES        466
#define BOARD_LCD_V_RES        466
#define BOARD_LCD_QSPI_HOST    SPI2_HOST
#define BOARD_LCD_PIN_PCLK     38
#define BOARD_LCD_PIN_DATA0    4
#define BOARD_LCD_PIN_DATA1    5
#define BOARD_LCD_PIN_DATA2    6
#define BOARD_LCD_PIN_DATA3    7
#define BOARD_LCD_PIN_CS       12
#define BOARD_LCD_PIN_RST      39

// Rotacion por hardware (MADCTL): 0, 90, 180 o 270. Ajustar segun donde
// quede el USB-C al montar la carcasa. OJO: la macro gira en sentido
// ANTIHORARIO, asi que para enderezar la imagen hay que restar, no sumar.
#define BOARD_LCD_ROTATION     0

// El tactil NO comparte origen con el panel: en esta placa esta montado a
// 180 grados de el. Se deduce de la propia placa (con el panel a 0 la imagen
// sale derecha pero los gestos van al reves) y encaja con lo que hacia el
// firmware para 90 y 270, que si estaban probados.
//
// Antes los dos bloques #if de display.c estaban escritos a mano y aplicaban
// este desfase solo en 90 y 270, no en 0 ni en 180: por eso al cambiar la
// rotacion se invertian los gestos. Derivarlo evita volver a descuadrarlo.
#define BOARD_TOUCH_ROTATION   (((BOARD_LCD_ROTATION) + 180) % 360)

// ------------------------------------------------------------------ tactil
// CST9217 (I2C 0x5A) — comparte bus con IMU/RTC/PMU/audio
#define BOARD_TOUCH_PIN_INT    11
#define BOARD_TOUCH_PIN_RST    40

// ------------------------------------------------------- bus I2C de a bordo
// CST9217 0x5A · QMI8658 0x6B · PCF85063 0x51 · AXP2101 0x34 · ES8311 0x18
#define BOARD_I2C_PORT         0
#define BOARD_I2C_PIN_SDA      15
#define BOARD_I2C_PIN_SCL      14

// ------------------------------------------------------ bus I2C del SEN66
// OJO: el SEN66 responde en 0x6B, la MISMA direccion que el IMU QMI8658 de
// la placa. No pueden convivir en el bus de a bordo: el SEN66 va en su
// propio bus I2C (puerto 1) sobre GPIOs del header de 8 pines, ademas a
// 100 kHz, que es el maximo que admite el sensor.
//
// Header de expansion de 8 pines. Cablear SIEMPRE por la etiqueta
// serigrafiada, no por numero de pin: la serigrafia de la placa lee, de
// izquierda a derecha vista por detras con el USB-C a la derecha,
//
//   IO18 · IO17 · IO16 · RXD · TXD · 3V3 · GND · VBUS
//
// que NO coincide con el orden 1..8 que publica la HARDWARE_REFERENCE.md
// oficial (alli 1=VBUS ... 6=GPIO17, 7=GPIO18, 8=GPIO16). Manda el cobre.
// Logica de 3.3V y NO tolerante a 5V; el VBUS son 5 V del USB.
#define BOARD_SEN66_I2C_PORT   1
#define BOARD_SEN66_PIN_SDA    17  // etiqueta IO17
#define BOARD_SEN66_PIN_SCL    18  // etiqueta IO18

// Pares que prueba el autodetector si el sensor no contesta en los de
// arriba (por si se cablea distinto). Solo GPIOs realmente libres del
// header: 16, 17, 18 y el UART0 (43/44). OJO con lo que parece libre y no
// lo esta: GPIO13 es LCD_TE y GPIO21 es QMI_INT2.
#define BOARD_SEN66_CANDIDATES { {17, 18}, {16, 17}, {18, 16}, {43, 44}, {16, 18} }

// Senal de tearing-effect del CO5300. Sin usar de momento, pero existe:
// sincronizar el volcado con ella es la via para matar el tearing.
#define BOARD_LCD_PIN_TE       13

// ------------------------------------------------------------------- audio
// I2S ES8311 (sin usar en v1, aqui por referencia)
#define BOARD_I2S_PIN_MCLK     42
#define BOARD_I2S_PIN_BCLK     9
#define BOARD_I2S_PIN_LRCLK    45
#define BOARD_I2S_PIN_DOUT     8
#define BOARD_I2S_PIN_DIN      10
#define BOARD_I2S_PIN_PA_EN    46
