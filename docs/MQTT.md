# El broker MQTT

El aparato no habla directamente con Home Assistant ni con HomeKit: publica
por MQTT y son ellos los que escuchan. Asi que **hace falta un broker**, y es
el mismo paso tanto si luego vas a [Home Assistant](../README.md#home-assistant)
como si vas a [Homebridge](HOMEBRIDGE.md). Si ya tienes uno, saltate el punto 1.

## 1. Instalar Mosquitto

### Si Home Assistant corre como HAOS o supervisado

Ajustes > Complementos > Tienda, instalar **Mosquitto broker** y arrancarlo.
Crea el usuario desde Ajustes > Personas > Usuarios y usalo en el punto 3;
el complemento acepta las credenciales de HA. Con eso ya esta: HA detecta su
propio broker y te ofrece configurar la integracion MQTT sola.

### Si es una Debian pelada (DietPi, Raspberry Pi OS, un contenedor)

`dietpi-software` NO lo trae en su catalogo (comprobado en DietPi 10.6): va
por apt.

```bash
apt-get install -y mosquitto mosquitto-clients
```

Mosquitto 2.x de fabrica **solo escucha en localhost y rechaza conexiones
anonimas**. Recien instalado el monitor no conecta y no dice por que, asi que
hay que abrirlo:

```bash
cat > /etc/mosquitto/conf.d/local.conf <<'EOF'
listener 1883 0.0.0.0
allow_anonymous false
password_file /etc/mosquitto/passwd
EOF
mosquitto_passwd -c /etc/mosquitto/passwd monitor
chown mosquitto:mosquitto /etc/mosquitto/passwd
chmod 0600 /etc/mosquitto/passwd
systemctl restart mosquitto
```

Dos piedras con las que se tropieza seguro:

- **No repetir `persistence` ni `persistence_location`** en `conf.d`: ya
  vienen en el `mosquitto.conf` del paquete de Debian y el servicio se niega
  a arrancar con `Duplicate persistence_location value`.
- **El fichero de contrasenas debe ser `mosquitto:mosquitto` y 0600.** Con
  `root:root` el broker sale con estado 13 (permiso denegado). Confunde que
  `mosquitto_passwd`, corriendo como root, avise justo de lo contrario: el
  que manda es el broker, que corre como `mosquitto`.

Comprobar que arranco de verdad, que el `restart` no se queja aunque falle:

```bash
systemctl is-active mosquitto && ss -lntp | grep 1883
```

## 2. Decidir el prefijo de descubrimiento

En el panel del aparato, el campo **prefijo MQTT** decide si se anuncia solo:

| Valor | Efecto |
|---|---|
| `homeassistant` (por defecto) | Publica el autodescubrimiento y HA crea el aparato con sus 11 entidades |
| vacio | No publica descubrimiento. Es lo que quieres **si usas Homebridge** y no HA |

Dejarlo vacio con Homebridge no es cosmetico: si no, quedan once mensajes
retenidos de configuracion en el broker que nadie consume.

## 3. Apuntar el aparato al broker

En su panel web (la IP que muestra la pantalla), en Red y MQTT:

- **URI**: `mqtt://IP-DEL-BROKER:1883`
- **Usuario** y **contrasena**: los del punto 1
- **Prefijo**: segun la tabla de arriba

Al guardar se reinicia y conecta. La pantalla muestra el estado de red dentro
del anillo.

## 4. Comprobar que publica

Desde la maquina del broker, antes de tocar HA o Homebridge. El estado va
retenido, asi que debe llegar **al instante**; si tarda, es que no publica:

```bash
mosquitto_sub -h localhost -u monitor -P 'TU-CONTRASENA' -t '#' -v -C 5
```

Con `#` sale todo lo que haya en el broker; los dos que importan son estos,
con `sen66-xxxxxx` derivado de la MAC (los seis ultimos digitos, los mismos
del nombre del portal `SEN66-XXXXXX`):

```
sen66-8625d4/status  online
sen66-8625d4/state   {"pm1":2.8,"pm25":3.4,...,"co2":563,"level":"good","rssi":-66}
```

- `state`: un JSON cada 10 s con las nueve magnitudes, el nivel global y la
  cobertura WiFi.
- `status`: `online` / `offline`, con *last will*. Es lo que hace que si el
  aparato se cae, HA lo marque como no disponible en vez de dejar el ultimo
  valor congelado haciendose pasar por actual.

Ojo si quieres filtrar por tema en vez de escucharlo todo: el comodin `+` de
MQTT ocupa **un nivel entero**, asi que `sen66-+/state` no casa con nada. O
pones el identificador completo, `sen66-8625d4/#`, o escuchas `#`.

El campo `level` es un **identificador estable en ingles** (`good`, `fair`,
`moderate`, `poor`, `bad`) que no cambia con el idioma de la pantalla, para
que las automatizaciones no se rompan al cambiarlo.

Para ver el descubrimiento, si lo tienes activado:

```bash
mosquitto_sub -h localhost -u monitor -P 'TU-CONTRASENA' -t 'homeassistant/#' -v -C 11
```

## Si no conecta

| Sintoma | Causa habitual |
|---|---|
| El aparato dice que no conecta y el broker no registra nada | Mosquitto sigue solo en localhost: falta el `listener` del punto 1 |
| El log del broker dice `Connection refused: not authorised` | Usuario o contrasena mal, o falta `password_file` |
| Conecta y se cae cada pocos segundos | Dos clientes con el mismo *client id*: se echan mutuamente |
| HA no crea el aparato | Prefijo vacio, o la integracion MQTT no esta anadida en HA |
| Los valores se quedan clavados | Mira `status`: si pone `offline`, el que se cayo es el aparato |

Para ver que pasa en el broker:

```bash
journalctl -u mosquitto -n 50 --no-pager
```

## Borrar el descubrimiento retenido

Si cambias de prefijo o retiras el aparato, los mensajes de configuracion
siguen en el broker porque van retenidos, y HA seguira resucitando el aparato
fantasma. Se limpian publicando vacio en cada tema:

```bash
for k in pm1 pm25 pm4 pm10 temperature humidity voc nox co2 level rssi; do
  mosquitto_pub -h localhost -u monitor -P 'TU-CONTRASENA' -r -n \
    -t "homeassistant/sensor/sen66-xxxxxx/$k/config"
done
```
