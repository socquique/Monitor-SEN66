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
// quede el USB-C al montar la carcasa.
#define BOARD_LCD_ROTATION     90

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
// !! PENDIENTE DE CONFIRMAR CONTRA LA SERIGRAFIA DEL HEADER !!
// El header de 8 pines expone 3 GPIOs + 1 UART (TX 43 / RX 44) + 3V3 + GND.
// Estos dos valores son la apuesta por defecto; al conectar el sensor,
// si `sen66` no aparece, el firmware barre automaticamente los pares
// candidatos y dice por consola cual funciona (ver sen66_autodetect()).
#define BOARD_SEN66_I2C_PORT   1
#define BOARD_SEN66_PIN_SDA    17
#define BOARD_SEN66_PIN_SCL    18

// Pares de pines que prueba el autodetector, en orden. Solo GPIOs que esta
// placa no usa para nada (evitando 33..37, ocupados por la PSRAM octal).
#define BOARD_SEN66_CANDIDATES { {17, 18}, {13, 21}, {43, 44}, {47, 48}, {21, 13} }

// ------------------------------------------------------------------- audio
// I2S ES8311 (sin usar en v1, aqui por referencia)
#define BOARD_I2S_PIN_MCLK     42
#define BOARD_I2S_PIN_BCLK     9
#define BOARD_I2S_PIN_LRCLK    45
#define BOARD_I2S_PIN_DOUT     8
#define BOARD_I2S_PIN_DIN      10
#define BOARD_I2S_PIN_PA_EN    46
