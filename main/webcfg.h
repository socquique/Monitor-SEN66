// Servidor web local: panel de estado, configuracion y actualizacion OTA.
// Todo local, sin nube. Escucha en el puerto 80 tanto en la red de casa como
// en el punto de acceso de configuracion (192.168.4.1).
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "air.h"

// Callback con el que el servidor pide la ultima muestra (el acceso al estado
// compartido lo protege quien lo implementa).
typedef void (*webcfg_sample_fn)(air_sample_t *out);

esp_err_t webcfg_start(webcfg_sample_fn get_sample);

// Recalibracion forzada de CO2. El panel solo la PIDE; quien la ejecuta es la
// tarea del sensor, porque el comando exige la medicion PARADA y hacer el
// stop/start desde el hilo del servidor se pisaria con la lectura de cada
// segundo. `request` devuelve false si no se puede aceptar ahora mismo, y
// `status` da el texto del ultimo intento para enseñarlo en el panel.
typedef bool (*webcfg_recal_fn)(uint16_t target_ppm);
typedef void (*webcfg_recal_status_fn)(char *out, size_t len);

void webcfg_set_co2_recal(webcfg_recal_fn request, webcfg_recal_status_fn status);
