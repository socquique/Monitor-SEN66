// Servidor web local: panel de estado, configuracion y actualizacion OTA.
// Todo local, sin nube. Escucha en el puerto 80 tanto en la red de casa como
// en el punto de acceso de configuracion (192.168.4.1).
#pragma once

#include "esp_err.h"

#include "air.h"

// Callback con el que el servidor pide la ultima muestra (el acceso al estado
// compartido lo protege quien lo implementa).
typedef void (*webcfg_sample_fn)(air_sample_t *out);

esp_err_t webcfg_start(webcfg_sample_fn get_sample);
